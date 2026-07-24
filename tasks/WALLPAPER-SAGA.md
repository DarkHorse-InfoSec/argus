# WALLPAPER BOOT-LOOP SAGA (READ THIS FIRST)

This is the full history of the wallpaper boot-loop bug on the T-Watch Ultra
(ESP32-S3, LVGL 9), copied verbatim from the prior developer's working notes so a
new agent has the complete context in-repo. `argusprompt.txt` is the short handoff;
this is the long form with every disproven fix. Read this before touching
`src/background.cpp`.

Board / build facts you need up front:
- USB flags live in `boards/lilygo-t-watch-ultra.json`, NOT `platformio.ini`:
  `-DARDUINO_USB_MODE=0` (TinyUSB OTG) and `-DARDUINO_USB_CDC_ON_BOOT=1`. This is
  WHY the ESP-IDF panic console never appears on the app CDC port (COM20): in OTG
  mode the hardware USB-Serial-JTAG console is not active. To surface the panic text
  you must either switch to a core-dump-to-flash workflow or flip to
  `ARDUINO_USB_MODE=1` as a TEMPORARY diagnostic build (that changes/kills the
  TinyUSB composite: CDC + the USB SD mass-storage feature).
- `monitor_speed = 115200`.
- `LV_MEM_SIZE` is only 64 KB, set in `lib/LilyGoLib/src/lv_conf.h`. This is central
  to the OOM findings below.
- Wallpapers on this board MUST be raw RGB565 (or PNG historically); the JPEG decoder
  (TJPGD) is unreliable here. Do not "optimize" a working raster into a JPEG.
- Do NOT trust `instance.pmu.isVbusIn()` as a USB-present signal on this board; it was
  proven unreliable (details below).

## Separate, UNRELATED boot loop to not chase

There is a SEPARATE first-boot-after-reset loop: the watch loops ONCE on the first
reset, then boots fine. It happens with wallpaper OFF too, so it PREDATES the
wallpaper and is NOT this bug. Disambiguate via the logged `reset=` codes in
`/Settings/bootlog.txt`. Do not attribute it to the wallpaper.

---

## Full journey (verbatim from working notes)

