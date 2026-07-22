# ARGUS Offense Unlock Path - Implementation Plan

Goal: make **Offense** mode actually reachable, and give each mode its own look. Turns the already-built `argus_mode` machine + tile/gesture gating into a usable, opsec-safe offensive layer.

Target: T-Watch Ultra, env `twatch_ultra`, LVGL 9.5. See MODE-ARCHITECTURE-PLAN.md sections 0/0a/P3/P4/P5 for the original design; this plan supersedes those with the CURRENT code state and the locked decisions.

---

## Already built (foundations this plan stands on)
- `argus_mode.{h,cpp}` - state machine. `enter_offense()` (RAM-only, blocked when `offense_locked_out()`), `lock_offense()`, `offense_shred()` (burns NVS `lockout` FIRST, then calls a registered wipe hook), `argus_mode_on_change(cb)`, Defense-persistence setting.
- Tile + gesture gating - `tools_apply_mode()` shows offensive tiles (`handshake`/Pwn, `mouse`, `tesla`) only in Offense; registered on `argus_mode_on_change`, so **`enter_offense()` already auto-reveals the offensive tiles**. Clock gestures gated in Daily.
- **BOOT input layer** (`main.cpp` loop) - polled duration state machine: SHORT = back/Settings, LONG (>=600ms) = home. The knock (Phase B) EXTENDS this; it is not a rewrite.

## Locked decisions (from Domenic)
- Knock is on the **BOOT button (GPIO0)**, pattern **long-short-long** -> PIN pad.
- **Two PINs:** unlock reveals Offense; a SEPARATE shred-PIN (>= unlock length + 1, distinct, no shared prefix) wipes offensive data + burns lockout. ONLY the exact shred-PIN shreds; any other wrong PIN just fails + rate-limits.
- **No flash encryption / secure boot** on this model -> deniability is social, not forensic (accepted); lockout is soft (reflash+erase recovers).
- Per-mode look: Daily neutral steel-blue/innocent, Defense steel-blue + "DEF", Offense amber/red + "OFF" + red border.

---

## Build order: A (theming) -> C (PIN pad + store) -> B (knock)
Theming is self-contained and visible. The PIN pad is built + testable via a TEMPORARY entry. The knock is added last, targets the finished PIN pad, and swaps in as the real entry. Each phase compiles + flashes + is verified on-wrist before the next.

### Phase A - Per-mode theming + mode indicator  [BUILT 2026-07-22, compiles clean, awaiting flash]
Done: `theme.h` `ARGUS_OFFENSE_ACCENT` + decls; `theme.cpp` `argus_base_accent()` (steel Daily/Defense, amber Offense) + `argus_accent()` mode-aware (Daily NEVER flips red = innocent; Defense/Offense flip red under threat); persistent `lv_layer_top` indicator (Daily hidden / Defense steel "DEF" chip / Offense amber-red border frame + "OFF" chip). Wired in `main.cpp`: `argus_mode_indicator_init()` + `argus_mode_on_change` refresh at setup end; 1 Hz refresh in the loop tick. NOTE: the Offense visuals can't be seen on-device until Phase B/C make Offense reachable; Daily (clean) + Defense ("DEF", no red flip) are testable now.

Tasks:
- [ ] `theme.h`: add `ARGUS_OFFENSE_ACCENT` (amber `0xF0A030`), declare `argus_base_accent()`, `argus_mode_indicator_init()/refresh()`.
- [ ] `theme.cpp`: `argus_base_accent()` switches on `argus_mode_current()` (Daily/Defense = steel-blue; Offense = amber). Rewrite `argus_accent()` so threat-red still wins ON TOP **except in Daily**: `if (mode==Daily) return argus_base_accent(); if (threat) return HADES_RED; return argus_base_accent();` - this also resolves the open "detectors flip the accent in Daily" item (Daily stays innocent).
- [ ] Persistent indicator overlay on `lv_layer_top()` (created once at boot, transparent, non-clickable, `IGNORE_LAYOUT`): Daily = HIDDEN; Defense = small steel "DEF" chip; Offense = amber/red border frame + "OFF" chip.
- [ ] Wire `argus_mode_on_change` -> indicator refresh + repaint the visible title; extend the existing 1s status tick to refresh the indicator so the border/chip flip live under threat.
Touchpoints: `theme.{h,cpp}`, `main.cpp` (indicator init near clock build + on_change hook + status tick).
Acceptance: each mode unmistakable at a glance; Daily shows ZERO security markers and never flips red; Offense shows the red border.

### Phase C - PIN pad + two-PIN unlock/shred + security store  [BUILT 2026-07-22, compiles clean, awaiting flash]
Done: `security_store.{h,cpp}` (NVS "argussec", salted PBKDF2-HMAC-SHA256 via mbedtls, set-time validator [4-8 digits, shred>=unlock+1, distinct, no shared prefix], constant-time compare, RAM escalating rate-limit 3->5s/5->30s/8->5min). `pin_pad_screen.{h,cpp}` (button-matrix keypad, first-run PIN setup, unlock -> `enter_offense()`, shred -> `offense_shred()` + fake "Unlocking..." decoy -> empty grid, wrong -> rate-limited msg). Wired: create at boot, back-chain cancel, TEMPORARY Settings "Unlock Offense (test)" button (remove in Phase B).
**DESTRUCTIVE WIPE NOT WIRED YET:** `offense_shred()` currently burns only the persistent lockout flag (no `argus_mode_set_wipe_hook` registered), so a shred locks Offense out (permanent until erase+reflash) but does NOT delete any SD/NVS data. Wiring the file/NVS wipe is BLOCKED on Domenic's explicit wipe-scope sign-off.

