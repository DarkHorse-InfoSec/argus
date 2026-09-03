ARGUS Watch - Wallpaper + coexistence battery-boot regression check (handoff for Codex)
=======================================================================================
Repository: argus-watch  (T-Watch Ultra, ESP32-S3, LVGL 9)
Work with argus-watch as your working directory; all paths below are relative to it.
Build:  pio run -e twatch_ultra   (from the repo root; the pio CLI may not be on PATH)
Flash:  ...pio.exe run -d ... -e twatch_ultra -t upload --upload-port COM19
COM ports: DOWNLOAD mode = 303A:1001 = COM19 (only this flashes; BOOT+RESET to enter). App CDC = 303A:8227 = COM20.
          Serial monitor must open with DTR=False/RTS=False (see tasks/FLASHING-NOTES.md) or the S3 drops to download mode.
GIT: branch argus-argus. HEAD ~ d641b72. This is a REGRESSION CHECK, not a new-bug hunt. Do NOT revert either shipped
     fix. Do not commit or push unless explicitly asked. No Co-Authored-By, no em dashes.

READ FIRST: tasks/WALLPAPER-SAGA.md (the brownout history + the raw-RGB565 fix), tasks/COEXIST-NOTES.md (the RAM fix +
BLE-at-boot), tasks/FLASHING-NOTES.md.


>>> THE TASK <<<
Two fixes shipped this session were each validated ALONE but never TOGETHER on battery:
- Wallpaper (commit history: raw RGB565 -> PSRAM + a ~10 s deferred render, tuned to dodge the COLD-BOOT CURRENT SURGE
  that used to brown out the battery rail).
- Coexistence (d10ab7b): frees internal SRAM AND brings the BLE controller up at end of setup via
  ble_scan_boot_keepalive() - which adds NEW RF current during the exact boot window the wallpaper defer was tuned against.
Confirm that wallpaper-ON + coexistence-ON boots cleanly on BATTERY, across repeated cold boots, and especially in the
worst case below. If it regresses, fix it WITHOUT disabling either feature.


>>> WHY THE RISK IS REAL (the seam between the two fixes) <<<
The wallpaper boot-loop was ultimately a battery BROWNOUT: display init + radios + first full-screen paint all drawing
current at once sagged the rail. The fix defers the wallpaper render ~10 s past boot so it lands AFTER the surge. That
margin was tuned when NO radio came up at boot. Coexistence now brings BLE up at boot (ble_scan_boot_keepalive), adding
current INSIDE that same sensitive window. The wallpaper-vs-radio ordering was never re-tuned for it. Classic "two
independently-safe changes that interact."


>>> GROUND TRUTH (verified 2026-07-25) <<<
Boot order in src/main.cpp setup():
  :1339  coex_log_heap("after-instance-begin")
  :1426  background_create(clock_screen)              - wallpaper object created (not yet rendered)
  :1818  coex_log_heap("before-ble")
  :1827  ble_scan_boot_keepalive()                    - *** BLE controller comes up HERE (new RF current) ***
  :1828  coex_log_heap("after-ble")
  :1879-1925  wallpaper decode/render ordering block  - forces the wallpaper decode with lv_refr_now()
  :1948  coex_log_heap("setup-done")
Wallpaper defer: src/background.cpp:55 `kBootSettleMs = 10000` (10 s). background_set_enabled() (:175) schedules
  defer_cb (:138) if millis() < kBootSettleMs; the render happens ONCE at bg_opa (:134), no fade. Default opacity
  bg_opa = 75/255 (background.cpp:32, "faint by design").
KEY NUANCE - the main.cpp:1879-1925 comments say "decode the wallpaper while internal SRAM is still free, BEFORE a boot
  radio" and warn about "wallpaper + WiFi-at-boot" internal-SRAM pressure. Those comments PREDATE the raw-RGB565 -> PSRAM
  rewrite: the wallpaper now lives in PSRAM (heap_caps_malloc MALLOC_CAP_SPIRAM) and no longer needs internal SRAM to
  render, so the SRAM argument in those comments is likely STALE. Verify against the current code; do not treat the old
  SRAM-ordering rationale as ground truth. The LIVE risk is CURRENT/brownout, not internal-SRAM contention.
Instrumentation is ALREADY ON: ARGUS_COEX_MEASURE=1 (src/radio_coexist.h) makes coex_log_heap() log to
  /Settings/coexlog.txt each phase, INCLUDING esp_reset_reason() (field `reset=`). Keep it on for this task - it is your
  boot-time capture channel. Reset codes: 9 = BROWNOUT (the smoking gun), 4 = PANIC, 5 = INT_WDT, 1 = normal.


>>> TEST MATRIX (the untested combinations) <<<
Wallpaper is OFF by default on the debug SD (wallpaper=0 in /Settings/settings.txt). Explicitly set wallpaper=1 to test.
Run each ~8-10 cold boots on BATTERY (unplugged), reading /Settings/coexlog.txt `reset=` after each:
  A. wallpaper=1 + coexistence (BLE keepalive) on, WiFi NOT enabled at boot.
  B. *** WORST CASE: wallpaper=1 + coexistence + WiFi "Enable at boot" ON *** - display + BLE + WiFi + full-screen paint
     all in the boot-current window at once. This is the combination most likely to brown out and was NEVER tested.
  C. Sanity: wallpaper=0 + same radio config (isolates wallpaper as the added-current variable).
