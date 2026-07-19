# DarkHorse ARGUS Watch — Fork Plan

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
- [ ] BLE-spam / adv-flood detector: pure src/detect/ble_spam.* (composes with
      src/ble adv parser; Flipper/BLE-spam signature) + host tests. IN PROGRESS.
- [ ] Threat-state aggregator: pure module combining all detector verdicts into
      one posture (calm/watch/alert/critical) to drive the HADES-red accent +
      HexHound. NEXT — this is the glue that makes integration turnkey.

Approach (per feedback [[feedback-keep-building-when-afk]]): keep producing
host-tested/additive work while AFK; do NOT idle waiting on integration. No
experimental flashing without the user present.

## ON YOUR RETURN — briefing
1. FLASH once and verify boot + features (pending on-device changes bundled):
   - Wallpaper OOM guard (9205bc7) + auto-create /backgrounds (6005ef9). The
     /backgrounds folder + README should appear on the card; a normal image
     loads faint; a deliberately huge multi-MP photo is SKIPPED with a
     "[background] skipping oversized wallpaper" serial line, NOT a crash.
     NOTE: 6005ef9 adds SD writes on the BOOT path — watch it boot clean.
   - Clock ghost fix (d31e7a8) already flashed + you confirmed it.
2. INTEGRATION (each a separate, hardware-gated step — wire one, flash, verify
   before the next; do NOT batch-wire blind). All modules map onto existing
   on-device types; see each header's notes:
   - BLE adv parser (src/ble) -> refactor airtag/flipper/skimmer detectors.
   - Evil-twin (src/detect/evil_twin) -> feed from wifi_beacon_manager.
   - Tail-detect (src/detect/tail_detect) -> feed BLE/wifi sightings + GNSS cell.
   - Deauth-flood (src/detect/deauth_flood) -> feed the promiscuous mgmt-frame
     path in wifi_beacon_manager (subtype 0xC/0xA, addr3).
   - BLE-spam (src/detect/ble_spam) -> feed from ble_scan_manager adv results.
   - Threat aggregator -> consumes the above, drives argus_accent() + HexHound.

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