Tasks:
- [ ] `security_store.{h,cpp}` - NVS namespace `"argussec"`. `salt` (16B `esp_random()`, once); `h_unlock`/`h_shred` = PBKDF2-HMAC-SHA256(pin, salt, iters ~= 50000) via bundled `mbedtls/pkcs5.h` (no new dep). NEVER store the PIN. `security_check(pin) -> {None,Unlock,Shred}` with constant-time compare; ONLY an exact `h_shred` match returns Shred. Rate-limit: escalating delay after 3 wrong, hard-lock after N. Persistent `off_lock` mirrors `argus_mode` lockout.
- [ ] Set-time validator (the only guard, since no runtime "are you sure"): shred-PIN length >= unlock length + 1, distinct, no shared prefix.
- [ ] `pin_pad_screen.{h,cpp}` - full-screen 3x4 button grid (0-9, backspace, OK), masked dots, 4-8 digits, NEUTRAL steel-blue (leaks nothing about what it guards). First-run: if no PINs set, prompt to set unlock then shred (with the validator).
- [ ] Unlock path: `h_unlock` match -> `enter_offense()` (tiles auto-reveal via the existing gating callback) + drop into the offense Tools grid.
- [ ] Shred path: `h_shred` match -> FAKE "Unlocking..." identical to the real transition, then a bare/empty offense screen, while `offense_shred()` runs in the background (register its wipe hook = wipe `/pwn/*.pcap`, `/Wardrive/*.csv`, offensive NVS namespaces; SD guard `instance.isCardReady() && !usb_sd_is_running()`; overwrite-before-unlink). NO confirmation dialog, ever.
- [ ] TEMPORARY test entry to reach the pad before the knock exists (a hidden Settings row or a debug long-press) - removed in Phase B.
Touchpoints: new `security_store.{h,cpp}`, `pin_pad_screen.{h,cpp}`; `argus_mode_set_wipe_hook()` (already exists).
Acceptance: unlock-PIN reveals Offense; shred-PIN shows fake unlock then empty screen and silently wipes + locks out; after shred EVERY PIN yields the empty screen across reboot (soft-recoverable only by erase+reflash); rate-limit blocks hammering; PINs stored only as salted hashes.

### Phase B - Side-button knock (extends the BOOT input layer)
Tasks:
- [ ] Extend the `main.cpp` BOOT state machine: classify each press SHORT/LONG (already have duration); keep a small recent-press buffer with timestamps; match **L-S-L** within the inter-press gap (`~700ms`).
- [ ] Coexistence (the reconciliation flagged when BOOT was built): a lone LONG now DEFERS "home" until the gap window passes with no SHORT following - because only the knock starts with a long. SHORT still acts immediately (can't start the knock). Net: back = instant; home = fires ~700ms after a lone long; L-S-L = PIN pad.
- [ ] Armed only when `argus_mode_current() != Offense` and no modal/PIN pad is open. On match -> `pin_pad_screen_show()`.
- [ ] Remove the Phase-C temporary test entry.
Touchpoints: `main.cpp` BOOT block; `pin_pad_screen.h`.
Acceptance: L-S-L on GPIO0 reliably opens the pad; a plain long still goes home (after the short defer); a plain short still does back/Settings; stray taps never open the pad.

---

## Risks / notes
- **Home gains a ~700ms delay** once the knock lands (must wait out the L-S-L window). Accepted; tune the gap if it feels off.
- **Shred is irreversible** (by design). The set-time distinct/longer-PIN validator is the only guard against fat-fingering; no runtime confirm (would defeat duress).
- **Soft lockout** - honestly a feature: the coercer's forced wipe is recoverable by the owner via erase+reflash.
- Watchdog: PBKDF2 at 50k iters is ~tens of ms; verify it doesn't trip the LVGL-thread watchdog, run off-thread if needed.

## Open decisions for Domenic
- [ ] **Knock timings** - SHORT/LONG thresholds + the L-S-L gap. Proposing LONG >= 600ms (matches the built BOOT layer), gap <= 700ms. Tune on-wrist.
- [ ] **PIN length** - unlock 4-8 digits; shred is then >= unlock+1. Confirm the minimum.
- [ ] **Wipe scope sign-off** - definite: `/pwn/*.pcap`, `/Wardrive/*.csv` + offensive NVS. Confirm `/PingSweeps`, `/Screenshots` in or out (from DEFENSIVE plan open decision #4).
- [ ] **First-run** - force PIN setup on first Offense-knock, or a Settings "Set offense PINs" entry? (Recommend: the knock's first-run flow, so nothing about it lives in visible Settings.)
