ARGUS Watch - Validate BLE tracker-detection escalation, then retire debug (handoff for Codex)
==============================================================================================

CURRENT STATUS UPDATE - 2026-07-30 (strobe fix FIELD-VALIDATED, PASS)
=====================================================================

This section supersedes everything below it anywhere the two conflict.

VERDICT: the report_raise() strobe fix (e60a24f) TOOK. Confirmed against a real
AirTag on a second outdoor drive.

Run: 2026-07-30 08:40-09:24, outdoor with GPS lock, real AirTag. (Its rotating
BLE address is on the card in ThreatRadar/discovered.txt and is deliberately not
repeated in this repo; the folded id below identifies it here.)
ThreatRadar promoted it to LIKELY at 08:50:34 (21
waypoints, 7496 m span, 10 min dwell, peak RSSI -54). bledetect.log logged the
folded id 0d01f5a8 151 times, max flag=4, span 5640 s across 8 geo cells. The
Airtag domain was still at level 3 when the log ends, i.e. the tail had not
cleared.

HOW TO READ THE EVIDENCE (do this first, or the numbers mislead):
/Settings/threat_log.txt APPENDS ACROSS BOOTS and is keyed by boot_sec, not wall
clock. Segment it on boot_sec resets before analyzing - the 2026-07-30 pull holds
22 boot segments. Only two segments carry an Airtag tail reaching level 3, and
AirTag/discovered.txt holds exactly two real tails (07-29, 07-30), so file order
identifies them without needing wall-clock correlation. Cross-check: the last
segment's max boot_sec (4742) for 07-29 matches the "~t=9 to ~t=4739 s" session
recorded in the 07-29 section below.

  Pre-fix segment (07-29): 415 Airtag transitions. From t=2588 to t=4742 it flips
  0<->3 continuously - about one flip every 5.5 s, sustained 36 minutes. The level
  never holds and decay() never gets to run. This is the reported symptom.

  Post-fix segment (07-30): 45 transitions across 92 minutes, ZERO 0<->3 flips.
  Rises are monotonic (0->1->2->3). Every fall is the decay staircase at exactly
  20 s per step (kDecaySec=20): t=3621 3->2, 3641 2->1, 3661 1->0; likewise
  3489/3509/3529 and 5471/5491/5511. That is the staircase the commit said was
  being starved, now running.

RESIDUAL (benign, no action): six single-step dips where the level drops 3->2 and
is re-raised to 3 one second later (t=2526/2527, 5225/5226, 5935/5936,
6198/6199, 6386/6387, 6727/6728). Cause: the tracker's advert gap occasionally
exceeds kDecaySec, decay fires one step, the next advert re-raises. It does NOT
reach the UI - detect_pipeline flips the HADES accent at level >= Alert(2)
(detect_pipeline.cpp:295-297) and the dip bottoms out at exactly 2, so the accent
stays hot throughout. The pre-fix strobe crossed 3<->0, which does cross that
threshold, which is why it was visible on the watch.

GPS lock flapping: 26 lock drops / 25 re-acquires in the 07-30 session (was 37 on
07-29), several only 1-2 s apart (e.g. t=6240 lock=1, 6241 lock=0, 6245 lock=1,
6246 lock=0). 246 distinct geo cells were emitted across the run. Independent of
the tracker fix - the tail still escalated and held through it - but every flap
blanked the geo-cell trail the tail detector depends on. FIXED, see below.

Same-run wardrive (Wardrive/20260730_084018.csv): 08:40:20-09:23:54, 351 rows /
351 unique MACs, 249 WiFi + 102 BLE, RSSI -96..-43 (avg -85), 236 WPA2-PSK-CCMP
and 13 open [ESS]. Every row carried coordinates and NOT ONE recorded an unknown
position, despite the 26 lock drops above - that is the bug, not a clean run.
FIXED, see below.

Checked and NOT a defect: 168 of the 249 WiFi rows have a blank SSID and 122 of
those have locally-administered MACs. Initial suspicion was client/probe frames
being written as AP rows; that is wrong. wifi_beacon_manager.cpp:32 keeps only
beacons ((frame[0] & 0xFC) == 0x80) and :61 keeps only infrastructure APs
(cap & 0x0001), so these are genuinely hidden-SSID APs and legitimate WiGLE data.
No change made.