**STILL OPEN 2026-07-24 (end of session): an INTERMITTENT wallpaper boot PANIC remains, UNSOLVED.**
Separate from the fade (fade fixed the render-hold). With wallpaper ON, on BATTERY only, the watch
intermittently crashes at the ~10-12s wallpaper load/render and boot-loops. Reset codes: 4 (PANIC) +
5 (INT_WDT), NEVER 9 (brownout). Plugged into USB = boots fine. TWO code fixes DISPROVEN this session:
(1) BOUNCE BUFFER (SD->DRAM->memcpy PSRAM) = no fix (not SD-DMA-to-PSRAM). (2) LOAD-IN-SETUP (all raster
reads moved out of the loop into setup) = no fix AND WORSE - markers proved crashes still happen DURING
the load in setup, so NOT loop concurrency; loading all 3 rasters = 3x reads = 3x crash exposure -> loops
more. Serial-backtrace via the app CDC (COM20) = dead end (panic console isn't on TinyUSB CDC). LEADING
HYPOTHESIS: POWER/PSRAM glitch on battery - rail sag during the high-current 411KB SD read + AMOLED render
corrupts a PSRAM access (PANIC) or glitches the core (INT_WDT) without tripping brownout(9). NEXT SESSION:
(a) recover the watch (pull SD or wallpaper=0 - it is LEFT boot-looping); (b) GET THE REAL BACKTRACE
(ESP core-dump to flash + esp-coredump against firmware.elf, OR find the USB-Serial-JTAG console) - stop
guessing; (c) REVERT load-all-3 back to current-raster-only.

**FADE-IN fix 2026-07-24 (this part WORKED - render holds on battery; the panic above is a DIFFERENT, still-open issue):** After the raw-RGB565-into-PSRAM + 10s boot-settle defer
fixes, a battery boot-loop REMAINED. bootlog showed reset=5 (INT_WDT), NOT 9 (brownout) -
i.e. something BLOCKED, not a current spike. Cause: the fade-in ramp stepped opacity 0->bg_opa
every 55ms, and each step invalidated the FULL-screen 410x502 image -> ~15 full-screen
ALPHA-BLEND re-renders in <1s, which tripped the interrupt watchdog on battery. Fix: dropped
the fade entirely (background.cpp), render ONCE directly at bg_opa in apply_current(). Kept the
10s defer + the PSRAM preload. Added a "wallpaper applied ms=" bootlog marker after the deferred
render to localise any future loop (during-render vs after-render). Enable via Settings ->
Wallpaper toggle (persists wallpaper= in settings.txt). NOTE: a SEPARATE first-boot-after-reset
loop persists (watch loops once on the 1st reset then boots fine) - it predates the wallpaper
(happens with wallpaper OFF too), so it is NOT this bug; diagnose via the logged reset= codes.

**PHASE-MARKER LOCALIZATION 2026-07-24 (fresh, clean bootlog after clearing it):** added
`bootlog_phase()` markers through setup() + a "loop" marker + the "wallpaper applied" marker.
Healthy boot: `boot reset=1 -> pre-screens(~6.2s) -> screens-done(~8.5s) -> radios-pre(~8.8s)
-> setup-done(~8.9s) -> loop(~9.0s) -> wallpaper applied(~11.0s)`. The ONE loop in the batch:
a boot reached `loop`(9.24s) but logged NO `wallpaper applied`, and the NEXT boot showed
**reset=4 (PANIC)**. => the crash is an actual EXCEPTION (not brownout, not INT_WDT) landing in
the **~10s DEFERRED WALLPAPER RENDER window** (between the loop marker at ~9.2s and the
wallpaper-applied marker at ~11.0s; kBootSettleMs=10000). Boot radios + screen creates are
CLEARED (radios-pre->setup-done completed on every boot) - NOT the culprit. The first wallpaper
render (apply_current: load_raster's ~411KB SD read INTO the lv_timer callback, then the render)
usually succeeds but INTERMITTENTLY panics (~1 in 8). This is a DIFFERENT signature from the
older wallpaper-OFF first-boot INT_WDT(5) loop (which did NOT recur in this batch). NEXT: catch
the panic's BACKTRACE over serial (plug in + serial monitor + reset-cycle) for the exact crashing
line; a code panic should still fire plugged in. If it won't repro on USB, add a defer_cb-entry
marker to split the loop-vs-render window on battery. Markers are TEMPORARY - remove once fixed.

Adding a mode-specific Offense wallpaper (skull2 on the clock face, swapped in on
argus_mode_on_change) BOOT-LOOPED the watch on Offense entry. Three attempts failed
before reverting (2026-07-22).

**Root cause (of the ORIGINAL OOM class):** LVGL's JPEG (TJPGD) / PNG decoder buffers the WHOLE compressed
file in the ESP32-S3's scarce INTERNAL SRAM to inflate it (background.cpp documents this).
The existing wallpaper works because it is decoded ONCE at BOOT, when ~100-130 KB
internal SRAM is free. A mode swap decoded a NEW image (offense.jpg) at RUNTIME,
where the heap is fragmented and low (seen as low as ~35 KB free with radios up).
The decode's malloc fails -> panic -> reset -> boot-loop on every Offense entry.

**What did NOT fix the OOM (dead ends):**
- Shrinking offense.jpg 59KB -> 25KB: still crashed.
- A runtime free-SRAM guard (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
  >= 80KB before decoding): still crashed. Either the guard threshold/metric was
  wrong for TJPGD's real peak, or the crash path was not the one guarded.

**How to do a mode-aware wallpaper RIGHT (untested plan):** decode at BOOT, when SRAM is free,
and rely on the LVGL image cache (2MB, in PSRAM) so entering a mode is a CACHE HIT with NO runtime
decode. The wallpaper budget checks (kMaxWallpaperFileBytes=100KB, kMaxWallpaperPixels=400k) are
calibrated for BOOT free SRAM, NOT runtime - do not trust them for a runtime decode.

**USB-ONLY variant (boots on battery, boot-loops when plugged into USB) - 2026-07-23:**
Same root cause, tipped over by USB. The ESP32-S3 native USB stack (CDC + MSC composite)
consumes internal SRAM at boot, so a wallpaper decode that JUST fits on battery
(~100-130KB free internal) OOMs when plugged in (less free) -> boot-loop on USB only.
Diagnostic: turn the wallpaper OFF in Settings - if the USB loop stops, it's the decode.

**RESOLVED 2026-07-23 - the SHRINK fix does NOT work on USB; a VBUS gate does (BUT SEE LATER - it was unreliable).**
Shrinking wallpaper.png 72KB -> wallpaper.jpg 26KB STILL boot-looped on USB. USB reduces free
internal SRAM so far that even a ~26KB decode OOMs. A VBUS-power gate at the top of `load_source()`
(`if (instance.pmu.isVbusIn()) return false;`) was tried - skip the decode on USB. Later proven
UNRELIABLE (see below): isVbusIn() is not a clean USB-present signal while charging on this board.

**SECOND finding, same day - JPEG wallpapers crash the decode on BATTERY too, not just OOM on USB.**
Ground truth: on USB it booted fine, on battery it looped - the only thing changed on the battery
path was the FILE (a 26KB wallpaper.jpg). That JPEG crashes the decoder on battery where the 72KB
PNG decoded fine. **TJPGD (LVGL's JPEG decoder) is unreliable on this board** - every JPEG wallpaper
tried boot-looped; the ONLY wallpapers that ever worked are PNGs (LODEPNG). **Rule: wallpapers on
this board MUST NOT be JPEG.** Fix was to restore the 72KB PNG.

**THIRD finding + CONFIRMED OOM root cause (2026-07-23).** Set `wallpaper=0` in
/Settings/settings.txt: boots cleanly on BOTH battery AND USB with the SD in. So the crash is
DEFINITIVELY the wallpaper decode running out of memory - not corruption (chkdsk: "found no
problems"), not JPEG-specific, not USB-specific. `LV_MEM_SIZE` in lib/LilyGoLib/src/lv_conf.h is
only **64 KB**, and LODEPNG/TJPGD buffer the WHOLE compressed file in scarce internal SRAM. The
offensive-tools firmware bloated the static footprint (Flash 93%, RAM 66.7% static) so there is no
longer enough contiguous free internal SRAM at boot for that buffer.
- `instance.pmu.isVbusIn()` VBUS gate was UNRELIABLE / ineffective here: on USB with a PNG
  wallpaper it still crashed. Do NOT trust isVbusIn() as a USB-present proxy on the T-Watch Ultra.

**FOURTH finding - BMP did NOT fix it either. The decode crashes on battery for EVERY format.**
Shipped a decoder-aware fail-soft gate + a 24-bit BMP wallpaper (BMP streams area-by-area, should
not OOM). Result: STILL boot-loops on battery, boots on USB. Confirmed exhaustively: wallpaper ON +
battery + SD = boot-loop for PNG, JPEG, AND BMP; wallpaper=0 boots every time; USB boots every time.
"Boots on USB, loops on battery" does NOT fit a simple internal-SRAM OOM (USB has <= battery's free
SRAM, so USB should crash first). Live hypothesis that DOES fit: a POWER BROWNOUT - decoding +
drawing the full-screen image spikes current, the battery rail sags, brownout-reset -> loop; on USB
the 5V rail is stiff so no brownout.

**FIFTH finding + interim state (2026-07-23) - the BMP lag broke the BOOT button.** The 24-bit BMP
rendered on USB, but LVGL never caches a streaming get_area image, so it RE-DECODES from SD on every
render. That starved the main loop enough that the POLLED BOOT button (GPIO0 duration state machine
in main.cpp loop) started missing/misclassifying presses. So the wallpaper was the root cause of
BOTH the battery boot-loop AND the "BOOT button doesn't work / haptic screwed up" symptom.

**SIXTH (2026-07-23) - FIXED the memory+lag via raw RGB565 -> PSRAM. background.cpp fully rewritten.**
The wallpaper is now a raw, panel-sized (410x502) little-endian RGB565 file
`/backgrounds/wallpaper.rgb565` (410*502*2 = 411640 bytes; matches LV_COLOR_DEPTH 16, no swap). At
first-enable, load_source() heap_caps_malloc()s a PSRAM buffer (MALLOC_CAP_SPIRAM), reads the file
once into it, fills a static lv_image_dsc_t (magic=LV_IMAGE_HEADER_MAGIC 0x19,
cf=LV_COLOR_FORMAT_RGB565) and lv_image_set_src(bg_img, &dsc). NO decoder runs at all: no
internal-SRAM inflate (no OOM), no per-render re-decode (no lag, button stays fine). Generate the
.rgb565 on the host (PIL: convert RGB -> pack ((r>>3)<<11)|((g>>2)<<5)|(b>>3) as little-endian u16).
If colors look byte-swapped, switch generation to big-endian (no firmware change).

**SEVENTH (2026-07-23) - raw/PSRAM exposed a BATTERY BROWNOUT; fixed by deferring the load.**
With raw->PSRAM the wallpaper finally RENDERED on battery but still boot-looped: "wallpaper shows
THEN loops," USB boots fine, unplugging a RUNNING watch keeps working. That signature = power
brownout during the COLD-BOOT current peak (display init + radios + first full-screen paint all at
once). FIX: defer the first wallpaper load/reveal until kBootSettleMs after boot via a one-shot
lv_timer. VERIFIED: boots on battery, wallpaper renders, no loop.

**EIGHTH - the brownout RECURRED after per-mode wallpapers + red border; kBootSettleMs 6000 -> 10000,
then a fade was added, then the fade itself caused an INT_WDT (see the FADE-IN entry at the top).**
Adding 3 per-mode rasters (daily/defense/offense) + the red Offense border frame lengthened the
cold-boot surge, so 6s wasn't enough (bumped to 10000). Recurred again when Defense was recolored
red->blue (blue AMOLED subpixels draw more current). A fade-in was added to soften di/dt, but the
fade re-rendered the full image ~15x -> INT_WDT, so the fade was then REMOVED (render once at bg_opa).
Also pre-loads all mode rasters into PSRAM after boot (preload_cb) so mode switches don't stall on an
SD read. Added the temporary /Settings/bootlog.txt diagnostic (esp_reset_reason + free heap in
write_bootlog) - REMOVE it once stable. Lesson: the deferral margin scales with how much the boot
draws; more wallpapers/chrome = longer surge = more defer needed.

### CURRENT STATE
raw RGB565 -> PSRAM (once) + 10s deferred load + render-once-at-opacity (no fade). Renders and holds
on battery. The ONE still-open bug is the INTERMITTENT ~1-in-8 PANIC(4)/INT_WDT(5) in the deferred
render window, on battery only. Leading hypothesis: power/PSRAM glitch. Next action: capture the real
backtrace (core dump to flash, or the USB-Serial-JTAG console) before any further code change.
