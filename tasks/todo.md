# DarkHorse ARGUS Watch — Fork Plan

Base: fork of `r3dfish/13-37` (upstream remote), branch `darkhorse-argus`.
Goal: take the T-Watch Ultra to the next level for cybersecurity red/blue team,
while keeping it a full watch (clock/alarms/timer/calendar). Bring ARGUS's
engineering rigor (testable modules + host unit tests) and DarkHorse/HADES
branding to the proven 13-37 base; cherry-pick Threat Radar (MIT) features.

## Phase 0 — Baseline (prove it flashes + renders) — IN PROGRESS
- [x] Clone r3dfish/13-37 -> `D:\Projects\DarkHorse\Firmware\argus-watch`, remote `upstream`, branch `darkhorse-argus`
- [x] **Reproducible-build fix (was a blocker):** LilyGoLib #b3df890, ST25R3916-fork #0c8e00f,
      NFC-RFAL-fork #7bde458 could not be re-fetched (GitHub won't shallow-fetch a bare
      SHA; PlatformIO re-validates git deps every build). Vendored all three into `lib/`
      at their exact commits (via `git archive`), baked the two LilyGoLib patches
      (SEND_BUF_SIZE 16384->4096, LV_USE_SNAPSHOT 0->1) permanently into the vendored
      source, dropped the git deps + retired `scripts/patch_lilygolib.py` from `platformio.ini`.
      Clean `.pio` build now succeeds: Flash 84.0% (2.64 MB), RAM 56.5%.
- [x] Flash baseline to watch (COM19, hash verified)
- [ ] **GATE: user confirms 13-37 clock boots + display upright/readable** <-- awaiting
- [ ] Commit baseline + vendored deps on `darkhorse-argus` (after gate passes)

## Phase 1 — Rebrand to DarkHorse ARGUS
- [ ] Boot splash -> DarkHorse horse-head (reuse argus `boot_logo.h`)
- [ ] Theme -> DarkHorse steel-blue / HADES threat-red; couple to detector state
- [ ] Naming / watch-face accent
- [ ] Verify on hardware

## Phase 2 — Engineering rigor (ARGUS's real edge)
- [ ] Stand up host unit-test harness (port argus `test/` CMake pattern)
- [ ] Extract security-critical logic into pure tested modules:
      Meshtastic crypto/packet decode, MAC dedup, tail-detection scoring
- [ ] CI-style `run tests` before each subsequent change

## Phase 3 — Cherry-pick Threat Radar (ciccirix/13-37-threat-radar, MIT)
- [ ] Spatio-temporal tail detection (GPS+RF correlation, Possible/Likely/Confirmed)
- [ ] Tail classification (tracker vs co-moving vehicle), familiarity learning
- [ ] Mesh reputation (hashed-MAC broadcast), pwnpet, spectrum analyzers
- [ ] Each hardened + unit-tested as it lands

## Phase 4 — Net-new red/blue roadmap
- [ ] Blue-team first: detection, forensic logging, counter-surveillance
- [ ] Red-team: authorized-testing only. NO offensive TX (WiFi deauth / LoRa jam)
      as one-button ship features — FCC bright line.

## Notes
- Current `D:\...\Firmware\argus` repo kept as architecture/test REFERENCE, not a parallel product.
- Flash workflow: see memory `flashing-twatch-ultra` (USB re-enum download-mode dance).