-----------------------------------------------------------------------------
FIXES IMPLEMENTED 2026-07-30 (built + host-tested, NOT yet field-verified)
-----------------------------------------------------------------------------

All three defects were the same shape: state sampled INSTANTANEOUSLY where the
consumer needed it STABLE.

1. AirTag / decay period (src/detect/threat_state.{h,cpp})
   Measured tracker verdict cadence: avg 37.7 s, ~60 s mode, 65 of 150 gaps over
   20 s. The flat kDecaySec=20 meant a LONE tracker decayed High->None across one
   advert gap, so the HADES accent (flips at Alert) would go cold mid-tail. The
   07-30 run only looked healthy because four other tracker-flagged devices kept
   the domain's anchor fresh.
   Fix: per-domain decay via ThreatState::decay_sec_for(). Airtag/Tail get
   kSlowDecaySec=90 (1.5x the observed mode); flood domains keep 20 s, since they
   report a self-relaxing in-window aggregate. NOTE: written as a single-return
   constexpr - the ESP32 core is C++11, where a constexpr body must be one
   return; the C++17 host harness accepts either, so this only fails on-device.

2. GPS / lock hysteresis (src/gps_screen.{h,cpp} + consumers)
   New gps_screen_has_stable_lock(): rises instantly with the fix, but a loss
   must persist 10 s before it goes false, and the satellite floor has hysteresis
   (acquire 4, hold 3). Evaluated once per second from the existing
   on_gps_update() timer; powering GPS off drops it immediately (deliberate loss,
   not a dropout). gps_screen_has_lock() is UNCHANGED and still drives the UI.
   Switched onto the stable predicate: ble_detect_pipeline.cpp (cell publish - a
   debounced drop now re-emits the same cell instead of -1, so the trail holds)
   and threat_radar.cpp:304 (waypoint capture, so both agree).

3. Wardriver / position provenance (src/wardriver_screen.cpp)
   New gps_stamp_now() replaces both gps.location.isValid() gates. isValid() is
   not a liveness test - TinyGPSPlus holds the PREVIOUS POWER CYCLE's coordinates
   until fresh NMEA arrives, which is why zero rows read 0,0 across 26 dropouts.
   Rows now carry coordinates only under a stable lock (bounding staleness to the
   10 s debounce, ordinary survey accuracy) and write 0,0 otherwise, WiGLE's
   "position unknown" convention.
   AccuracyMeters was hardcoded 0.0; now HDOP x 5 m UERE, floored at 5 m, and 0.0
   only when the position is genuinely unknown.
   Also moved the wardriver's READY gate (on_start_stop + the two button-colour
   sites) onto the stable lock so a tap landing in a 1 s dropout is no longer
   refused. The GPS status ICON deliberately stays on the instantaneous
   predicate - it should show the fix actually coming and going.

Verification so far: host suite 1733 checks / 0 failures (was 1723; +3 tests
covering slow-domain hold across a 60 s gap, slow-domain decay on its own period,
and flood domains keeping the fast staircase). Firmware builds, flash 94.3%.
Field verification is still OUTSTANDING - see the on-device checklist below.

NEXT FIELD RUN should confirm:
- bledetect.log: GPS lock=0 events well below 26, and no cell=-1 for sub-10 s
  dropouts.
- threat_log.txt (segment on boot_sec resets FIRST): no single-step 3->2 dips
  across a ~60 s advert gap.
