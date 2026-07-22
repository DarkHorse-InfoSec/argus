# ARGUS Mode Architecture — Implementation Plan

Status: DESIGN COMPLETE, ready to build. Owner: Domenic. Synthesized from 6 team design sections (input/knock, mode-state, gating, PIN+shred, theming, security review).

Target: LILYGO T-Watch Ultra, ESP32-S3, PlatformIO env `twatch_ultra`, LVGL 9.5, Arduino framework. Source root `src/`.

---

## 1. Overview

One watch, three user-selectable modes driven by a single new state machine (`argus_mode`), kept strictly orthogonal to the existing `DeviceMode{FieldTool, DailyWear}` radio/notification arbiter (do NOT overload it):

- **DAILY** (always the boot state): innocent smartwatch. Tools grid unreachable, all detectors/offensive tiles hidden, no security markers. Plausible deniability. A reboot or confiscation always shows Daily.
- **DEFENSE** (openly selectable from a normal menu): passive anti-surveillance. Threat Radar + detectors with alerting UI. No offensive tiles.
- **OFFENSE** (hidden, never persisted): the offensive suite. Reached ONLY by a side-button knock (`LONG-SHORT-LONG`) that arms a PIN pad; the correct UNLOCK-PIN reveals it. Always re-unlock after reboot.

Two-PIN duress: the PIN pad accepts an UNLOCK-PIN (reveals Offense) and a SHRED-PIN (fake "unlocking...", then a bare Offense screen while silently wiping offensive payloads/captures/config and burning a persistent lockout flag). Offensive CODE stays in flash; "shred" wipes DATA + locks out. Recovery is out-of-band (full flash + NVS erase + reflash).

Per-mode look and feel makes the active mode unmistakable (safety invariant: never fire an offensive action while the UI believes it is in Defense). Mode drives a layered base accent plus an always-visible indicator overlay, with the existing threat-red flip layered on top.

---

## 2. Architecture

### 2.1 State machine (the foundation everything hangs off)

New module `argus_mode.{cpp,h}` is the single source of truth. `DeviceMode` is left untouched.

```
enum class ArgusMode { Daily = 0, Defense, Offense };

ArgusMode argus_mode_current();               // RAM
bool      argus_mode_set(ArgusMode m);        // Daily<->Defense ONLY; REJECTS Offense
bool      is_offense_unlocked();              // RAM session flag
bool      enter_offense();                    // PIN-verified caller ONLY; RAM-only, NO persist; blocked if locked out
void      lock_offense();                     // -> previous non-offense mode; clears RAM flag
void      offense_shred();                    // wipe payloads/captures/config + burn lockout (flag FIRST)
bool      offense_locked_out();               // reads persistent NVS flag
void      argus_mode_on_change(cb);           // broadcast: theme + UI subscribe
```

State: two RAM globals, `s_mode` (default `Daily`) and `s_offense_unlocked=false`. NVS namespace `"argusmode"` (verify no collision; `argusnotify` and `argussec` are taken), single persisted key `lockout` (bool). NO mode value is ever persisted. Boot forces Daily.

### 2.2 Flow

```
        boot ----> ALWAYS Daily (argus_mode_init forces s_mode=Daily, loads lockout)
                     |
         menu/quick panel tap (argus_mode_set)
                     v
   Daily <--------------------> Defense        (free, persisted? -> OPEN DECISION #1)
     |  \                          |
     |   \ swipe-down GATED        | swipe-down -> Tools (Defense+Daily tiles)
     |    (Daily: gate returns)    |
     |                             |
     |        side-button KNOCK  LONG-SHORT-LONG  (GPIO0 FSM, coexists w/ Settings)
     |                             v
     |                        PIN PAD (neutral steel-blue, leaks nothing)
     |                        /            \
     |             UNLOCK-PIN               SHRED-PIN
     |                 |                        |
     |          enter_offense()          fake "Unlocking...", bare Offense screen,
     |          (RAM only, blocked         background silent wipe, burn lockout FIRST
     |           if locked_out)            (no confirm dialog, ever)
     |                 v                        v
     |            OFFENSE (amber/red)      Offense-empty + locked out forever
     |                 |
     |          lock_offense() or reboot
     +-----------------+---------------------> back to Daily/Defense
```

