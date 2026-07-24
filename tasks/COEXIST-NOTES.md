# Simultaneous BLE + WiFi + LoRa - coexistence notes (PARKED)

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
