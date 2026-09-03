# ARGUS — Overnight Session Review

Branch `argus-argus`, LOCAL only. Final firmware (theming build) is flashed.
Final commit: `3b5778e`. Last USER-CONFIRMED-GOOD build: `a0eb15e` (the merge).

## >>> FIRST THING IN THE MORNING <<<
**Press the crown/power button to boot the watch.** After a USB flash the chip sits
in its ROM state (enumerates as 303A:1001) until a physical power-cycle; the running
app enumerates as 303A:8227. This is normal — it was true after every flash this
session (you power-cycled each time without noticing). So: press the crown, and it
should boot the ARGUS splash → themed clock.

If it boots and looks right → great, we're done for this batch.
If it's blank / boot-loops / looks broken → hold BOOT + click RST (download mode) and
tell me; I'll reflash the last-confirmed-good build `a0eb15e` in seconds.

## What ARGUS is now
ARGUS-branded fork of r3dfish/13-37 (full watch + all RF tools + Meshtastic +
wardriver + detectors) PLUS: ARGUS name, Bank Gothic boot splash + titles, steel-blue
#9BBCD6 theme with ARGUS→HADES threat flip, HexHound recon pet (5 stages,
replaced the pwnpet goldfish), Threat Radar anti-stalking, passive handshake capture,
and a host unit-test harness (mesh crypto vs FIPS/NIST, `bash test/run.sh` = 20/20).

## Done + committed this session (builds clean, Flash ~85.4%)
1. **Full theming pass**: 27 screen titles → steel-blue + new full-alphabet Bank Gothic
   UI font (`src/font_argus_ui.c`). Semantic colors (battery, running-green, threat-red,
   error-red, Meshtastic purple) intact. This is the flashed build.
2. Host tests still 20/20.

## VERIFY once booted (I could not see the display overnight)
- [ ] Boots cleanly to ARGUS/ARGUS splash → clock.
- [ ] **Titles**: Bank Gothic steel-blue, legible, NOT clipped. Check the longest:
      CONFIGURATION, SEND MESSAGE, THREAT RADAR, MESHTASTIC. If any clips on the right,
      tell me — I drop `font_argus_ui` from 32px to ~26px (one-line regen).
- [ ] HexHound / Radar / Pwn tiles still open + work.

## Reverted / deferred (NOT in the flashed build)
- **Time/date fix — REVERTED.** My build-time RTC seed, placed too early (right after
  instance.begin(), before USB/LVGL init), hung the boot before the app came up. Backed
  it out (commit 3b5778e). The clock is therefore still on its default/unset time until
  a GPS fix (take it outside) or Settings → manual date/time. Re-doing the seed AFTER
  full init + on-device verification is a supervised follow-up.
- **Mesh crypto wiring (Task #6/#12) — deferred by safety decision.** Security-critical
  RX; can't verify decryption overnight. Tested `src/mesh` module stands ready.

## Remaining polish / roadmap (your call)
- ~12 minor decorative non-title accents still generic; a few dynamic/banner titles not
  Bank-Gothic'd. Low impact.
- Phase 4 red/blue roadmap: NOT started. Offensive TX (deauth/jam) deliberately excluded
  (FCC line). `stealth` module: not ported.

## Honest summary
Delivered safely: the full ARGUS/HADES **color + Bank Gothic title theming** (your
main ask), all committed and flashed. The **time fix I attempted broke the boot, so I
reverted it** rather than leave a bricked-feeling watch — it needs a supervised re-do.
Mesh-crypto wiring stayed deferred for safety. Nothing destructive; the last known-good
build is one reflash away if the themed build misbehaves.