- **Knock -> PIN -> Offense**: knock FSM (P4) arms the pad; pad (P5) calls `enter_offense()` only on UNLOCK-PIN match.
- **Gating**: `argus_mode_current()` filters the Tools grid (show/hide) and gates the swipe-down front door and every offensive click handler (P2).
- **Theming**: `theme.cpp` READS `argus_mode_current()` for a per-mode base accent; indicator overlay reflects mode + threat (P3).
- **Shred**: SHRED-PIN path calls `offense_shred()` which burns lockout first, then wipes (P5).

---

## 3. Phased build plan

Ordered by dependency. Each phase is a team with concrete tasks, the real touchpoints the sections surfaced, and acceptance criteria.

### P1 — Mode-state foundation (TEAM: mode-state) [BLOCKS ALL] — BUILT 2026-07-22 (compiles clean, not yet flashed)

Built: `src/argus_mode.{h,cpp}` with the full 2.1 API, `argus_mode_init()` wired into `setup()` at `main.cpp` (before screen creation). Boot forces Daily unless Defense-persistence is on and Defense was last. Offense never persisted; `offense_shred()` burns the `lockout` NVS flag first then calls a registered wipe hook (P5). Defense-persistence is a setting (`argus_mode_defense_persist`). No visible effect yet - gating (P2) is what surfaces it.

Tasks:
- [ ] Create `src/argus_mode.{h,cpp}` with the API in 2.1. RAM globals; `"argusmode"` NVS namespace via `Preferences` (mirror `device_mode.cpp:18-25` pattern).
- [ ] `argus_mode_init()`: load `lockout`, FORCE `s_mode=Daily`. Call EARLY in `setup()` (before screen creation), separate from and not disturbing `device_mode_restore_boot()` at `main.cpp:1822`.
- [ ] `offense_shred()`: burn `lockout=1` FIRST (atomicity under power loss), then call the offense-suite wipe (P5 supplies exact paths). `enter_offense()` returns false when `offense_locked_out()`.
- [ ] `argus_mode_on_change()` broadcast so theme/UI re-render.

Touchpoints: new `argus_mode.{h,cpp}`; `main.cpp:1680-1822` (setup). Read-only alignment with `device_mode.h:15`, `device_mode.cpp:10/18-25/72`.

Acceptance:
- Boot always reports Daily regardless of prior state.
- `argus_mode_set(Offense)` is rejected and persists nothing.
- After `offense_shred()`, `offense_locked_out()` returns true across reboot; no API clears it.

Dependency: none. Everything else depends on this enum + getters.

### P2 — UI gating + Defense access (TEAM: ui-gating) [needs P1] — BUILT 2026-07-22 (compiles clean, not yet flashed)

