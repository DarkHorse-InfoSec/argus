# DarkHorse ARGUS — Overnight Session Review

Prepared for morning review. Branch `darkhorse-argus`, local only (no push).
Final commit: see `git log --oneline`. Final firmware flashed to the watch.

## What ARGUS is now
A DarkHorse-branded fork of r3dfish/13-37 (full T-Watch Ultra: clock, alarms,
timer, calendar, all RF tools, Meshtastic, wardriver, detectors) PLUS:
- DarkHorse identity: ARGUS name, Bank Gothic boot splash + titles, steel-blue
  #9BBCD6 theme, brand-as-threat-state (DarkHorse -> HADES red flip).
- HexHound: DarkHorse's own 5-stage cyber-recon pet (replaces the pwnpet goldfish).
- Threat Radar: anti-stalking GPS+RF tail detection + mesh reputation + Radar screen.
- Passive WPA handshake capture (Pwn tile).
- Engineering rigor: vendored reproducible deps + host unit tests (mesh crypto
  vs FIPS-197 / NIST SP800-38A vectors, 20 checks passing via `bash test/run.sh`).

## Done this session (all committed, builds clean at Flash ~85.4%)
1. **Full theming pass**: 27 screen titles re-colored to steel-blue AND switched to
   the new full-alphabet Bank Gothic UI font (`src/font_dh_ui.c`). Semantic colors
   (battery, running-green, threat-red, error-red, Meshtastic purple) left intact.
2. **Time/date**: RTC now seeds from firmware build time (local wall-clock) when it
   comes up unset, so the clock reads ~right without GPS/NTP.
3. Verified: `pio run` SUCCESS, host tests 20/20.

## PLEASE VERIFY ON THE WATCH (things I could not check while you slept)
- [ ] **Boots cleanly** (no restart loop). SD is FAT32/ARGUS = fine.
- [ ] **Titles**: are the Bank Gothic steel-blue titles legible and NOT clipped?
      Longest ones to check: CONFIGURATION, SEND MESSAGE, THREAT RADAR, MESHTASTIC.
      If any clips on the right, tell me and I'll drop `font_dh_ui` from 32px to ~26px
      (one-line regen). Titles are all ~33px cap-height now (were mixed 28/48px).
- [ ] **Time**: should read approximately correct local time now. For exact time:
      take it outside for a GPS fix (auto-sets timezone), or Settings -> manual
      date/time. Timezone default currently relies on GPS longitude detection.
- [ ] **HexHound** (Tools -> HexHound), **Radar** (Tools -> Radar), **Pwn** capture
      still work as before.

## Deferred BY DECISION (need supervised hardware verification — not done)
- **Task #6 / #12 — wire tested mesh crypto into meshtastic.cpp.** Security-critical
  RX decryption; a subtly-wrong nonce/key wiring silently breaks mesh with no way to
  verify overnight. The tested `src/mesh` module stands ready. Do this with you present
  + real mesh traffic. (Safety-first override of "do everything.")

## Remaining polish / roadmap (NOT done — for your call)
- ~12 minor decorative UI accents (non-title blues/cyans) still generic; a few
  dynamic/banner titles (portscan/send-message dynamic titles, TIME'S UP / ALARM ring
  banners) not yet Bank-Gothic'd. Low impact.
- Category-4 body/value white text left white for legibility (optional to brand).
- Phase 4 red/blue roadmap: NOT started. Offensive TX (WiFi deauth / LoRa jam) is a
  hard FCC line and deliberately excluded.
- `stealth` (shake-to-disguise) module from the fork: not ported (main.cpp/clock
  conflict-prone; do supervised).

## Recovery note
If the watch ever boot-loops: hold BOOT + click RST (download mode), then reflash.
The SD being FAT32 fixed the earlier crash-loop.
