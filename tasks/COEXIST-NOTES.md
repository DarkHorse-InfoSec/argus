# Simultaneous BLE + WiFi + LoRa - coexistence notes

Copied from the prior developer's notes so this is in-repo. This is a SEPARATE effort
from the wallpaper bug; the scaffolding is preserved behind a flag but disabled. Only
read this if you pick the coexistence work back up.

Goal: run BLE + WiFi + LoRa simultaneously (upstream r3dfish 13-37 can; maxes CPU, slows
UI, but works). argus-watch was forked from `github.com/r3dfish/13-37`.

**KEY FINDING: r3dfish does NOTHING special.** No sdkconfig, no `esp_coex_*`, no
`esp_wifi_set_ps`, no coexistence build flags. He relies on the Arduino-ESP32 core's
built-in software coexistence (CONFIG_SW_COEXIST_ENABLE=y, baked into the prebuilt libs
of `platform = espressif32@6.10.0` = arduino-esp32 2.0.17 / IDF 4.4.7). Uses **Bluedroid**
(NOT NimBLE - the "NimBLE is why he coexists" hypothesis is WRONG). Brings each radio up
on-demand, ref-counted, with no mutual-exclusion guard. LoRa (SX1262) is a separate SPI
chip - never contends with the 2.4GHz radio.

**argus's BLE/WiFi bring-up code is byte-for-byte identical to upstream.** The ONLY
difference: argus ADDED guards forbidding BLE+WiFi together, because it observed a HANG:
- `src/ble_scan_manager.cpp` ~L105-114: refuses BLE bring-up if `wifi_is_active()`.
- `src/wifi_beacon_manager.cpp` ~L152-156: refuses `WiFi.mode(WIFI_STA)` if `ble_is_active()`.
- `src/offense_wifi.cpp` L25,40: single-owner + `if (ble_is_active()) return false`.
- `src/device_mode.cpp`: mutual-exclusion mode plan (`BlockedWifiActive`).

**ON-HARDWARE RESULT 2026-07-24 - COEXIST ATTEMPT FAILED, FELL BACK.** Implemented behind
a master flag `src/radio_coexist.h` (`#define ARGUS_RADIO_COEXIST`): 1 = keepalive + all
guards `#if !ARGUS_RADIO_COEXIST`-gated out; 0 = guards active (fallback, CURRENT STATE).
Wired `ble_scan_boot_keepalive()` at END of setup() under `#if ARGUS_RADIO_COEXIST`. Gated
both the MANAGER guards AND the UI-level pre-checks (wifi_radio_screen on_toggle +
restore_power, wifi_screen start_scan) - the UI pre-checks are separate
`esp_bt_controller_get_status()==ENABLED` dialogs that fire BEFORE the manager.

Test on the T-Watch Ultra:
- Boot with BLE keepalive alone: FINE.
- Tap WiFi with BLE up (keepalive, no phone/ANCS connected): **HARD FREEZE**, battery and USB.
- WiFi "Enable at boot" ON: **BOOT LOOP** - restore_power brings WiFi up while keepalive holds
  BLE -> `WiFi.mode(WIFI_STA)` hangs -> watchdog reset -> loop.

CONCLUSION: on THIS firmware the WiFi+BLE hang is real and fundamental, reproduces even at
boot with a fresh heap, and guard-gating alone cannot fix it. Flag set back to **0**;
reflashed the guarded build to recover. All coexist scaffolding is PRESERVED.

**CORRECTION from Domenic 2026-07-24 (load-bearing): the hang PREDATES notifications.** The
ANCS persistent GATT link is NOT the origin. It froze with keepalive-only, no phone, no ANCS
GATT active. The hang is BASELINE: base BLE controller (Bluedroid) + `WiFi.mode(WIFI_STA)`
together exceed the contiguous internal SRAM on this board/build, independent of notifications.
Do NOT scapegoat ANCS.

NEXT ATTEMPT must go DEEPER than guard-gating:
- MEASURE first (safe, no hang): log
  `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)` at boot BEFORE any
  radio, after the BLE keepalive, and after detectors - find how far short of WiFi's ~40-50KB
  contiguous need we are, and WHICH consumer eats it. Shortfall should show with BLE-only.
