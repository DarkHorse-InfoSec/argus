ARGUS Watch - Wardriver START/STOP button responsiveness handoff for Codex
==========================================================================
Repository: argus-watch  (T-Watch Ultra, ESP32-S3, LVGL 9)
Work with argus-watch as your working directory; all paths below are relative to it.
Build:  pio run -e twatch_ultra   (from the repo root; the pio CLI may not be on PATH)
Flash:  ...pio.exe run -d ... -e twatch_ultra -t upload --upload-port COM19
COM ports: DOWNLOAD mode = 303A:1001 = COM19 (only this flashes; BOOT+RESET to enter). App CDC = 303A:8227 = COM20.
          Serial monitor must open with DTR=False/RTS=False (see tasks/FLASHING-NOTES.md) or the S3 drops to download mode.
GIT: argus-watch IS a git repo, branch argus-argus. Baseline HEAD includes the coexistence fix (commit d10ab7b).
     There is UNCOMMITTED WIP in the tree (see below) - preserve it; do not commit or push unless explicitly asked.
     No Co-Authored-By trailers, no em dashes.


>>> THE ISSUE <<<
On the Wardriver screen the START/STOP button is INTERMITTENTLY unresponsive - it sometimes takes two or more taps to
start or stop wardriving. This surfaced right after BLE + WiFi + LoRa coexistence went live (all three radios now run
together), so scanning load makes it more noticeable. Do NOT assume it is a radio failure just because scanning makes
it worse - the load is almost certainly the MECHANISM (loop starvation), not a radio bug.


>>> DO NOT TOUCH <<<
- Radio coexistence behavior (radio_coexist.h, the PSRAM result-table allocations in portscan.cpp/pingsweep.cpp, the
  BLE keepalive). That fix is committed (d10ab7b) and hardware-validated - do not revert, disable, or "simplify" it.
- Wardriver readiness/gating checks (the GPS/SD/radio "why Start is unavailable" logic). The button being gated when
  not-ready is BY DESIGN; that is not the bug.


>>> WHERE IT LIVES (verified 2026-07-24) <<<
- src/wardriver_screen.cpp:
    :447  static void on_start_stop(lv_event_t *)   - the button handler.
    :617  lv_obj_add_event_cb(start_btn, on_start_stop, LV_EVENT_CLICKED, NULL)  - CLICKED binding.
    :471+ The handler ALREADY flips the UI to STOP *before* the work, with this comment: the start/stop path
          "can block the main loop for ~1 s", and (near :494) a note that "the label update made the STOP tap look
          ignored." So blocking work on this path is a KNOWN hazard - that ~1 s stall is a prime suspect.
- Main-loop latency machinery already in the tree (prior art - reuse it, don't reinvent):
    src/main.cpp:1130 main_loop_request_lvgl_priority(int cycles) - raises s_lvgl_priority_cycles; while > 0 the loop
      (main.cpp:2417+) PAUSES the heavy matrix/background work and pumps LVGL for N iterations, then decrements. This
      is the existing "give the UI airtime during interaction" lever.
    src/main.cpp:687-688, 1870 - a FALLING-edge ISR latch (back_btn_pressed / on_back_btn_isr / attachInterrupt) with
      a cooldown (BOOT_COOLDOWN_MS, main.cpp:2374) that RECOVERS fast physical BOOT-button taps the duration-poll drops.
      Same class of problem (input lost to loop latency), solved for the hardware button - a useful model, though the
      Wardriver button is an on-screen LVGL widget, not GPIO.


>>> UNCOMMITTED WIP YOU MUST INSPECT BEFORE BUILDING <<<
`git status` / `git diff` first. The working tree has related-but-separate WIP that is NOT yet committed:
- src/main.cpp: a touch-press handler that calls main_loop_request_lvgl_priority(20) on every LV_INDEV_STATE_PRESSED
  (keeps LVGL on its fast cadence during interaction), PLUS the boot_dispatch/ISR-latch BOOT-button work.
- src/time_screen.cpp: unrelated red-icon/notify WIP.
These are intentionally uncommitted. Preserve them. Note the touch-press priority boost may already be helping (or may
be the incomplete fix) - measure with it in place before adding more.


>>> LIKELY MECHANISM (hypothesis - CONFIRM, don't assume) <<<
The LV_EVENT_CLICKED for start_btn is competing with heavy per-loop background work (now BLE + WiFi + LoRa ticks every
iteration). Most probable: either the click is delivered LATE (LVGL not pumped often enough under load, so the press/
release window is missed or coalesced), or it IS delivered but the ~1 s blocking start/stop work stalls the LVGL thread
so the NEXT tap (the STOP) lands during the stall and is dropped. Less likely but check: the readiness gate silently
eating the event. Confirm which of these before changing anything.


>>> WHAT TO DO <<<
1. REPRODUCE + INSTRUMENT the missed taps. Add temporary logging in on_start_stop (entry timestamp) AND at the LVGL
   input layer to distinguish: (a) event never fired, (b) fired late, (c) fired but the handler's blocking work stalled
   the loop so a following tap was lost. Log to serial (COM20, DTR/RTS false) or /Settings during a wardrive session.
2. Determine which of the three it is - the fix differs per case:
     - Not delivered / gated: the readiness path or event routing is swallowing it (do NOT change readiness semantics;
       fix the routing).
     - Delivered late: raise LVGL cadence around interaction (reuse main_loop_request_lvgl_priority) and/or ensure the
       loop pumps lv_timer_handler often enough under coexistence load.
     - Delivered but work blocks the thread: make the ~1 s start/stop work non-blocking / deferred (flip UI now, do the
       teardown/flush off the click handler) so LVGL keeps processing input during it.
3. FIX responsiveness WITHOUT changing wardriver readiness checks or coexistence behavior.
4. Build + flash ONLY after inspecting the current uncommitted changes (step above).
5. VALIDATE on hardware: repeated SINGLE-tap starts and stops, reliably, WHILE BLE + WiFi + LoRa are all active
   (coexistence is the load that exposes it - test under that load, not idle).
6. Do NOT commit or push unless explicitly asked. Preserve the existing dirty worktree.


READ FIRST: tasks/COEXIST-NOTES.md (why the loop is now heavily loaded) and tasks/FLASHING-NOTES.md (serial/flash traps).


>>> HARDWARE DIAGNOSIS 2026-07-25 <<<
Temporary tracing was added at the LilyGo raw touch callback, the Wardriver button event
route, and the START/STOP handler work boundaries. Testing was performed with BLE + WiFi
Wardriving + LoRa active.

Measured successful tap:
- Raw touch sampled every 33-34 ms.
- Raw PRESSED reached the button-level PRESSED event in about 3 ms.
- RELEASED, SHORT_CLICKED, and CLICKED were all delivered.
- START work took about 246-250 ms (about 282-285 ms total including the forced redraw).
- STOP work took about 84 ms (about 120 ms total).

Captured apparent miss:
- Raw coordinate was x=210, y=471.
- The button occupies approximately y=380 through y=440.
- No Wardriver button event fired because the touch was 31 pixels below the button.
- This was confirmed by the user as a partial/poor tap.

After many deliberate center-button START/STOP taps under full radio load, no valid tap
failed. The user concluded the original symptom was user error. There is no evidence of
LVGL starvation, delayed routing, readiness gating, or blocking work dropping a valid
single tap in the tested build.

No production behavior change was made. All temporary tracing was removed. Do not make
the start/stop path asynchronous or change its readiness semantics based on the disproven
loop-starvation hypothesis.
