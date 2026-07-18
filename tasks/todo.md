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

## Phase 1 — Rebrand to DarkHorse ARGUS — DONE (commit 69ce59b), build-verified
- [x] Naming: FW_NAME ARGUS, Meshtastic names, matrix eggs (kept 1337 homage)
- [x] src/theme.h; swapped matrix-green accent -> steel-blue at ~67 sites / 26 files
- [x] Boot splash text -> steel-blue "ARGUS"
- [ ] VISUAL CONFIRMATION ON HARDWARE PENDING (built OK; not yet flashed — USB was
      intermittent + user AFK. Flash `.pio/build/twatch_ultra/firmware.bin` and eyeball.)
- [ ] Optional polish: horse-head boot logo via setBootImage() raw-RGB565 (argus boot_logo.h)

## Phase 2 — Engineering rigor — DONE (commit f22efd5), tests PASS
- [x] Host test harness in test/ (g++, no cmake): wl_test.h + test_main.cpp + Makefile + README
- [x] Ported pure mesh-crypto module src/mesh/{aes,crypto}.* + test_mesh_crypto.cpp
- [x] `make -C test test` -> 6 tests / 20 checks pass (AES FIPS-197, CTR NIST SP800-38A, nonce)
- [x] Firmware still builds clean with the module added (Flash 84.0%)
- [ ] (Task #6) Wire src/mesh into src/meshtastic.cpp so the DEVICE path uses tested
      crypto — DEFERRED: needs mesh RX verified on hardware; do not land unattended.

## Phase 3 — Cherry-pick Threat Radar (ciccirix/13-37-threat-radar, MIT) — TODO
- [ ] Spatio-temporal tail detection, tail classification, familiarity learning
- [ ] Mesh reputation (hashed-MAC broadcast), pwnpet, spectrum analyzers
- [ ] Each hardened + unit-tested as it lands (extend test/)

## Phase 4 — Net-new red/blue roadmap — TODO
- [ ] Blue-team first: detection, forensic logging, counter-surveillance
- [ ] Red-team: authorized-testing only. NO offensive TX (WiFi deauth / LoRa jam)
      as one-button ship features — FCC bright line.

## Next candidates to extract + test (from scout map)
- Evil-Twin decision logic (stateful rogue-AP detection)
- Shared BLE AD-record parser (de-risks AirTag/Flipper/Skimmer at once)

## Notes
- Current D:\...\Firmware\argus repo kept as architecture/test REFERENCE.
- Flash workflow + host-test toolchain gotchas: see session memory.