- Then trim resident internal-DRAM footprint (Bluedroid controller + our resident .bss, NOT
  ANCS) and/or bring WiFi up EARLY at boot from a fresh heap, r3dfish-style. Diff our build's
  internal-RAM map / sdkconfig vs upstream r3dfish since he coexists on the same silicon.
- A NON-hanging boot-time log is better than the on_toggle path (which freezes). Reading
  `/Settings/coexlog.txt` off the SD is the reliable capture path.

## Measured diagnosis and fix, 2026-07-24

The guarded measurement build kept all BLE/WiFi mutual-exclusion checks enabled and
started only the BLE keepalive. `/Settings/coexlog.txt` recorded:

- Before BLE: largest internal block 81,908 bytes; total free internal 99,988 bytes.
- After BLE: largest internal block 28,660 bytes; total free internal 39,056 bytes.
- End of setup: largest internal block 20,468 bytes; total free internal 30,584 bytes.
- The earlier pre-`WiFi.mode()` hang record was the same: largest block 20,468 bytes,
  total free 30,592 bytes, BLE controller enabled.

This proves the freeze is internal-RAM exhaustion, not ANCS and not missing RF coexistence
configuration. Base Bluedroid consumed about 53.2 KB from the largest block and 60.9 KB
total; final headroom was below WiFi's startup requirement.

An exact upstream `r3dfish/13-37` build at commit
`2bf83b501cecf16c3961b94ff690c011efb09f5d`, using the same PlatformIO toolchain,
used 185,128 bytes of static RAM. ARGUS used 218,872 bytes, a 33,744-byte increase.

The targeted correction moves two WiFi-tool-only result tables out of permanent internal
`.bss` and lazily allocates them from PSRAM:

- Port Scan results: 25,600 bytes.
- Ping Sweep devices: 20,320 bytes.

After the change, ARGUS static RAM is 172,960 bytes (52.8%), a 45,912-byte recovery.
The ELF now contains only four-byte PSRAM pointers for `s_results` and `s_devices`.
Both tools retain their full capacities and fail safely if PSRAM allocation fails.

The guarded build was then flashed and measured on the watch:

- Before BLE: largest internal block 126,964 bytes; total free internal 145,832 bytes.
- After BLE: largest internal block 73,716 bytes; total free internal 84,900 bytes.
- End of setup: largest internal block 65,524 bytes; total free internal 76,428 bytes.

The end-of-setup largest block increased by 45,056 bytes on hardware, matching the
45,912-byte static-link recovery closely enough to validate the diagnosis and placement
fix. `ARGUS_RADIO_COEXIST` is now enabled for controlled BLE + WiFi + LoRa testing, with
heap logging retained temporarily.

### Live coexistence validation

The coexistence build was flashed and booted with serial capture:

- BLE controller remained enabled at WiFi startup (`bt_status=2`).
- Immediately before `WiFi.mode(WIFI_STA)`, the largest internal block was 65,524 bytes
  and total free internal RAM was 76,428 bytes.
- WiFi started without hanging.
- Wardriving remained responsive and collected both WiFi and BLE results.
- LoRa was then enabled while Wardriving continued. WiFi and BLE counts kept increasing,
  at a slower expected shared-airtime rate, LoRa stayed on, and the UI remained responsive.

This is the first confirmed simultaneous BLE + WiFi + LoRa operation on this ARGUS build.

Final hardware validation also passed:

- The watch was unplugged and restarted on battery.
- It booted normally instead of entering the former WiFi-at-boot boot loop.
- BLE + WiFi Wardriving + LoRa all operated together on battery.
- WiFi and BLE Wardriving counts continued increasing with LoRa enabled.
- The UI remained responsive.

Observed follow-up issue, separate from radio coexistence: Wardriver's START/STOP button
sometimes needs two or more taps. Investigate the LVGL input/event path and main-loop
latency without reverting the coexistence memory fix.
