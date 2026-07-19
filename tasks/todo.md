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

## Known transients (self-recovered, not chased)
- First-boot matrix / stale-artifact rendering glitches — did not reproduce.
- One first-boot restart loop at the clock — settled on its own next power-up.

## Notes
- Current D:\...\Firmware\argus repo kept as architecture/test REFERENCE.
- Flash workflow + host-test toolchain gotchas: see session memory.
