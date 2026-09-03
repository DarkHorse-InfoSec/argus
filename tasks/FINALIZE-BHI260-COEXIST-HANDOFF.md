ARGUS Watch - Retire the BHI260/coexistence scaffolding (handoff for Codex)
===========================================================================
Repository: argus-watch  (T-Watch Ultra, ESP32-S3, LVGL 9)
Work with argus-watch as your working directory; all paths below are relative to it.
Build:  pio run -e twatch_ultra   (from the repo root; the pio CLI may not be on PATH)
Flash:  ...pio.exe run -d ... -e twatch_ultra -t upload --upload-port COM19
COM ports: DOWNLOAD mode = 303A:1001 = COM19 (only this flashes; BOOT+RESET to enter). App CDC = 303A:8227 = COM20.
          Serial monitor must open with DTR=False/RTS=False (see tasks/FLASHING-NOTES.md).
GIT: branch argus-argus. HEAD ~ f2e10f4. Do NOT commit or push unless explicitly asked. Preserve the untracked
     handoff docs in tasks/. No Co-Authored-By, no em dashes.

READ FIRST: tasks/WALLPAPER-SAGA.md (why both of these are temporary), tasks/COEXIST-NOTES.md, tasks/FLASHING-NOTES.md.


>>> THE TASK <<<
Two temporary items were added while chasing the BHI260 boot-loop and the WiFi/BLE coexistence hang. BOTH root causes
are now FIXED and validated (SensorLib patches in scripts/patch_sensorlib.py; 32/32 clean battery boots). Retire the
scaffolding so the firmware is clean for a public release:
  1. The 15 s motion-accel defer - a MITIGATION for the (now-fixed) callback crash. Test whether it is still needed;
     remove it if not.
  2. The ARGUS_COEX_MEASURE boot-time heap/reset logging - a DIAGNOSTIC instrument. Turn it off (it writes to serial +
     the SD card every boot; must not ship).
ORDER MATTERS: do #1 FIRST while the #2 instrument is still on (it is your boot-time capture channel), then do #2 last.


>>> PART 1: RE-EVALUATE / REMOVE THE MOTION DEFER <<<
Context: the user motion-wake accel stream is deferred 15 s past boot as a mitigation, because the BHI260 FIFO callback
crashed in the early-boot window. That crash is now genuinely fixed at the source (both SensorLib bugs patched), so the
callback path should be safe to exercise from boot - meaning the defer is probably now UNNECESSARY, and removing it lets
motion-wake work immediately instead of 15 s late (a real UX win).
Where (src/main.cpp):
  :993  static const uint32_t MOTION_BOOT_DEFER_MS = 15000;
  :1006-1022  clock_screen_set_motion_wake(): if millis() < MOTION_BOOT_DEFER_MS, sets s_motion_accel_pending and
        returns; the stream is later started from motion_wake_poll() once past the defer.