- Wardrive/*.csv: rows captured during a genuine (>10 s) dropout read 0,0, and
  AccuracyMeters is populated rather than 0.0.
ARGUS_BLE_DETECT_DEBUG stays 1 until that passes.

CURRENT STATUS UPDATE - 2026-07-29 (real-AirTag moving-vehicle run + strobe fix)
================================================================================

This section is superseded by the 2026-07-30 section above where the two conflict.

What was run:
- Domenic ran a real-AirTag test in a moving vehicle (device DES-70072, SD card
  read afterward on drive E). Ground truth from Domenic: the AirTag rode in a
  laptop bag on the passenger seat (co-moving with the watch on his wrist) for
  most of the drive; he briefly entered a house mid-route, and at the end entered
  his employer and may have walked away from the bag before powering off.
- boot_radios: ble=1, gps=1, wifi=0, lora=0. Debug still ARGUS_BLE_DETECT_DEBUG=1.
- Newest session in /Settings/bledetect.log ran ~t=9 to ~t=4739 s.

The reported symptom (BLE + SD status icons turned red) is NOT a fault:
- status_accent_active() (src/main.cpp:399) recolors EVERY active status icon to
  HADES_RED when threatradar_top_level() >= TR_LVL_LIKELY. A hardware fault shows
  GRAY, never red.
- Legacy Threat Radar escalated the co-moving AirTag (rotating BLE address on the
  card, not repeated here) to
  LIKELY at 08:20:37 (ThreatRadar/discovered.txt: Waypoints 3, Span 7034m, Dwell
  11min), ~30 s before the 08:21:10 photo. That flipped BLE + SD (both active) red
  together. Both subsystems were healthy the whole run: BLE received 72,960
  adverts; SD logged continuously and is 1% full. This is the anti-stalking
  feature firing correctly on a genuinely co-moving Find My tag.

Positive-path evidence (interpret before ticking the roadmap):
- The new pipeline escalated a co-moving folded-id (00650a55) to ConfirmedTail
  (flag=4). Because the tag genuinely co-moved with the wearer, this is a real
  positive, not a jitter artifact. BUT: (a) co-movement cannot distinguish the
  wearer's own bag/phone from a stalker (inherent to the method); (b) GPS lock
  flapped hard this run (37 lock drops; long cell=-1 stretches), which degrades
  the cross-cell evidence quality. Do NOT mark the field validation "passed" on
  this single vehicle run; a cleaner walk/drive with stable GPS lock is still
  wanted, and ARGUS_BLE_DETECT_DEBUG stays 1.

Real defect found and FIXED this session (the tracker-domain strobe):
- Symptom: /Settings/threat_log.txt shows the Airtag domain severity strobing
  0<->3 dozens of times, every few seconds, near the end of the run.
- Root cause: detect::ThreatState::report() is authoritative last-wins (rise OR
  fall applied immediately). That is correct for a single-source FLOOD domain, but
  the Airtag/tracker domain is fed one verdict PER ADVERT, PER DEVICE
  (ble_detect_pipeline.cpp:197 -> detect_pipeline_feed_tracker -> feed_tracker ->
  report(Airtag, ...)). A benign device's flag=0 advert overwrote the confirmed
  tag's High to None, and the tag's next advert raised it back -> strobe. The
  intended decay() staircase never got to run.
- Fix: added ThreatState::report_raise() (raise-and-hold; a weaker peer read is
  ignored and does NOT touch the decay anchor, so falling is owned solely by
  decay()/tick()). Switched the four PER-ENTITY feeds to it - Airtag (feed_tracker)
  plus its structural twins RogueAp, Tail, and Surveillance (all fed one verdict
  per observed entity). The single-source FLOOD feeds (DeauthFlood, BleSpam,
  BeaconFlood) keep the authoritative report().
- Files: src/detect/threat_state.h, src/detect/threat_state.cpp,
  src/detect/threat_map.cpp.
- Tests added: test/test_threat_state.cpp (report_raise rises, holds against
  interleaved weaker reads, and still decays once the real threat departs),
  test/test_threat_map.cpp (feed_tracker + feed(RogueFlag) hold through a benign
  interleave). Host suite: 177 tests, 1723 checks, 0 failures.
- NOT yet done: rebuild/flash the watch and confirm the red icons and the
  clock-face AirTag level hold steady (no 0<->3 strobe) on hardware. Domenic has
  not asked for a flash yet. No commit/push made.

Toolchain note (DES-70072 only): the msys2 mingw64 linker (ld.exe / ld.bfd.exe)
was missing - Avast quarantined it (pacman still listed binutils as installed).
Restored both from the signed cached package at
/c/msys64/var/cache/pacman/pkg/mingw-w64-x86_64-binutils-2.45.1-1-any.pkg.tar.zst.
Avast may re-quarantine it; re-restore the same way, or exclude the msys64 bin dir
in Avast. The MSI dev machine is unaffected (no Avast).

CURRENT STATUS UPDATE - 2026-07-25
==================================

This section supersedes the original handoff anywhere the two conflict.

Status:
- The stationary negative hardware test has passed.
- Domenic has now purchased a real Apple AirTag.
- The watch's AirTag scanner detected the real tag and its clock-face count
  increased from 0 to 1. This confirms reception of the strict full-length
  separated/lost-mode AirTag advertisement used by `airtag.cpp`.
- The real-AirTag stationary-plus-movement escalation test is postponed because
  Domenic is not feeling well. Resume on another day; do not pressure him to
  continue the physical test.
- `ARGUS_BLE_DETECT_DEBUG` must remain `1`.
- `ARGUS_BLE_THREAT_PIPELINE` remains `1`.
- Do not mark the roadmap item complete or retire debug yet.
- No commit or push has been made.

Two real detector bugs were found and fixed in the working tree:

1. Permanent Familiar exemption

The detector learned Familiar after five same-cell advertisements and previously
latched it forever. A real BLE tracker advertises repeatedly, so it became
Familiar before the wearer left the starting point and could never escalate.
Familiar is now revoked when the same device is seen in a second distinct cell.
The 2/3/4-cell and 5/10/18-minute thresholds were not changed.

Files:
- `src/detect/tail_detect.h`
- `src/detect/tail_detect.cpp`
- `test/test_tail_detect.cpp`

2. Raw GPS cell-boundary jitter

A stationary tethered hardware session flipped between cells `370647735` and
`1779277793`, falsely creating two-cell evidence. `geo::StableCellTracker` now
holds the accepted cell until the fix moves at least 120 metres from the last
accepted anchor. Real movement beyond 120 metres is still admitted.

Files:
- `src/geo_cell.h`
- `src/geo_cell.cpp`
- `src/ble_detect_pipeline.cpp`
- `test/test_geo_cell.cpp`

Temporary observability was expanded in `src/ble_detect_pipeline.cpp`:
- SD log: `/Settings/bledetect.log`
- Records session, scan attachment, GPS lock/cell transitions, total adverts,
  recognized trackers, accepted trackers, and verdict/cell/minute transitions.
- BLE callback only queues fixed-size events. SD writes remain on the main task.
- `identify_tracker()` is called once so debug can distinguish recognized and
  owner-state-accepted advertisements. The intended gate remains unchanged:
  reject None and OwnerNearby; accept Separated and Unknown.

Host validation:
- 172 tests
- 1669 checks
- 0 failures

Use this test command. Plain `bash` invokes WSL and lacks the required g++:

`C:\msys64\usr\bin\bash.exe -lc 'export PATH=/usr/bin:/mingw64/bin:$PATH; cd /d/Projects/DarkHorse/Firmware/argus-watch; ./test/run.sh'`

Current watch image:
- RAM: 173112 / 327680, 52.8%
- Flash: 2966897 / 3145728, 94.3%
- firmware.bin SHA-256:
  `2BE2FBC9A0E83F4551EAB1714A69FF0F6FBD5B98D6A8200500EF804C1411C2F6`
- firmware.elf SHA-256:
  `1B9C75D6CF71DFB97E8C43C35680CC9B6522A452E2B5CFEDF775E98EEB63808D`
- Successfully flashed to the watch on COM19 with every region hash-verified.
- Domenic confirmed the clock and wallpaper returned.

Controlled lab beacon:
- Domenic does not own an AirTag.
- A Seeed Studio XIAO ESP32-C6 was flashed with the utility under
  `tools/findmy_lab_beacon/`.
- It advertises Find My Network service UUID `0xFD44` as `ARGUS-LAB`, once per
  second at -12 dBm, using a stable board address.
- Serial confirmed `ARGUS-LAB advertising=1`.
- It has no Apple company identifier, offline-finding key, owner identity, or
  location payload. It cannot be claimed or located through Apple Find My.
- It is not an AirTag emulator.
- XIAO serial/MAC: COM3 when last connected; its board MAC is on the device
  (read it with `esptool.py chip_id`) and is not recorded in this repo.
- C6 firmware SHA-256:
  `9AFDA3DAFBA15970210E4B219D1F1484672340C02060E3BA8D9AC871A4DC9857`

Use the isolated C6 package directory for C6 builds only:

`$env:PLATFORMIO_PACKAGES_DIR='<your PlatformIO home>\packages-c6lab'`

`$env:PYTHONUTF8='1'`

Do not set that package directory when building the watch. The watch uses its
pinned Espressif platform 6.10.0 and Arduino 2.0.17 environment.

Completed stationary negative test:
- Wardriving BLE active.
- GPS locked.
- Watch and powered XIAO remained stationary together for more than eight
  minutes.
- Accepted GPS cell remained `370647735`.
- One one-second GPS lock loss recovered to the same cell.
- At t=493: 19,961 total adverts, 550 recognized, 550 accepted.
- Three nearby Find My Network IDs remained `flag=1`, `cells=1`, through a
  480-second span.
- No false Watching, PossibleTail, or ConfirmedTail escalation occurred.
- This validates the GPS hysteresis and stationary Familiar behavior.

Remaining controlled movement test:
1. Put the SD card in the watch.
2. Power the XIAO from a portable battery, vehicle USB, or laptop.
3. Keep the XIAO close to the watch throughout the route.
4. Start Wardriving with BLE and confirm GPS lock.
5. Travel for at least 20 minutes and at least 500 to 800 metres. Around 1 km
   is safer.
6. Stop Wardriving so queued log records flush.
7. Return the SD to the laptop and inspect only the newest session after the
   last `SESSION` line in `G:\Settings\bledetect.log`.

Expected carried-device ladder:
- Familiar: flag 1, one cell
- Watching: flag 2, at least 2 cells and at least 5 minutes
- PossibleTail: flag 3, at least 3 cells and at least 10 minutes
- ConfirmedTail: flag 4, at least 4 cells and at least 18 minutes

Several tracker-like IDs were visible at the house. Identify the carried XIAO
as the stable ID whose distinct-cell count advances throughout the route. Do not
guess its ID from stationary RSSI alone.

The debug SD log is the authoritative ladder evidence. The new BLE pipeline
feeds shared ThreatState, `threat_log.txt`, ARGUS/HADES posture, and HexHound.
The Tracker Timeline screen reads the separate legacy `threat_radar` store, so
its absence is not proof that this new BLE pipeline failed.

Acceptance warning:
- The controlled XIAO route validates the UUID 0xFD44 pipeline mechanics.
- It does not validate a real Apple offline-finding manufacturer payload or
  real AirTag owner/separated behavior.
- A real separated/lost-mode AirTag has now been detected once by the standalone
  AirTag scanner, but its cross-cell tail escalation has not yet been tested.
- Do not claim the real-AirTag field test passed until the stationary and route
  logs have been inspected.

Resume procedure for the real AirTag:
1. Ensure the SD card is in the watch.
2. Boot the watch and enter Defense mode.
3. Turn on only the AirTag tile. Green means the scanner is running.
4. Return to the clock face. The small AirTag disc and count near the bottom
   center is the standalone detector feedback.
5. Pair the AirTag first, then keep its owner iPhone powered off or physically
   away. A nearby owner is intentionally filtered.
6. Confirm the clock-face AirTag count rises above 0.
7. Confirm GPS lock.
8. Keep the real AirTag and watch stationary together for at least five minutes
   so the device reaches Familiar in one cell.
9. Without restarting the watch or toggling the AirTag scanner, carry both for
   at least 20 minutes over roughly 1 km.
10. Note any ARGUS/HADES alert transition but continue the route.
11. Turn the AirTag tile off, return the SD to the laptop, and inspect the newest
    `SESSION` in `/Settings/bledetect.log`.

Wardriving is not required. The AirTag tile is a BLE scan consumer, so the new
tail detector piggybacks on it. The separate `Trackers` tile is for non-Apple
Tile/SmartTag/Chipolo-style devices and is not needed for an AirTag test.

Only after the accepted positive and negative tests:
1. Set `ARGUS_BLE_DETECT_DEBUG` to `0`.
2. Keep `ARGUS_BLE_THREAT_PIPELINE` at `1`.
3. Run the full host suite.
4. Rebuild and verify no `[bledetect]` strings remain in `firmware.elf`.
5. Flash and verify a normal clock/wallpaper boot.
6. Update `tasks/ROADMAP.md` with the actual evidence and whether the accepted
   device was real or controlled.
7. Report the final firmware SHA-256.

Repository: argus-watch  (T-Watch Ultra, ESP32-S3, LVGL 9)
Work with argus-watch as your working directory; all paths below are relative to it.
Build:  pio run -e twatch_ultra   (from the repo root; the pio CLI may not be on PATH)
Flash:  ...pio.exe run -d ... -e twatch_ultra -t upload --upload-port COM19
COM ports: DOWNLOAD mode = 303A:1001 = COM19 (only this flashes; BOOT+RESET to enter). App CDC = 303A:8227 = COM20.
          Serial monitor: open with DTR=False/RTS=False (see tasks/FLASHING-NOTES.md) or the S3 drops to download mode.
GIT: branch darkhorse-argus, HEAD ~ 6e9b14b (reliability + fonts all committed; tree clean but for untracked tasks/*-HANDOFF.md).
     Do NOT commit or push unless Domenic explicitly asks. Preserve the untracked handoff docs. No Co-Authored-By, no em dashes.

READ FIRST: tasks/ROADMAP.md (the ARGUS_BLE_DETECT_DEBUG item + the "identity guardrail"), tasks/FLASHING-NOTES.md,
src/detect/tail_detect.h (the detection contract), src/detect/README.md.


>>> THE TASK <<<
Validate that ARGUS's BLE unwanted-tracker "tail/follow" detection actually escalates on hardware with a REAL tracker
doing a REAL follow, THEN retire the bench-debug logging. This is the LAST ROADMAP item before public release, and it is
NOT "flip a flag" - the flag is only observability. The point is to prove the headline DEFENSIVE capability works before
you silence the window you'd use to see it working. If it does not escalate correctly, that is a real bug to REPORT (do
not turn debug off and do not ship a broken anti-stalking feature).

This is a FIELD test Domenic must physically run (it needs real movement through real locations with a real AirTag).
Your job: prep the observability build, tell Domenic exactly what route/steps to run and what you need logged, interpret
the serial + on-watch results, then make the keep-or-fix-or-retire call.


>>> WHAT THE FLAG ACTUALLY DOES <<<
src/ble_detect_pipeline.cpp:17  #define ARGUS_BLE_DETECT_DEBUG 1
  :18-21  gates BLD_LOG(...) -> Serial.printf (1) or a no-op (0). It is PURE LOGGING. Turning it 0 changes NO detection
  behavior; it only silences the [bledetect] serial lines. So retiring it is safe - but only AFTER the logs have shown
  you the escalation works.
DO NOT touch: src/main.cpp:105 #define ARGUS_BLE_THREAT_PIPELINE 1 - that is the PRODUCTION pipeline enable, keep it 1.


>>> HOW DETECTION WORKS (ground truth, verified 2026-07-25) <<<
Flow (src/ble_detect_pipeline.cpp): each BLE advert -> detect::is_unwanted_tracker() GATE (identifies Apple Find My /
AirTag via the offline-finding manufacturer payload, src/detect/tracker_ident.cpp) -> build a DeviceSighting {addr-fold
key, t_sec, cell_id} -> s_tracker_tail.ingest() (src/detect/tail_detect.cpp) -> TailVerdict{flag, distinct_cells, span}
-> detect_pipeline_feed_tracker(flag) (src/detect_pipeline.cpp) -> shared ThreatState -> hexhound_set_threat_level() +
the Tracker Timeline UI (src/tracker_timeline_screen.cpp).

Escalation ladder (enum TailFlag, src/detect/tail_detect.h:52):
  0 None          brand-new / single sighting / not enough evidence
  1 Familiar      learned BENIGN: repeatedly seen in ONE cell (your home/work router-equivalent) - must NOT alarm
  2 Watching      early cross-cell evidence (>= 2 distinct location cells over time)
  3 PossibleTail  sustained cross-cell co-movement (>= 3 cells)
  4 ConfirmedTail long, wide cross-cell co-movement (>= 4 cells)
Thresholds (tail_detect.h): distinct LOCATION CELLS *and* a time span, BOTH axes required together (the standard
anti-stalking 2/3/4-waypoint + 5/10/18-minute model, with coarse integer cell_id standing in for GPS waypoints).
  *** CRITICAL: cell_id needs a GPS FIX. cell_id = -1 means no fix; then only time/recency accrue and the device
  CANNOT climb the cross-cell ladder. So the test REQUIRES GPS lock (boot_radios gps=1) and REAL movement between
  distinct places - a stationary bench test with one AirTag will never reach PossibleTail/ConfirmedTail by design. ***
Only escalations are logged (to avoid spam): BLD_LOG at :78
  [bledetect] tracker follow flag=%u cells=%u span=%us cell=%ld


>>> THE FIELD TEST (Domenic runs it; you direct + interpret) <<<
Setup:
  - A real Apple AirTag (or other Find My tracker) that is SEPARATED from its owner (so it advertises offline-finding /
    lost mode) - carry it WITH the watch. Debug build (ARGUS_BLE_DETECT_DEBUG stays 1 for the test).
  - GPS locked (gps=1). Confirm a fix before starting (cells depend on it).
  - Serial monitor on COM20 (DTR/RTS false) capturing [bledetect] lines, OR watch the Tracker Timeline screen on-watch.
Positive path (a real tail should escalate):
  - Carry watch + AirTag together and MOVE through several distinct locations over time (walk/drive a route hitting
    >= 4 distinct cells across the span). Watch the flag climb None -> Watching(2) -> PossibleTail(3) -> ConfirmedTail(4)
    as distinct_cells rises, and confirm the Tracker Timeline UI + threat level respond.
Negative path (no false alarm):
  - A tracker that stays in ONE cell (sitting at home/desk) should settle to Familiar(1) and NOT escalate. Verify a
    benign single-location tracker does not raise an alarm.
PASS = the positive path reaches PossibleTail/ConfirmedTail on real cross-cell co-movement AND the negative path stays
None/Familiar, with the UI + threat level reflecting both. Capture the [bledetect] log (flag/cells/span progression).


>>> AFTER A PASS: RETIRE THE DEBUG LOGGING <<<
  - Set src/ble_detect_pipeline.cpp:17 ARGUS_BLE_DETECT_DEBUG 1 -> 0. Leave ARGUS_BLE_THREAT_PIPELINE=1.
  - Rebuild; confirm the ELF has no "[bledetect]" strings (grep strings on firmware.elf). Flash, quick sanity boot.
  - Update tasks/ROADMAP.md to tick the item with the observed escalation evidence. Report final firmware SHA-256.


>>> IF IT DOES NOT ESCALATE (real bug - do NOT flip the flag) <<<
Report the observed logs and diagnose, do not guess or silence it:
  - Was the AirTag even GATED as a tracker? (is_unwanted_tracker returning false -> no sighting; check tracker_ident.)
  - Did distinct_cells rise? If it stayed low with a real route, cell_id may be stuck at -1 (no GPS fix) or the cell
    bucketing is too coarse/fine (tail_detect cell derivation).
  - Did span accrue but cells not (or vice-versa)? Both axes are required.
Fix the pipeline, re-run the field test. A broken anti-stalking detector must not ship as a headline feature.


>>> GUARDRAILS <<<
- DEFENSIVE ONLY. tail_detect/tracker_ident never transmit, deauth, track, or attack - it only INGESTS sightings.
  Keep it that way (see the ROADMAP "identity guardrail"): this is anti-surveillance defense, not an offensive tool.
- Do NOT change the escalation thresholds (2/3/4 cells, span) without field evidence that they mis-fire - they encode a
  known anti-stalking standard. Measure first.
- Keep ARGUS_BLE_THREAT_PIPELINE=1. Only ARGUS_BLE_DETECT_DEBUG is the throwaway.
- The test needs GPS + real movement; a stationary bench test cannot reach the cross-cell tail levels by design - do not
  "fix" that by weakening the thresholds.
- Do NOT commit or push unless Domenic explicitly asks. Preserve the worktree and untracked tasks/ handoff docs.