Built: `tools_apply_mode()` + `tile_mode()` classification in tools_screen.cpp (exposed in .h); registered via `argus_mode_on_change()` + called in `tools_screen_show()`. Nav guard on the FULL clock-gesture surface (`main.cpp` on_clock_gesture): in Daily, swipe-down (Tools), swipe-left (Wardriver) and swipe-right (Meshtastic) are ALL gated - only swipe-up (Time) + the clock + Settings remain. Analyzer-exit path falls back to clock in Daily. (Fixed 2026-07-22: initially only swipe-down was gated, leaving Wardriver/Mesh reachable in Daily.) Offensive click handlers (handshake/mouse/tesla) guarded to Offense-only. Defense switch + "Keep on boot" toggle appended to the BOTTOM of Settings (safe insertion into the fixed-Y layout; ideally moves higher later - Open Decision #11). NOT done here: suppressing background-detector alert-UI (accent flip) in Daily - that lives in P3 (mode-aware theme), so a detector firing in Daily can still flip the accent red until P3 lands (Open Decision #8).

Tasks:
- [ ] Static `key -> ArgusMode` classification table mirroring `tile_keys[]` (`tools_screen.cpp:1401-1428`). See section 4.
- [ ] `tools_apply_mode()`: walk `tools_grid` children (`tools_screen.cpp:60`), read each key from `user_data`, toggle `LV_OBJ_FLAG_HIDDEN`. Daily hides all; Defense shows Defense+Daily; Offense clears all flags. LVGL 9 flex excludes HIDDEN from layout so the grid re-flows; build-once grid preserved, drag order intact. Do NOT rebuild/reconstruct.
- [ ] Navigation guard (the real gate): in `on_clock_gesture()` `LV_DIR_BOTTOM` (`main.cpp:678`) add `if (argus_mode_current()==Daily) return;` before `tools_screen_show()`. Add same guard on the analyzer-exit return-to-Tools path (`main.cpp:2111`).
- [ ] Defense-in-depth: guard each offensive click cb (`on_handshake_clicked`, `mouse_screen_show`, `tesla_cp_screen_show`) with `if (argus_mode_current()!=Offense) return;`.
- [ ] Call `tools_apply_mode()` from `argus_mode_set()` and from `tools_screen_show()` (`tools_screen.cpp:1520`) on entry (idempotent).
- [ ] Add the openly-selectable Defense toggle to a normal menu / quick panel (location TBD — see Open Decisions).

Touchpoints: `tools_screen.cpp:1311/1401-1428/1520`, `main.cpp:678/2111`.

Acceptance:
- Daily: swipe-down does nothing; grid unreachable (not merely blank).
- Defense: grid shows only Defense+Daily tiles; offensive tiles hidden.
- A hidden/stale offensive tile firing in non-Offense mode is a no-op (guard hit).

### P3 — Per-mode theming + indicator (TEAM: theming) [needs P1; parallel with P2]

Tasks:
- [ ] `theme.h`: add offense palette macros `ARGUS_OFFENSE_ACCENT` (amber `0xF0A030`, NOT alert red), `ARGUS_OFFENSE_DIM`; declare `argus_base_accent()`, `argus_mode_indicator_init/refresh()`.
- [ ] `theme.cpp`: `argus_base_accent()` switches on `argus_mode_current()` (Daily/Defense = `ARGUS_ACCENT` steel-blue; Offense = amber). Rewrite `argus_accent()` (`theme.cpp:26`) so threat-red still wins on top: `if (s_pipeline_threat || threatradar_top_level()>=TR_LVL_LIKELY) return HADES_RED; return argus_base_accent();`.
- [ ] Persistent indicator: one full-viewport overlay on `lv_layer_top()`, created once at boot, `LV_OPA_TRANSP` bg, `IGNORE_LAYOUT`, non-clickable. Daily = HIDDEN; Defense = steel-blue shield+`DEF` chip; Offense = amber border frame + `OFF`/teeth chip (both recolor to `HADES_RED` under threat).
- [ ] `main.cpp setup()` (~`:1470`, after LVGL/clock build): force Daily, then `argus_mode_indicator_init()`.
- [ ] Wire `argus_mode_on_change()` -> `argus_mode_indicator_refresh()` + `realign_status_icons()` (`main.cpp:421`) + repaint visible title. Extend the 1s status tick (`status_accent_active()`, `main.cpp:346`) to also call `argus_mode_indicator_refresh()` so border/chip flip live.

Touchpoints: `theme.h:14-46`, `theme.cpp:19-30`, `main.cpp:346/421/1331-1470`, `tools_screen.cpp:1319/1525`, `settings_screen.cpp:543`. Static `ARGUS_ACCENT` sites (~67) need NO rewrite — Daily/Defense share steel-blue; Offense screens read `argus_accent()` at build.

Acceptance:
- Each mode visually unmistakable at a glance; Daily shows zero security markers.
- Threat-red flip still reads in every mode (amber base preserves it).
- Duress fake-empty Offense screen suppresses the offense border (coercer sees a clean screen).

### P4 — Side-button knock detector (TEAM: input-knock) [needs P1 for `knock_armed()`]

DESIGN NOTE / CONFLICT (do not silently resolve — carried to Open Decisions #2): the prompt says the knock is on "the SIDE BUTTON — the SAME physical button that opens Settings." In THIS firmware those are two different buttons. The PMU/side power button (`main.cpp:1721-1749`) cycles screens and is hardware-pre-classified (no raw edge timing — unusable for morse). The GPIO0 back button (`main.cpp:636-637/1751-1753/2059-2118`) is the one that opens Settings (`settings_screen_show()`, `settings_screen.cpp:1233`) and gives raw edges. The input team designed for GPIO0 because only it matches BOTH "opens Settings" and "gives morse timing." Confirm before building.

Tasks:
- [ ] Replace FALLING ISR with an `IRAM_ATTR` CHANGE ISR near `main.cpp:636` that timestamps both edges (`{millis(), digitalRead(0)}`) into a small volatile ring. Change `attachInterrupt(0, ..., FALLING)` -> `CHANGE` at `main.cpp:1753`.
- [ ] Refactor the existing GPIO0 body (`main.cpp:2059-2118`) into a `back_button_action()` helper so all current back/Settings chains are preserved verbatim.
- [ ] Add `knock_poll()` FSM (call in `loop()` just before `main.cpp:2059`). Thresholds: glitch `<30ms`; SHORT press 40-350ms; LONG press 450-1500ms (350-450ms dead zone rejects); inter-press gap `SEQ_GAP_MAX=700ms`. Pattern `LONG,SHORT,LONG`.
- [ ] Coexistence: SHORT at progress 0 fires `back_button_action()` immediately (normal Settings). LONG at progress 0 starts pattern, withholds action. Match to progress 3 -> `pin_pad_show()` (P5). Mismatch/gap-timeout -> reset and fall through to `back_button_action()` so the button is never dead.
- [ ] `knock_armed()`: true only when `argus_mode_current()!=Offense` and no PIN pad/modal open (`s_low_mem_dialog==nullptr`). When disarmed, degrade to today's behavior.

Touchpoints: `main.cpp:636-637/1751-1753/2059-2118`; new `knock_poll()`/`back_button_action()`. Read-only dep on `argus_mode.h`, `settings_screen.cpp:1233`.

Acceptance:
- A plain single press still opens Settings (now on release, +<=350ms — verify on-wrist).
- `LONG-SHORT-LONG` reliably arms the pad; stray taps never arm; button never becomes dead.

### P5 — PIN pad + two-PIN unlock/shred (TEAM: pin-shred) [needs P1, P4]

Tasks:
- [ ] `src/security_store.{h,cpp}`: NVS namespace `"argussec"`. `salt` (16B `esp_random()`, generated once), `h_unlock`/`h_shred` = `PBKDF2-HMAC-SHA256(pin, salt, iters=50000)` via `mbedtls/pkcs5.h` (bundled, no new dep). Never store the PIN. Constant-time compare (fixed-length memcmp, no early return). `off_lock` bool = persistent lockout.
- [ ] API: `security_set_pins()`, `security_check(pin) -> {None,Unlock,Shred}`, `security_offense_available()` (false when `off_lock`), `security_shred_begin()`, `security_wipe_tick()`.
- [ ] `src/pin_pad_screen.{h,cpp}`: full-screen 3x4 `lv_button` grid (0-9, backspace, OK) built like `portscan_screen.cpp:344`; masked dots; 4-8 digits. NEUTRAL steel-blue (`ARGUS_ACCENT`), NOT red. `pin_pad_show()` invoked by the knock-armed edge.
- [ ] Unlock path: match `h_unlock` -> `enter_offense()` (RAM-only) + load offense grid. Wrong -> shake + escalating backoff (rate-limit counter in NVS: delay after 3, hard lock after N).
- [ ] Shred path: match `h_shred` -> immediate FAKE "Unlocking..." spinner identical to the real transition, then a bare/empty offense screen. Background wipe (FreeRTOS low-prio task or `loop()`-drained queue mirroring `handshake_bg_tick()` at `main.cpp:2171`): overwrite pcap/cred files with random data BEFORE unlink, recursive-delete offensive SD trees, `nvs_flash_erase()` offensive namespaces, set `off_lock` LAST relative to data but the mode-level `lockout` is burned FIRST by `offense_shred()`. NO confirmation dialog, ever.
- [ ] First-run: prompt to set both PINs. Set-time validator (only guard, since no runtime "are you sure"), per Domenic 2026-07-22: the SHRED-PIN must be at least ONE DIGIT LONGER than the UNLOCK-PIN, distinct, NO shared prefix. And only the EXACT shred-PIN hash triggers `security_shred_begin()` - `security_check()` returns Shred ONLY on that exact match; every other wrong entry returns None (fail + rate-limit), NEVER shreds.
- [ ] Confirm iters=50000 (~tens of ms) does not trip the LVGL-thread watchdog; run off-thread if needed.

Wipe scope (confirm before build — Open Decisions #4): definite wipe `/pwn/*.pcap` (`handshake.cpp:46/116`), `/Wardrive/*.csv` (`wardriver_screen.cpp:324`). Candidates flagged, defaulted to WIPE (safer): `/PingSweeps/*` (`pingsweep.cpp:135`), `/Screenshots/*` (`screenshot.cpp:16`). MUST NOT wipe: `/Icons`, `/backgrounds`, `/Settings`, `/Meshtastic`, `/AirTag`, `/Flipper`, `/Flock`, `/CounterTail`, `/EvilTwin`, `/HexHound`. Reuse `instance.isCardReady() && !usb_sd_is_running()` SD guard. Note: `SD.rmdir` needs empty dirs — walk-and-delete files first (helper + test).

Touchpoints: new `src/security_store.{h,cpp}`, `src/pin_pad_screen.{h,cpp}`; wire at knock arm (P4) + boot gate; offense entry gate.

Acceptance:
- UNLOCK-PIN reveals Offense; SHRED-PIN shows fake unlock then empty screen and silently wipes + locks out.
- After shred, EVERY PIN (even correct unlock) yields the empty screen; no code path clears `off_lock`.
- Rate-limiting blocks pad hammering; PINs stored only as salted hashes.

### P6 — Offensive-tools surfacing + polish (TEAM: ui-gating/theming) [needs P2, P3, P5]

Tasks:
- [ ] On successful `enter_offense()`, surface the Offense tile set (`handshake`/Pwn, `mouse`, `tesla`) via `tools_apply_mode()` clearing all HIDDEN flags; repaint amber accent + Offense indicator.
- [ ] Ensure `lock_offense()` and reboot cleanly return to Daily/Defense and re-hide tiles.
- [ ] Resolve the Evil Twin name collision: current `eviltwin` tile is a DETECTOR (Defense); keep offensive evil-portal as a distinct future Offense key.

Acceptance: Offense tiles appear only after unlock, disappear on lock/reboot, and the mode indicator matches at all times.

### P7 — Hardening + on-device test (TEAM: security-review) [needs P1-P6]

Tasks (ranked, from risk register in section 5):
- [ ] Evaluate ESP32-S3 flash encryption (release) + secure boot v2 + encrypted NVS (`nvs_keys`) via `board_build.partitions` in `platformio.ini` (R1). Confirm dev-reflash cost is acceptable before enabling (Open Decisions #5).
- [ ] Decide single-image vs two-artifact build to strip offensive strings/symbols from Daily image (R2, Open Decisions #6).
- [ ] Verify shred overwrites-before-unlink and full-partition NVS erase (R3).
- [ ] Verify salted-hash PIN storage + rate-limit lockout (R4).
- [ ] Verify set-time distinct-PIN validator prevents self-shred (R5).
- [ ] Honestly document lockout is soft (reflash-recoverable) unless an eFuse bit is used (R6); check eFuse availability (Open Decisions #3).
- [ ] On-wrist test matrix: boot->Daily, Daily<->Defense, knock arm + coexistence, unlock, duress shred (on a SCRATCH unit — irreversible), threat-red flip in each mode, indicator correctness.

Acceptance: risk register items each have a verified status; on-device test matrix passes on a scratch unit.

---

## 4. Tile / detector classification

| Tile key / handler | Function | Mode |
|---|---|---|
| `radar` -> `threat_radar_screen_show` | tail-correlation scope | DEFENSE |
| `airtag` -> `on_airtag_clicked` | BLE Find My tracker detect | DEFENSE |
| `flock` -> `on_flock_clicked` | surveillance cam/drone detect | DEFENSE |
| `skimmer` -> `on_skimmer_clicked` | card-skimmer detect | DEFENSE |
| `flipper` -> `on_flipper_clicked` | Flipper presence detect (passive) | DEFENSE |
| `eviltwin` -> `on_eviltwin_clicked` | rogue-AP DETECTOR | DEFENSE (name collides w/ future offensive evil-portal) |
| `notify` -> `notifications_screen_show` | phone notif mirror | DAILY |
| `aprs` -> `aprs_screen_show` | ham comms | DAILY |
| `usbsd` -> `usb_sd_screen_show` | card-reader utility | DAILY |
| `handshake` -> `on_handshake_clicked` | WPA/PMKID capture | OFFENSE |
| `mouse` -> `mouse_screen_show` | BT HID injection | OFFENSE |
| `tesla` -> `tesla_cp_screen_show` | 315 MHz charge-port TX | OFFENSE |
| `wifi` -> `wifi_screen_show` | site-survey + ping-sweep | AMBIGUOUS — defaulted OUT of Daily; survey=Defense, ping-sweep=active/Offense (Open Decisions #7) |
| `analyze` -> `analyze_screen_show` | passive channel util | DEFENSE (passive; could be DAILY) |
| `pager` -> `pager_screen_show` | POCSAG/FLEX decode | DEFENSE (interception; NOT innocent — keep OUT of Daily) |
| `tpms` -> `tpms_screen_show` | passive TPMS RX | DEFENSE (vehicle-track-capable) |
| `pet` -> `pet_screen_show` (HexHound) | recon "pet" | DEFENSE |
| Background: `detect_pipeline` / `ble_detect_pipeline` / `deauth` / `threat_radar` | passive threat aggregation, runs from `loop()` 1Hz regardless of screen | DEFENSE (alert UI must be SUPPRESSED in Daily — Open Decisions #8) |

Ambiguous tiles were defaulted to the MORE restrictive reading (kept out of Daily); confirm per-tile.

---

## 5. Risk register (ranked)

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| R1 | Flash dump reveals everything — stock image, no encryption/secure boot; `esptool read_flash` exfils firmware + NVS in 30s | CRITICAL | Enable ESP32-S3 flash encryption (release) + secure boot v2 + encrypted NVS. The single highest-leverage change; without it R2-R6 are moot. Daily-default-boot gives SOCIAL deniability, not FORENSIC deniability — frame honestly to the user. |
| R2 | Offensive strings/symbols (`evil_twin`, `/pwn/`, `deauth`, `HID`) in the Daily image; `strings dump.bin` reveals intent | HIGH | Gate offensive `.cpp` behind a build flag; ship two artifacts (clean Daily/Defense `.bin` + authorized-testing `.bin`). If single-image mandatory: strip symbols + obfuscate tell-tale strings (defense-in-depth, not a fix). |
| R3 | Shred leaves recoverable remnants — FAT delete only unlinks; NVS is log-structured/wear-leveled | HIGH | Overwrite pcap/cred files with random data BEFORE unlink; `nvs_flash_erase()` whole partition not per-key. Document only full flash erase + reflash is a guaranteed wipe. |
| R4 | PIN storage / brute force — 4-digit pad = 10^4; stored PIN in dumpable NVS trivially recovered | HIGH | Salted PBKDF2-HMAC-SHA256 (high iters) in encrypted NVS; escalating rate-limit lockout (delay after 3, hard lock after N). |
| R5 | Accidental self-shred — no confirmation by design | MEDIUM | SHRED-PIN: zero shared prefix, different length, adequate Hamming distance from UNLOCK-PIN; enforced at set time (only guard). Consider requiring full knock+pad, not a bare pad. |
| R6 | "NVS lockout survives reflash" is FALSE — `esptool erase_flash` + reflash clears NVS; only an eFuse bit truly persists | MEDIUM | Either burn a one-way eFuse for true tamper-persistence, or state honestly that lockout is soft/reflash-recoverable (arguably the safer duress story — the coercer's wipe is not permanent for the owner). |
| R7 | Knock/Settings coexistence race on the shared button ISR — could eat legit Settings-opens or mis-trigger | LOW-MED | FSM only ARMS on exact pattern; always falls through to `back_button_action()`/`settings_screen_show()` on a plain press. Tune timings on hardware. |

Legal note (from security review): a duress wipe of authorized-testing artifacts is defensible as data hygiene, but destroying data under legal compulsion (served warrant, border hold) can be spoliation/obstruction in some jurisdictions. This is a personal-safety anti-coercion feature for hostile-actor scenarios, NOT for use against lawful process. Advise Domenic to get counsel before relying on it in a regulated engagement.

---

## 6. Open decisions needing Domenic

1. **Does Defense persist across reboot?** DECIDED 2026-07-22: it's a **user SETTING** (`argus_mode_defense_persist`, default OFF -> boot Daily; ON -> boot back into Defense). Default preserves the confiscation guarantee; opting in is the user's call. Built in P1. Still need: WHERE the toggle lives in Settings UI (see #11).
2. **Which physical button is "the SIDE BUTTON"?** DECIDED 2026-07-22: the **BOOT button (GPIO0)** - a single click opens Settings (confirmed against the T-Watch Ultra pinmap: BOOT = GPIO0; the AXP2101 Power button is separate). P4 builds the knock FSM on GPIO0 as the input team designed. Resolved.
3. **Are eFuses available/uncommitted on the target units?** MOOT for now - flash encryption/secure boot deferred (see #5), so lockout stays soft/reflash-recoverable by design (acceptable; it's arguably the better duress story - the coercer's wipe isn't permanent for the owner).
4. **Wipe-scope sign-off:** confirm `/PingSweeps/*` and `/Screenshots/*` are wiped (defaulted to WIPE, safer). Confirm the do-NOT-wipe list. (Blocks P5 wipe implementation.) STILL OPEN.
5. **Accept flash-encryption release-mode cost?** DECIDED 2026-07-22: **NO** - do not add flash encryption / secure boot on this model; it makes reflashing harder for no proportional gain here. Revisit only if the community asks. CONSEQUENCE (accept honestly): deniability is **social, not forensic** - a flash dump reveals the offensive firmware. Daily-default-boot defeats a glance/handler, not a lab. R1/R2 mitigations are therefore DEFERRED, not applied.
6. **Single-image or two-artifact build?** Deferred with #5 (R2). Single image for now.
7. **Compound/ambiguous tiles** (`wifi` = survey + ping-sweep; also `analyze`, `pager`, `tpms`): per-tile ruling. Defaulted to the more restrictive reading (out of Daily). Consider splitting `wifi` into passive-survey (Defense) vs active ping-sweep (Offense). STILL OPEN (P2).
8. **Background detectors in Daily:** they run from `loop()` regardless of screen and would flip the HADES accent, breaking the innocent look. Recommend suppressing the alert UI (accent flip, popups) in Daily while detection keeps logging silently. Confirm. STILL OPEN (P2/P3).
9. **Exact knock timings** (`LONG` min/max, `SEQ_GAP_MAX`, dead zone) — tune on-wrist after P4 builds; the values in P4 are starting candidates.
10. **PIN lengths / policy** — DECIDED 2026-07-22: the **SHRED-PIN must be at least one digit LONGER than the UNLOCK-PIN** (enforced at set time), and ONLY the exact shred-PIN triggers the wipe - any other wrong entry just fails + rate-limits (never shreds on a generic wrong PIN). Confirm the unlock-PIN min length (proposing 4-8 digits, so shred is >= unlock+1).
11. **Where the Defense toggle + Defense-persistence setting live** (normal menu vs quick panel) — P2/Settings needs a concrete home for the openly-selectable Daily<->Defense control and the persistence checkbox. STILL OPEN.