Do NOT just delete it blindly - TEST it (this is a controlled experiment, measure, don't assume):
  a. Reduce MOTION_BOOT_DEFER_MS toward 0 (or remove the defer branch so the stream starts immediately when
     clock_screen_set_motion_wake(true) is called at boot).
  b. Reproduce the ORIGINAL failure condition: motion_wake=1 in /Settings/settings.txt, on BATTERY, repeated cold
     boots. Watch /Settings/coexlog.txt reset= codes (instrument still on from Part 2's flag).
  c. PASS = many clean cold boots (reset=1), motion-brighten works immediately, no BHI260 PANIC (reset=4). If it stays
     clean, remove the defer (and MOTION_BOOT_DEFER_MS / s_motion_accel_pending plumbing) for good.
  d. If it re-crashes: the defer is still masking something - KEEP it, and report the new backtrace (core dump) instead
     of guessing. Do not ship a known-crash.


>>> PART 2: RETIRE THE COEX MEASUREMENT INSTRUMENT (do this LAST) <<<
ARGUS_COEX_MEASURE logs heap + reset reason to serial and /Settings/coexlog.txt on every boot - fine as an instrument,
must not ship. Once Part 1 is validated, turn it off:
  src/radio_coexist.h:31   #define ARGUS_COEX_MEASURE 1   -> set to 0.
The code is already guarded: with the flag 0, coex_log_heap() compiles to an inline no-op (src/main.cpp:136) and the
call sites (main.cpp:1339/1818/1828/1948) + the SD write in wifi_radio_screen.cpp (~:238, /Settings/coexlog.txt) drop
out. Confirm via the build map / a grep of the ELF that "coexlog"/"[COEX]" strings are GONE.
IMPORTANT: do NOT remove ARGUS_RADIO_COEXIST or the BLE keepalive - the keepalive is now PRODUCTION (it is what makes
coexistence work), NOT instrumentation. Only ARGUS_COEX_MEASURE is the throwaway. Note main.cpp:1818-1828 currently
brings BLE up under `#if ARGUS_RADIO_COEXIST || ARGUS_COEX_MEASURE` - when you flip MEASURE to 0, make sure the BLE
keepalive STILL fires (ARGUS_RADIO_COEXIST is 1, so it will - but verify the keepalive is not accidentally gated only
by MEASURE anywhere).


>>> VALIDATION <<<
- After Part 1: battery cold-boot matrix with motion_wake=1 (and wallpaper=1), reset=1 every boot, motion-brighten
  immediate.
- After Part 2: rebuild, confirm no coexlog/[COEX] strings in the ELF, and one more battery boot sanity pass (BLE
  keepalive still up: bt_status was 2 in prior logs - but that log is now gone, so verify BLE+WiFi still coexist via a
  quick wardrive, not the coexlog).
- Capture the final firmware SHA-256.


>>> GUARDRAILS <<<
- Measure before removing the defer; if it still masks a crash, KEEP it and report the backtrace.
- Do NOT touch: the SensorLib patches (scripts/patch_sensorlib.py), ARGUS_RADIO_COEXIST=1, the BLE keepalive, the
  PSRAM result-table allocations, or the raw-RGB565 wallpaper.
- Do NOT commit or push unless explicitly asked. Preserve the worktree and the untracked tasks/ handoff docs.


>>> COMPLETED RESULT, 2026-07-25 <<<

Part 1 passed and the motion defer was removed from `src/main.cpp`:

- Removed `MOTION_BOOT_DEFER_MS`.
- Removed `s_motion_accel_pending`.
- Removed the early-return defer branch from `clock_screen_set_motion_wake()`.
- Removed the delayed stream-start block from `motion_wake_poll()`.
- The accelerometer still starts only after `instance.begin()` initializes the
  BHI260 firmware.

The defer-free measurement firmware completed 14/14 logged boots. Every boot
reached `setup-done` with reset=1. There were no reset=4 panics, reset=5
watchdogs, or reset=9 brownouts. BLE reached bt_status=2 on every boot and the
end-of-setup largest internal block remained 65,524 bytes. The user confirmed
that motion brightening works immediately and that repeated battery power cycles
produced no boot loops.

The measurement evidence is archived on the SD card at:

`G:\Settings\regression_backup_20260725\motion_defer_removed_coexlog.txt`

Part 2 passed and the measurement instrument was retired:

- `ARGUS_RADIO_COEXIST` remains 1.
- `ARGUS_COEX_MEASURE` is now 0.
- The production `ble_scan_boot_keepalive()` remains enabled.
- The pre-WiFi diagnostic in `src/wifi_radio_screen.cpp` was corrected from
  `#if ARGUS_RADIO_COEXIST` to `#if ARGUS_COEX_MEASURE`. Without this correction,
  the standalone `[COEX]` serial output and SD write would have remained in
  release builds.

The release ELF contains neither `coexlog` nor `[COEX]`. Disassembly confirms a
direct setup call to `ble_scan_boot_keepalive()` at address 0x42018332, targeting
the keepalive function at 0x42009660.

Release build:

- Static RAM: 172,960 / 327,680 bytes, 52.8%.
- Flash: 2,937,913 / 3,145,728 bytes, 93.4%.
- Firmware SHA-256:
  `5C85AE64650F9090957A559ED971548E36CAA12C78E6DECB64276FA473ACD541`
- ELF SHA-256:
  `845DB9E5A7C3077FDD8E66D9F8F8F443ED3C23FDDFA0A0973889EE26BD7D6770`

The release image was flashed on COM19 and every esptool partition hash verified.
The user completed the full battery sanity test twice, once immediately after
unplugging and once after a battery restart:

- Clock and wallpaper returned normally.
- Motion brightening worked immediately.
- Wardriving collected both WiFi and BLE results.
- Enabling LoRa kept the watch responsive while Wardriving continued.
- Wardriving START/STOP remained responsive.
- No boot loop or freeze occurred.

One four-line `coexlog.txt` was present when the SD returned to the laptop. Its
single boot group came from the old measurement firmware briefly running after
the SD was inserted but before the release flash. Neither of the two release
boots appended a group. That final pre-release record was archived as:

`G:\Settings\regression_backup_20260725\pre_release_flash_coexlog.txt`

There is now no active `/Settings/coexlog.txt`. The release firmware will not
recreate it.

Final conclusion: both temporary scaffolds are retired. The source-level BHI260
fixes, immediate motion wake, raw RGB565 wallpaper, PSRAM coexistence memory
fixes, BLE keepalive, and simultaneous BLE + WiFi + LoRa operation all remain
intact.
