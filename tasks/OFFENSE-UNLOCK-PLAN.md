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
**DESTRUCTIVE WIPE - WIRED (Tier 1, signed off 2026-07-22).** `offense_wipe.{h,cpp}` registers the `argus_mode_set_wipe_hook()` at boot; `offense_shred()` burns the lockout first, then the hook wipes ONLY the offensive artifact dirs: `/pwn`, `/Wardrive`, `/PingSweeps`, `/Screenshots` (overwrite-with-zeros in place, then unlink, then rmdir the now-empty dir). Guarded by `!instance.isCardReady() || usb_sd_is_running()`. Everything else is preserved by design: defensive detection logs (the cover story), comms/config/creds, and NVS. NVS is deliberately UNTOUCHED - the `argus_mode` lockout flag and the `argussec` PIN store must survive or the shred would unlock itself (ground-truth check: there are NO offensive NVS namespaces; the handoff's "wipe offensive NVS" line was moot AND would have been harmful). Full rationale in `offense_wipe.cpp` header.

Tasks:
- [ ] `security_store.{h,cpp}` - NVS namespace `"argussec"`. `salt` (16B `esp_random()`, once); `h_unlock`/`h_shred` = PBKDF2-HMAC-SHA256(pin, salt, iters ~= 50000) via bundled `mbedtls/pkcs5.h` (no new dep). NEVER store the PIN. `security_check(pin) -> {None,Unlock,Shred}` with constant-time compare; ONLY an exact `h_shred` match returns Shred. Rate-limit: escalating delay after 3 wrong, hard-lock after N. Persistent `off_lock` mirrors `argus_mode` lockout.
- [ ] Set-time validator (the only guard, since no runtime "are you sure"): shred-PIN length >= unlock length + 1, distinct, no shared prefix.
- [ ] `pin_pad_screen.{h,cpp}` - full-screen 3x4 button grid (0-9, backspace, OK), masked dots, 4-8 digits, NEUTRAL steel-blue (leaks nothing about what it guards). First-run: if no PINs set, prompt to set unlock then shred (with the validator).
- [ ] Unlock path: `h_unlock` match -> `enter_offense()` (tiles auto-reveal via the existing gating callback) + drop into the offense Tools grid.
- [ ] Shred path: `h_shred` match -> FAKE "Unlocking..." identical to the real transition, then a bare/empty offense screen, while `offense_shred()` runs in the background (register its wipe hook = wipe `/pwn/*.pcap`, `/Wardrive/*.csv`, offensive NVS namespaces; SD guard `instance.isCardReady() && !usb_sd_is_running()`; overwrite-before-unlink). NO confirmation dialog, ever.
- [x] TEMPORARY test entry to reach the pad before the knock exists (a hidden Settings row or a debug long-press) - removed in Phase B (2026-07-22).
Touchpoints: new `security_store.{h,cpp}`, `pin_pad_screen.{h,cpp}`; `argus_mode_set_wipe_hook()` (already exists).
Acceptance: unlock-PIN reveals Offense; shred-PIN shows fake unlock then empty screen and silently wipes + locks out; after shred EVERY PIN yields the empty screen across reboot (soft-recoverable only by erase+reflash); rate-limit blocks hammering; PINs stored only as salted hashes.

### Phase B - Side-button knock (extends the BOOT input layer)  [BUILT 2026-07-22, compiles clean, awaiting on-wrist verify]
Done: `main.cpp` gained a small knock detector (`boot_knock_feed/poll/flush`, `KNOCK_GAP_MS = 700`) layered onto the existing BOOT edges. Each completed press is classified SHORT/LONG (existing duration logic) and fed in. The buffer only ever holds a knock prefix [L] or [L,S]; **L-S-L** within the gap calls `pin_pad_screen_show()`. A lone LONG is deferred by `KNOCK_GAP_MS` (poll flush fires "home" if no SHORT follows); a SHORT with an empty buffer acts immediately. Knock is disarmed when `argus_mode_current() == Offense` or the low-mem modal is open (`s_low_mem_dialog`), where the press runs its ordinary action. The Phase-C temporary "Unlock Offense (test)" Settings row + its `pin_pad_screen.h` include were removed.
Tasks:
- [x] Extend the `main.cpp` BOOT state machine: classify each press SHORT/LONG; buffer recent presses with a timestamp; match **L-S-L** within `KNOCK_GAP_MS` (700ms).
- [x] Coexistence: a lone LONG DEFERS "home" until the gap passes with no SHORT following (only the knock starts with a long). SHORT with an empty buffer acts immediately. Net: back = instant; home = fires ~700ms after a lone long; L-S-L = PIN pad.
- [x] Armed only when `argus_mode_current() != Offense` and no modal is open. On match -> `pin_pad_screen_show()`.
- [x] Remove the Phase-C temporary test entry.
Touchpoints: `main.cpp` BOOT block; `settings_screen.cpp`.
Acceptance: L-S-L on GPIO0 reliably opens the pad; a plain long still goes home (after the ~700ms defer); a plain short still does back/Settings; stray taps never open the pad. **On-wrist verify pending.**
**CORRECTION 2026-07-22:** the temporary "Unlock Offense (test)" Settings row was RESTORED (verification ordering - do not remove a tested entry before the replacement is verified on-wrist; removing it left a build with no Offense entry until the knock is confirmed). Remove it only AFTER the knock is confirmed working on-wrist.
**KNOCK BUG FIXED 2026-07-22 pt2:** the knock never fired on-wrist. Root cause: the inter-beat gap was measured release-to-release, but the final LONG beat holds ~600ms, so its release always landed >KNOCK_GAP_MS after the SHORT's release - the poll flushed [L,S] mid-hold, making L-S-L impossible (only ~100ms to even start the final long). Fixed: gap is now measured from the previous beat's RELEASE to the next beat's PRESS-DOWN (boot_knock_feed takes press-down + release times). Awaiting on-wrist re-verify.
**EXIT ADDED 2026-07-22 pt2:** Offense had NO exit (lock_offense() existed but nothing called it). Added a Settings "Exit Offense" button shown only in Offense (the test-unlock button shows only outside Offense); it calls lock_offense() + clock_screen_show(). Redesign target: make the header mode badge the exit affordance and retire both temporary Settings buttons. See tasks/OFFENSE-REDESIGN-PLAN.md.
**STRICT TOOL SEPARATION 2026-07-22 pt2:** tools_apply_mode() now shows ONLY offensive tiles in Offense (was showing all). Defense still carries the neutral utilities (notify/aprs/usbsd) so they aren't stranded - final taxonomy is a redesign decision (OFFENSE-REDESIGN-PLAN.md sec 3).

### Same-session bug fixes (2026-07-22, from on-wrist photos)  [BUILT, compiles clean, awaiting re-verify]
- **DEF/OFF chip clipped by the rounded corner.** Two causes, fixed in two passes. (1) `LV_OBJ_FLAG_IGNORE_LAYOUT` on the chip dropped `lv_obj_align`'s offset, pinning it to (0,0) - removed the flag. (2) Even aligned, `16,44` still sat inside the top-left corner curve (the AMOLED physically masks corner pixels; the status icons clear it by sitting ~70px inset on the right at `TOP_RIGHT,-70,20`). Final fix (`theme.cpp` `argus_mode_indicator_init`): absolute `lv_obj_set_pos(chip, 24, 72)` - below the top curve (matching the notify banner's y=72 clearance), no align-resolution ambiguity.
- **PIN pad backspace/OK keys were empty "tofu" squares.** Root cause: the keypad matrix used `font_argus_label_28`, an Orbitron subset carrying only digits/colon/space/AMP, so `LV_SYMBOL_BACKSPACE`/`LV_SYMBOL_OK` had no glyphs. Fix (`pin_pad_screen.cpp`): keypad items now use `&lv_font_montserrat_28`, which bundles those FontAwesome glyphs - both action keys render as real icons, digits stay legible.

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
