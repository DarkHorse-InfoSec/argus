# DarkHorse ARGUS Watch — Fork Plan

## >>> RETURN BRIEFING (2026-07-19 late session) <<<
The watch is RUNNING the recovered build (ghost fixed, WiFi good, adaptive-font
clock). Committed since that flash but NOT yet flashed: clock-slow DIAG
instrumentation + smaller AM/PM. To pick them up: from stable download mode
(hold BOOT, tap RST, hold BOOT ~2s) I reflash the current build, you power-cycle.

1. CLOCK TICKS BY 3-4s (only with SD inserted; fine without it). Not the wardriver
   (30s cadence) nor USB-MSC (hidden). Instrumented (commit): with a card in,
   read serial for a "[slow] <call> Nms" line - it names the exact blocking call,
   then I fix it and remove the DIAG. THIS is the first thing to do on return.
2. AM/PM is now a smaller font (separate span). Verify it looks right.
3. BACKGROUNDS: resized 410x502 PNGs are in
   C:\Users\dlaur\Downloads\argus-backgrounds-410x502\ (DarkHorse/HADES/Privacy).
   Copy these into the SD /backgrounds and DELETE the 1242x1242 originals (too big).
   Stock-baked-into-firmware deferred (flash 86.4%; do later as a compressed PNG).
4. BLUETOOTH toggle: use BT-FIRST (toggle BT on before WiFi). Auto BLE keepalive
   boot-loops (see [[ble-keepalive-boot-loops]]); proper async fix = fresh session.
5. Boot-loop lesson: NEVER flash while the watch is mid-boot-loop (incomplete
   flash). Force stable download mode (BOOT+RST) first, then flash.

---


Base: fork of `r3dfish/13-37` (upstream remote), branch `darkhorse-argus`, LOCAL ONLY (no push yet).
Goal: take the T-Watch Ultra to the next level for cybersecurity red/blue team,
while keeping it a full watch (clock/alarms/timer/calendar). Bring ARGUS's
engineering rigor (testable modules + host unit tests) and DarkHorse/HADES
branding to the proven 13-37 base; cherry-pick Threat Radar (MIT) features.

## Phase 0 — Baseline — DONE (commit 8afd220)
- [x] Clone r3dfish/13-37, remote `upstream`, branch `darkhorse-argus`
- [x] Reproducible-build fix: vendored LilyGoLib/ST25R3916/NFC-RFAL into lib/ at
      exact commits; baked in the two LilyGoLib patches; retired patch_lilygolib.py
- [x] Baseline flashed + confirmed on hardware (13-37 clock, upright/readable)
- [x] Committed baseline

## Phase 1 — Rebrand to DarkHorse ARGUS — DONE, HARDWARE-CONFIRMED
- [x] Naming: FW_NAME ARGUS, Meshtastic names, matrix eggs (kept 1337 homage)
- [x] src/theme.h; ARGUS_ACCENT steel-blue #9BBCD6 + HADES_RED #DB615A runtime flip
- [x] Bank Gothic brand fonts (subset ARGUS/DARKHORSE + full-alphabet font_dh_ui 32px)
- [x] 27 screen titles themed steel-blue; boot splash horse-head silhouette + wordmark
- [x] VISUAL CONFIRMATION ON HARDWARE — flashed + eyeballed, upright/readable/on-brand