A clean pass = no boot loop and `reset=1` every boot on battery for A and B. Any `reset=9` (or a loop) = brownout regression.


>>> IF IT REGRESSES - LEVERS (measure first, pick from the data) <<<
Do NOT disable coexistence or the wallpaper. Prefer, in rough order:
  1. Raise kBootSettleMs (background.cpp:55) so the wallpaper render lands even later, clear of the BLE-up (and WiFi-up)
     current. The saga already bumped this 6000 -> 10000 as boot draw grew; more radios at boot may need more.
  2. STAGGER: ensure the wallpaper render and the BLE/WiFi bring-up don't peak together - e.g. hold the wallpaper until
     after the radios are up and steady, or bring BLE up a beat earlier so its inrush is done before the paint.
  3. Lower the default bg_opa (background.cpp:32) - a fainter first paint draws less AMOLED current (di/dt).
  4. Only if none of the above hold: gate the wallpaper render on VBUS/steady-state, but note isVbusIn() was proven
     UNRELIABLE on this board (see WALLPAPER-SAGA.md) - do not lean on it.
Re-validate the full matrix after any change.


>>> GUARDRAILS <<<
- Regression check FIRST. If A, B, and C all pass clean on battery, the answer is "no regression" - report that and
  change NOTHING (do not tune timing that isn't broken).
- Keep ARGUS_COEX_MEASURE=1 for this task (it is the instrument). Turning it off is a SEPARATE cleanup task - not this one.
- Do NOT revert/disable/simplify the coexistence fix or the raw-RGB565 wallpaper. Do NOT re-introduce a JPEG/PNG wallpaper
  decoder (both OOM'd - see WALLPAPER-SAGA.md); the wallpaper MUST stay raw RGB565 in PSRAM.
- Capture ground truth from /Settings/coexlog.txt `reset=` codes - on battery there is no serial. Confirm on battery, not USB.
- Do NOT commit or push unless explicitly asked. Preserve the existing worktree.


>>> MATRIX A PANIC AND BACKTRACE-DRIVEN FIX, 2026-07-25 <<<
Matrix A (wallpaper on, firmware BLE keepalive on, optional boot radios off) completed
three normal battery boots, then looped. The fresh coexistence log showed reset=4
(PANIC), not reset=9 (brownout). Therefore wallpaper timing/current tuning was not
changed.

The flash core dump matched firmware.bin SHA-256
`1C50B6D39ED79DF5E73517B8B68C4B59F4900C360088341830169A6ECB2CE926` exactly.
It captured IllegalInstruction at garbage target `0x3225A54A` through:

`loop -> LilyGoUltra::loop -> SensorBHI260AP::update -> bhy2 FIFO parsing ->
BoschParseStatic::parseData -> SensorBHI260AP::parseData ->
BoschParseCallbackManager::call`

The prior qualified/non-virtual bridge patch worked, but exposed the actual defect one
level deeper. On the GCC 8 `USE_CUSTOM_VECTOR` path, BoschParseCallbackManager has a
member `size` (registered callback count) and call() also names its FIFO payload-length
parameter `size`. The custom-vector loop used the parameter, walked payload-length
entries beyond the initialized callback array, and called an uninitialized function
pointer. Disassembly confirmed the crashing instruction was `callx8 a9` at the manager
callback call site.

`scripts/patch_sensorlib.py` now also applies a guarded rewrite that:
- renames the payload parameter to `data_size`;
- bounds the custom-vector loop with `this->size`;
- still passes `data_size` to the valid callback.

Generated source and disassembly were verified: the new loop bound loads the manager
member at SensorBHI260AP+76 instead of the FIFO payload length. Static RAM remains
172,960 bytes. Patched firmware SHA-256:
`35CBB2E710E5C73935214CB284302DE221AC83168AEC75219EB477359611363C`.

The patched image was flashed and verified. The old dump was preserved under
artifacts/coredump and the dedicated flash coredump partition was cleared before the
Matrix A retest.


>>> FINAL BATTERY REGRESSION RESULT, 2026-07-25 <<<
The patched firmware passed the full cold-boot matrix on battery:

- A, wallpaper on + BLE keepalive + WiFi boot off: 12/12 completed setup.
- B, wallpaper on + BLE keepalive + WiFi boot on: 9/9 completed setup.
- C, wallpaper off + BLE keepalive + WiFi boot on: 11/11 completed setup.

All 32 post-fix boots logged reset=1. There were no reset=4 panics, reset=5
watchdogs, or reset=9 brownouts. Matrix B and C logged bt_status=2 and an internal
heap minimum of about 30,708 bytes, confirming that WiFi and the firmware BLE
keepalive were active together in the intended worst-case configuration.

Conclusion: there is no remaining wallpaper/coexistence battery-boot regression.
The one reproduced failure was a SensorLib callback-table overrun, not wallpaper
current draw or a brownout. Both the raw RGB565 wallpaper and radio coexistence
behavior remain enabled and unchanged. The original SD preferences were restored
after testing: wallpaper on, WiFi boot off, BLE boot on, LoRa boot off, GPS boot on.

Archived evidence is in `G:\Settings\regression_backup_20260725`:

- `matrix_A_coexlog.txt`: pre-fix failure, including reset=4.
- `matrix_A_after_fix_coexlog.txt`: 12 clean boots.
- `matrix_B_coexlog.txt`: 9 clean boots.
- `matrix_C_coexlog.txt`: 11 clean boots.