## Phase 2 — Engineering rigor — DONE, tests PASS
- [x] Host test harness in test/ (g++, no cmake): wl_test.h + test_main.cpp + Makefile
- [x] Pure mesh-crypto module src/mesh/{aes,crypto}.* + test_mesh_crypto.cpp
- [x] `bash test/run.sh` -> AES FIPS-197 / CTR NIST SP800-38A / nonce all pass
- [x] (Task #6) DONE — decrypt_ctr_n in src/meshtastic.cpp now calls
      wl::mesh::aes_ctr_xcrypt (byte-identical to the old mbedTLS path; include dropped)

## Phase 3 — Threat Radar + pet + feeds — DONE (ported, DarkHorse-branded)
- [x] HexHound pet replaces borrowed pwnpet (DarkHorse's own creature; layout tuned)
- [x] HexHound feeds from BLE / detectors / NFC / GNSS cells, not just WiFi
- [x] Threat Radar rings + accent flips to HADES red under threat
- [ ] Remaining Threat Radar extras (spatio-temporal tail classify, familiarity
      learning, spectrum analyzers) — extract + host-test as each lands

## Team features (Jul 18-19) — DONE + flashed
- [x] GPS power persists across reboot (/Settings/gps.txt, restored in setup)
- [x] SD-card wallpaper for the clock face (user/kid-uploadable images)
- [x] Bluetooth toggle: interim fix — radio screens ordered Bluetooth -> WiFi so
      BLE comes up before WiFi (coexistence-safe). Proper fix (boot keep-alive at a
      SAFE init point) documented in ble_scan_manager, deferred — crashed when early.

## Phase 4 — Net-new red/blue roadmap — TODO
- [ ] Blue-team first: detection, forensic logging, counter-surveillance
- [ ] Red-team: authorized-testing only. NO offensive TX (WiFi deauth / LoRa jam)
      as one-button ship features — FCC bright line.

## Next candidates to extract + test (host-testable, no hardware risk)
- Shared BLE AD-record parser (de-risks AirTag/Flipper/Skimmer at once)
- Evil-Twin decision logic (stateful rogue-AP detection)

## Rendering — clock stale-pixel ghosting — FIXED + flashed (commit d31e7a8)
- Symptom: clock/date drawn multiple times (ghosted), plus stale Settings text
  left in the lower half. Photographed with matrix OFF.
- Root cause: the digital clock has a transform_scale (up to 1.5x, auto-fit).
  Under LVGL partial-refresh a scaled label draws past the area a label-only
  invalidate clears, so each second's redraw left the enlarged glyph rim behind.
  The matrix rain masked it by repainting the background 8x/sec.
- Fix: update_clock() now invalidates the whole clock_screen each tick (1 Hz
  full repaint, trivial on this panel). USER-CONFIRMED FIXED with matrix off.
- Holds with matrix ON (more repainting) and with wallpaper (composited fresh
  each tick). Wallpaper OOM risk for oversized images is being hardened (below).

## Autonomous session 2026-07-19 (user AFK) — host-testable / additive only
RULE: no experimental hardware flashing without the user present to confirm boot
(two boot loops earlier). Work below is build + host-test + commit only; flashing
is queued for the user's return.
- [x] Shared BLE advertisement parser src/ble/ + 17 host tests (commit 0c077a0)
- [x] Wallpaper oversized-image guard: pure src/image_dims.* header probe (PNG/
      BMP/JPEG) + 14 host tests + background.cpp skips images over a 1.2M-px
      budget (never OOM). Firmware build verified. (commit 9205bc7)
- [x] Evil-twin / rogue-AP decision logic: pure src/detect/evil_twin.* + 12 host
      tests. Firmware build verified. (commit ccc4eb4)
- [x] Tail-detection / anti-stalking classifier: pure src/detect/tail_detect.*
      (familiarity learning + cross-cell escalation + decay) + 13 host tests.
      Firmware build verified. (commit 50dfaa4)
- [x] Auto-create /backgrounds folder + README on SD so the wallpaper drop-in
      folder is discoverable (was: "no backgrounds folder"). (commit 6005ef9)
- [x] Deauth/disassoc flood detector: pure src/detect/deauth_flood.* (per-BSSID
      + global sliding window) + 9 host tests. Build verified. (commit 57d7bd5)
- [x] BLE-spam / adv-flood detector: pure src/detect/ble_spam.* (composes with
      src/ble adv parser) + 9 host tests. Build verified. (commit dbf5be2)
- [x] Threat-state aggregator: pure src/detect/threat_state.* - unified posture
      (Calm/Watch/Alert/Critical) with rise/decay hysteresis + correlation
      escalation + 14 host tests. Build verified. (commit 0186624)
- [x] Detector->aggregator mapping layer: src/detect/threat_map.* (severity_of +
      feed() per detector) + 8 host tests. Build verified. (commit a4dc0a2)
- [x] Subsystem architecture + integration guide: src/detect/README.md.

DETECTION SUBSYSTEM COMPLETE: parser -> 5 detectors -> map -> aggregator, all
pure/host-tested/decoupled and compiling into the firmware. Host suite: 102
tests / 944 checks green. See src/detect/README.md for the data flow + the
step-by-step (hardware-gated) integration guide.

Approach (per feedback [[feedback-keep-building-when-afk]]): keep producing
host-tested/additive work while AFK; do NOT idle waiting on integration. No
experimental flashing without the user present.
- [x] WiFi beacon-flood / fake-AP-spam detector: src/detect/beacon_flood.* + 9
      tests, wired into aggregator as ThreatDomain::BeaconFlood. (570afd3, 4c8205a)
- [x] Coarse GPS-to-cell quantizer src/geo_cell.* (~120 m, configurable) so
      tail_detect has real cell ids + 8 tests. (commit 0d60d4d)
- [x] Unwanted-tracker (AirTag / Find My) ident src/detect/tracker_ident.* +
      Airtag wiring (feed_tracker -> ThreatDomain::Airtag). (commit 669c8ca)
- [x] Forensic threat log src/detect/threat_log.* (edge-recorded, 48-event ring,
      SD-append documented) - Phase 4 blue-team. (commit c12ffb8)

## Rendering - display FULL-refresh (commit 7c5a85b) - FLASHED + user-confirmed
Flipped LilyGoLib LilyGo_Display(full_refresh=true) (Ultra already has full-screen
PSRAM buffers, so zero extra memory). Kills the whole partial-refresh artifact
class: clock jump under Matrix AND wallpaper, and Tools-scroll stale rows. Verified
on-device: Matrix+clock stable, Tools smooth, wallpaper smooth.

## Radio persistence (commit 602dc1c) - built, PENDING FLASH CHECK
WiFi/NFC/LoRa power state now persists across reboot (GPS pattern, /Settings/*.txt).
Bluetooth deliberately EXCLUDED (BLE-at-boot boot-looped). Verify: toggle each on ->
reboot -> should return AND boot clean (LoRa brings the SX1262 fully live at boot).

## Full module inventory (pure, host-tested; parser/detectors UNWIRED to scan loop)
  src/ble/adv_parser  BLE AD parser | src/image_dims  wallpaper guard | src/geo_cell  GPS->cell
  detect/: evil_twin->RogueAp  tail_detect->Tail  deauth_flood->DeauthFlood
           ble_spam->BleSpam  beacon_flood->BeaconFlood  tracker_ident->Airtag
           threat_map (verdict->Severity)  threat_state (->ThreatLevel)  threat_log (history)
  test/test_subsystem_e2e  end-to-end composition proof (real inputs through the chain)
Host suite: ~142 tests / 1541 checks green. See src/detect/README.md for data flow
+ the step-by-step integration guide. The e2e test already caught 2 real cross-
module bugs (log all-domain polling; geo_cell must be non-negative) - extend it
BEFORE wiring hardware integration; cheaper than a flash cycle.

## ON YOUR RETURN — briefing
1. FLASH once + verify boot (bundled pending on-device changes):
   - Radio persistence (602dc1c): toggle WiFi/NFC/LoRa on -> reboot -> return + clean boot.
   NOTE: the watch currently runs the full-refresh build (7c5a85b, flashed +
   confirmed) but NOT yet radio persistence / the latest modules - reflash to get them.
   - Already flashed + confirmed this session: full-refresh (7c5a85b), wallpaper OOM
     guard (9205bc7) + auto-/backgrounds (6005ef9), clock fix (d31e7a8).
2. INTEGRATION (hardware-gated; wire one, flash, verify, then next - do NOT batch blind):
   - BLE adv parser -> refactor airtag/flipper/skimmer detectors.
   - evil_twin + beacon_flood -> feed from wifi_beacon_manager (beacon path).
   - deauth_flood -> promiscuous mgmt-frame path (subtype 0xC/0xA, addr3).
   - ble_spam + tracker_ident -> feed from ble_scan_manager adv results.
   - tail_detect -> BLE/wifi sightings keyed by geo::coarse_cell(GNSS); tracker_ident
     hits feed a 2nd TailDetector -> feed_tracker() (Airtag domain).
   - threat_map::feed*() -> threat_state; drive argus_accent()+HexHound; threat_log
     records escalations to /Settings/threat_log.txt.
3. Open feature question: phone notifications over BLE (iPhone/ANCS first). Discussed;
   sequence after integration since it leans on the BLE stack.

## Session commits (darkhorse-argus, LOCAL only, none pushed)
- d31e7a8 fix(clock) stale-pixel ghosting  [FLASHED + user-confirmed]
- 0c077a0 feat(ble) advertisement parser + tests
- 9205bc7 feat(background) oversized-image OOM guard + tests  [PENDING FLASH]
- ccc4eb4 feat(detect) evil-twin / rogue-AP logic + tests
- 50dfaa4 feat(detect) tail-detection / anti-stalking + tests

## PENDING HARDWARE FLASH (needs user present to verify boot)
- Wallpaper oversized-image guard (commit 9205bc7): flash once, then drop a
  normal image (loads faint) and a deliberately huge one (should be skipped,
  logged on serial, NOT crash). This is the only pending-flash item so far.
- Later pure modules (evil-twin etc.) are host-tested and compile into the
  build but are not wired into the UI/scan yet, so they change no on-device
  behavior; their integration is a separate hardware-gated step.

## Known transients (self-recovered, not chased)
- First-boot matrix / stale-artifact rendering glitches — did not reproduce.
- One first-boot restart loop at the clock — settled on its own next power-up.

## Notes
- Current D:\...\Firmware\argus repo kept as architecture/test REFERENCE.
- Flash workflow + host-test toolchain gotchas: see session memory.
