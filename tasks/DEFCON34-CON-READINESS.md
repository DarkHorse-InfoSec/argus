# ARGUS on the wrist at DEF CON 34 - con-readiness audit

Answers the questions in a local `QUESTIONS-FOR-ARGUS-REPO-20260728.txt` (not in this repo).
Audited 2026-07-28 against the working tree at branch `darkhorse-argus`, HEAD `6e9b14b`.
Every claim below is from the code, not the README.

Scope note: this file is about **ARGUS on the T-Watch Ultra**. It says nothing
about Pocket Threat Radar (PTR), which is a separate passive handheld.

---

## Headline

The saturation fear was the wrong thing to be most afraid of. Memory is the
best-engineered part of this codebase: there is not a single dynamic allocation
or STL container in `src/`, every detector table is a fixed array with an
explicit LRU eviction policy, and the haptic is edge-latched rather than
per-detection. ARGUS will not run out of RAM on the con floor.

The two things that actually needed fixing were both about what the watch
**emits and retains**, not what it can survive:

1. The "Broadcast Location" toggle was dead code. Fixed (see below).
2. `/ThreatRadar/discovered.txt` stores other people's MACs in the clear
   alongside two 6-decimal GPS fixes, append-only, with no retention cap.
   **RESOLVED 2026-08-03** in two steps: `c50e57b` cut logged GPS to
   ~110 m, and retention now expires records at 30 days with a 300-entry
   cap per log, swept at boot and enforced on every append. Settings gained
   a record count and a one-tap "Clear detection logs". See
   `src/detect/log_retention.h` for the policy and the reasoning.

---

## Q1. What exactly transmits, and when?

**a. TRFLAG gating - Confirmed only.** `threat_radar.cpp:277` fires
`tracker_rep_flag_local()` only at `level >= TR_LVL_CONFIRMED`, behind a
`c->broadcast` edge latch so a persistent tail is announced once, not
repeatedly. `tracker_rep.cpp:74` additionally requires `rep_add()` to report a
genuinely new hash and requires `meshtastic_is_active()`. Payload is
`TRFLAG|<8 hex>|<1 char>`, max 17 bytes. The MAC is a 32-bit FNV-1a hash, never
in the clear. This is a well-behaved, rare, tiny transmission. It is not the
thing to worry about.

**b. Toggle to disable TRFLAG only?** No dedicated toggle. It is implicitly
disabled by not having the mesh up (`meshtastic_is_active()` false). Given (a),
I do not think a dedicated toggle is worth adding before Aug 6.

**c. Broadcast Location - this was a bug, now fixed.**
The default is `false` (`configuration_screen.cpp:27`) and it persists to
`/Meshtastic/config.txt`. But **`configuration_screen_get_broadcast_location()`
had zero consumers** - the toggle, its GPS-lock interlock, its persistence and
its companion `configuration_screen_get_broadcast_interval_ms()` were all
UI-only. No periodic position broadcast was ever wired to the radio.

Good news inside that: ARGUS has never had a periodic position broadcast. The
bad news is the switch made a privacy promise it was not keeping, in either
direction.

Separately, `meshtastic.cpp handle_position()` auto-replied with the wearer's
exact GPS position to any peer that sent a directed empty-Position request,
gated only on having a fix. Your node id is discoverable because NodeInfo is
announced every 10 minutes by default (see d), so on LongFast at DEF CON a
stranger could have queried your location and the watch would have answered.

**Fix applied:** the auto-reply is now gated on
`configuration_screen_get_broadcast_location()`. Default OFF means ARGUS does
not answer position queries unless you deliberately opt in, and the existing
toggle now governs the thing it is named after. If you want your own group to
be able to locate you, turn it on in Configuration; it survives reboot.

**d. Does joining the mesh transmit? Yes - this is the real answer to "am I
emitting".** `s_announce_on = true`, `s_announce_interval = 600000` ms
(`meshtastic.cpp:74-75`). Every 10 minutes, whenever the LoRa radio is active,
the watch sends **NodeInfo** (your long name, short name, node id) plus
`send_telemetry_broadcast()` (battery and uptime). It is not RX-only. It fires
immediately on activation and on any name change, and defaults apply when
`/Meshtastic/config.txt` is absent.

Nothing about that is anomalous at a con with its own Meshtastic node, but be
deliberate about your long/short name - it is broadcast in the clear on
LongFast every 10 minutes and it is your identity, not a hash.

**e. Band - correct for Las Vegas.** `MESH_FREQ_MHZ = 906.875f`
(`meshtastic.cpp:40`), inside the US 902-928 MHz ISM band. The LoRa screen
displays "906.9 MHz". Correct for Nevada, no change needed.

---

## Q2. Saturation

**a. Everything is bounded, with documented eviction.** No `malloc`, `new`,
`realloc`, or STL container anywhere in `src/`. Full inventory:

| Table | Cap | Full behaviour |
|---|---|---|
| `TailDetector::devices_` | 32 | LRU evict |
| `TailDetector` cells/device | 8 | count saturates |
| `BeaconFloodDetector::bssids_` | 64 | LRU evict |
| `BleSpamDetector::addrs_` | 64 | LRU evict |
| `DeauthFloodDetector::bssids_` | 16 | LRU evict |
| `EvilTwinDetector` ssids/bssids | 32 / 128 / 8 per SSID | stops tracking new SSIDs, keeps flagging |
| `threat_radar` contacts | 48 | evict lowest level, then oldest |
| `tracker_rep` flags | `TR_REP_MAX` | reuse expired, else evict oldest |
| AirTag/Flipper/Skimmer/Tracker dedup | 32 each | LRU evict |
| `spycam` / `probe_sniffer` / `pwnagotchi_peer` | 16 / 24 / 16 | fixed |

The `threat_radar` eviction policy is the one that matters most and it is the
right one: it evicts the *least interesting* contact (lowest threat level, then
oldest), so con-floor noise gets recycled while an actual escalating tail is
retained. Alert state is an edge latch (`c->alerted`), and reaching
`TR_LVL_LIKELY` requires real waypoint plus span co-movement evidence, which
ambient con density does not manufacture.

**b. Peak heap - not measured, and I could not measure it from here.** Static
build figures for the current image: RAM 52.8% (173112 / 327680), flash 94.3%
(2966917 / 3145728). Because every table is fixed-size and preallocated, the
detector working set does not grow with device count at all - the tables are
the same size with 5 devices in range or 5000. That is a much stronger
guarantee than a measurement, and it is why I am not worried here.

Flash at 94.3% is worth knowing: there is about 179 KB of headroom. Do not plan
a large feature before the con.

**c. Alert fatigue - not the problem it looked like.** The haptic is never
fired per detection. `instance.vibrator()` call sites are: alarm ringing,
Meshtastic message RX (separately toggleable for DM vs broadcast), screen touch
and power-button feedback (`clock_vibrate`), the knock sequence, and
`threat_radar`'s triple-pulse. That last one is `s_buzz_left = 3`, sequenced
non-blocking one pulse per 300 ms, fired once per contact behind the `alerted`
latch and suppressed entirely for vehicles `counter_tail` has learned as
familiar. The detector badges (AirTag, Flipper, Skimmer, Flock, Evil-Twin) do
not buzz at all - they increment a counter and enqueue an SD line.

I recommend no rate-limit change. Adding one would be solving a problem the
code does not have, and the residual risk (an evicted contact returning and
re-climbing to LIKELY) requires genuine co-movement evidence to trigger.

**d. Battery runtime with scanning on - unmeasured, and it is the real gap.**
Nothing in the tree records this. This is the one saturation-adjacent number
you should get empirically before you go. See "What to actually do" below.

---

## Q3. The crash - root-caused, not moved

It was root-caused, and the theory in the question was correct.

Two commits fixed it:
- `daf97e1` (07-24 18:11) "fix: resolve intermittent battery boot-loop
  (SensorLib BHI260 vtable crash)"
- `64dcd0f` (07-25 09:46) "fix: SensorLib BHI260 callback-table overrun
  (deeper boot-loop root cause)"

A callback-table overrun corrupting a function pointer is exactly what produces
a jump to a non-code address, so `0x3065a54a` is explained rather than
explained away.

Three corrections to the premise in the prompt:

1. There is a **later** dump than the one the prompt cites:
   `argus-twatch_ultra-20260725-071547`, at 07:15 on 07-25. It still predates
   the deeper fix at 09:46 and the current build at 14:30, so it is consistent
   with the same pre-fix defect. The current image has no recorded crash.
2. **The `tz_worker` / `timezone.cpp:110` reading is not trustworthy.**
   `ondevice-before-diagnostic-against-current-elf.txt` contains the decoder's
   own verdict: `Failed to load core dump: Invalid application image for
   coredump: coredump SHA256(c58b2d62419e622f) != app SHA256(0ca9155b8c7b96bf)`.
   Every symbol in that gdb output was resolved against a mismatched ELF - note
   all frames print as `?? ()` and every task name is blank. `tz_worker` was
   incidental to a bad decode, not implicated.
3. The genuinely useful register was never used: the 07-25 dump records
   `exccause 0x0 (IllegalInstructionCause)` with **`epc1 0x421408a9`**, which
   lands inside `.flash.text` (0x42000020 + 0x1c1967). `epc1` is the faulting
   PC; the `pc 0x3225a54a` in the stack block is a garbage frame. Symbolising
   `epc1` against the *matching* ELF is the only way to close this properly,
   and that ELF is gone - it was overwritten by the 14:30 build.

**Verdict: fixed, with a caveat.** The root cause is named, the fix is
specific, and the bad-function-pointer theory is confirmed rather than assumed.
But "no crash since" still rests on a build that has not been to a con.

Recommendation: keep `read_coredump.ps1` and a copy of the **current**
`firmware.elf` on the travel laptop. If it panics at the con, an ELF that
matches is the difference between a root cause and another week of `?? ()`.

---

## Q4. What is written to SD - the open item

This is the one that still needs your decision.

**a/b. Yes, sightings are persisted, with cleartext MACs and GPS.**
`threat_radar.cpp:161-182` appends to `/ThreatRadar/discovered.txt` one line
per first alert per contact, containing: local timestamp, threat level,
category, **MAC in the clear**, waypoint count, span in metres, dwell minutes,
best RSSI, first-seen time, and **two GPS coordinate pairs at 6 decimal places**
(`FirstGPS` and `FarGPS`, roughly 0.1 m precision).

It was append-only, with no retention cap, no rotation, and no age-out.

**RESOLVED 2026-08-03.** Records now expire: 30 days maximum age, 300 entries
maximum per log, oldest evicted first. Enforced on every append (cheap: it only
rewrites when something must be dropped, via temp-file-then-rename so a power
loss cannot destroy the log) and swept once at boot, so a watch that detects
nothing for a month still ages its records out. `/Flock` per-hit files are
pruned by the same window. Settings shows the live record count and offers a
one-tap "Clear detection logs".

The MAC deliberately stays in the clear. This log is the wearer's own evidence
about a device that followed THEM, and a hashed MAC cannot be matched against a
device they later physically identify. What is bounded is retention, not
fidelity. Policy and rationale: `src/detect/log_retention.h`; the policy is
pure and covered by `test/test_log_retention.cpp`.

The same shape applies to `/AirTag/discovered.txt`, `/Flipper/discovered.txt`,
`/Skimmers/discovered.txt`, `/EvilTwin/discovered.txt`, `/Flock/<name>.txt`,
and `/Wardrive/<timestamp>.csv`.

Volume at the con is self-limiting - a line is written only on first alert per
contact, at `TR_LVL_LIKELY` or above, which needs co-movement evidence. You
will not get thousands of lines. But every line you do get is a real person's
device identifier and where they were standing, and the mitigating factor is
the *rarity* of the records, not their contents.

**c. The one-sentence answer you asked for**, accurate as the code stands
today:

> It logs a device's MAC, GPS position and timestamp to the SD card, but only
> for a device it has decided is following me, and nothing leaves the watch
> except a 32-bit hash on the mesh when a tail is confirmed.

That sentence is true and it is defensible. Whether you are comfortable saying
it in a privacy village is a separate question, and it is yours.

**DECIDED (Domenic, 2026-07-28): wipe before and after, plus truncate GPS to 3
decimals.** The truncation is implemented; the wipe is a manual step below.

**Scope note - this went wider than the question I asked.** I originally framed
this around `/ThreatRadar/discovered.txt`. On implementing it I found the same
cleartext-MAC-plus-precise-GPS pattern in five **higher-volume** logs:
`/AirTag/`, `/Flipper/`, `/Skimmers/`, `/EvilTwin/` and `/Flock/`. Those fire on
every logged detection (rate-limited only by each detector's relog interval and
32-entry LRU), not just at LIKELY+, so at the con they will hold far more
records than ThreatRadar will. Truncating ThreatRadar alone would not have
achieved what was chosen, so I applied it to all six. Say the word and it is a
two-line revert.

Deliberately **not** changed:
- `/Wardrive/*.csv` - a WiGLE export whose entire purpose is a precise survey
  you chose to collect. Truncating it would break the format's usefulness.
- `/Pager/messages.txt` (`pager.cpp:178`) - logs *your own* position against a
  received POCSAG message. Different category. Flagging it rather than changing
  it; say if you want it truncated too.

The precision constant is centralised as `geo::kGpsLogDecimals` in
`src/geo_cell.h` with the rationale attached, so the six sites cannot drift
apart and nobody re-raises one to `%.6f` without reading why.

**The manual wipe, before you go and again on the way back:**

```
del G:\ThreatRadar\discovered.txt
del G:\AirTag\discovered.txt
del G:\Flipper\discovered.txt
del G:\Skimmers\discovered.txt
del G:\EvilTwin\discovered.txt
del G:\Flock\*.txt
```

Doing this *before* the trip matters as much as after: those files hold your own
home-area movement history, and that is the part that travels with you into a
hostile venue.

**First pass done 2026-07-28** (card mounted as G: over the USB SD tile):
- Deleted `/AirTag/discovered.txt` (42 lines, 8022 bytes). Every pre-flash entry
  was the owner's home location at sub-metre precision with tracker MACs
  attached. (Coordinates deliberately not reproduced in this file - see the
  redaction note at the end.)
- Moved `/pwn/20260723-081317.pcap` (5017 bytes, a real EAPOL capture) off the
  card to a local captures directory (not in this repo). Deliberately placed
  OUTSIDE the argus-watch repo: `artifacts/` is gitignored, but this tree is
  headed for public release and a third-party handshake capture should not sit
  inside it at all.
- All six detector log dirs and `/pwn/` are now empty.

**THIS IS NOT THE PRE-DEPARTURE WIPE.** The watch resumes writing the moment the
card is unmounted, so between now and Aug 6 these files will refill with home
data. Repeat the wipe immediately before you leave.

**STILL OUTSTANDING - `/Wardrive/`.** 28 CSVs, 896 KB, spanning 07-20 to 07-28,
at full WiGLE precision over the home area. Kept by decision (they may be queued
for WiGLE upload) but flagged to **move off the card before the con**. Note one
was written today, `20260728_130845.csv`, so this set is still growing.

**Updated one-sentence answer**, accurate as of this change:

> It logs a device's MAC and a timestamp to the SD card, with my position
> rounded to about 110 metres, and only for a device it has decided is
> following me; nothing leaves the watch except a 32-bit hash on the mesh when
> a tail is confirmed.

**Final one-sentence answer**, accurate as of the 2026-08-03 retention work:

> It logs a device's MAC and a timestamp to the SD card, with my position
> rounded to about 110 metres, only for a device it has decided is following
> me; those records delete themselves after 30 days and are capped at 300, I
> can wipe them all from Settings in one tap, and nothing leaves the watch
> except a 32-bit hash on the mesh when a tail is confirmed.

---

## Q5. The Pwn tile - present, off, does not persist

**a. Present in ARGUS.** `tools_screen.cpp:1785` creates a tile literally
labelled "Pwn"; it toggles `handshake_start()` / `handshake_stop()`.
`handshake.cpp` captures EAPOL (WPA handshakes and PMKID from M1) and
`main.cpp:2429` drains them to `/pwn/<ts>.pcap`.

**b. Off by default and it stays off.** `boot_prefs.cpp` persists exactly four
flags - `wifi`, `ble`, `lora`, `gps`. Handshake capture is not among them, so
it cannot be armed by a stale setting. It requires a deliberate tile tap after
every boot.

**c. Your call, and it is a real one.** It is passive - no deauth anywhere in
that path - but capturing handshakes at DEF CON is capturing other people's
traffic. My advice: do not tap it on the floor, and know that the tile sits in
the WiFi recon cluster next to tiles you *will* be tapping during a demo. If
you plan to hand the watch to strangers, that adjacency is worth a thought.

---

## Q6. Demo mechanics

Answered from code where I could; the rest genuinely needs the hardware in
your hand and I have not guessed.

**a/b. Gesture paths and mode switching:** these need to be walked on the
device. I did not verify them from source because a swipe path read out of
LVGL code is exactly the kind of thing that is subtly wrong when you are
standing in front of someone. Rehearse it cold, as your own prompt says.

**c. Factory images:** `factory.watch.ultra.sx1262.*` vs
`factory.watch.ultra.sx1280.*` - the two differ by LoRa radio. Your unit runs
the Meshtastic path at 906.875 MHz, which is the **SX1262** sub-GHz part; the
SX1280 is 2.4 GHz. So `factory.watch.ultra.sx1262.20251219.bin` is the one that
matches. Confirm against the radio your build actually initialises before you
rely on this in a recovery situation, not during one.

**d. Screen and motion-wake under venue lighting:** not answerable from source.
Test it somewhere bright before you go.

---

## What to actually do before Aug 6

Ordered by value, not by effort.

1. **Measure battery runtime with scanning on.** The only unmeasured number
   that can end the demo. A morning of wear with the detectors you plan to run
   tells you whether "four days" means one charger or three.
2. ~~Flash this build and confirm the two behaviours.~~ **DONE 2026-07-28.**
   Flashed to COM19, hash verified, clock and wallpaper returned. Mode chip gone
   in both Defense and Offense; Offense border still draws. 3-decimal GPS
   confirmed live: a post-flash line logged GPS with 3 decimal places against
   6 decimals on the pre-flash lines in the same file. Actual coordinates not
   reproduced here - the point is the digit count, not the position.
3. **On the watch: Configuration -> Announce Node to the Mesh -> OFF.** With
   Broadcast Location already off by default, that makes ARGUS effectively
   RX-only apart from the occasional hashed TRFLAG. Not done yet.
4. **Re-wipe the six detector logs immediately before departure** (commands in
   Q4), and **move `/Wardrive/` off the card**. The 07-28 first pass does not
   cover anything written between now and Aug 6.
5. **Copy the current `firmware.elf` and `read_coredump.ps1` to the travel
   laptop.** A mismatched ELF is why Q3 took a week the first time.
6. **Set your Meshtastic long/short name deliberately** if you ever turn
   Announce back on. It goes out in the clear.
7. **Rehearse the gesture path cold**, and decide in advance whether strangers
   hold the watch.
8. **Finish the postponed AirTag route test** (`TRACKER-DETECTION-VALIDATION-HANDOFF.md`).
   Unrelated to saturation, but it is the headline feature and it is still
   unvalidated on a real tracker doing a real follow.

A synthetic-beacon saturation test is worth much less here than it looked
before this audit, because the tables are statically sized. If you want one
anyway, aim it at SD write latency and battery drain, not at RAM.

---

## Changes made in this pass

Mesh emission (Q1c):
- `src/meshtastic.cpp` - gate the automatic position reply on
  `configuration_screen_get_broadcast_location()`; forward-declare it; correct
  the ignore-path log string.
- `src/configuration_screen.h` - document what the toggle now governs.

SD retention (Q4):
- `src/geo_cell.h` - add `geo::kGpsLogDecimals` (3) with the rationale.
- `src/threat_radar.cpp`, `src/airtag.cpp`, `src/flipper.cpp`,
  `src/skimmer.cpp`, `src/evil_twin.cpp`, `src/flock.cpp` - log GPS via
  `%.*f` at that precision instead of `%.6f`; include `geo_cell.h`.

Demo UI:
- `src/theme.cpp` - disable the "DEF" / "OFF" corner chip (commented, not
  deleted). Mode is already unmistakable from the wallpaper, tool set and accent
  colour. The `argus_mode_indicator_refresh()` guard was narrowed to the frame
  pointer only; leaving it checking the now-absent chip pointers would have
  silently disabled the Offense border too.
- `src/theme.h`, `src/main.cpp` - correct the comments that described the chip
  as live behaviour.
- The Offense border frame is UNCHANGED and still draws.

Two layout bugs found on-device 2026-07-28 and fixed:

- **Settings: ~230 px dead gap with no SD card.** `settings_screen.cpp` called
  `apply_layout()` at what its comment claimed was "now that all the rows are
  registered", but 16 more rows (screenshot, wallpaper, boot, system, mode
  sections) register AFTER that call, so they kept their raw design y. The only
  other `apply_layout()` calls sit in the settings-load path behind an
  `instance.isCardReady()` early-return - so inserting a card silently repaired
  the layout on load, and without one the whole lower half stayed 230 px low.
  Fixed by moving the authoritative `apply_layout()` to the very end of the
  builder. Ruled out the `MAX_SHIFTABLE` cap as the cause first: 45 runtime
  registrations against a cap of 64.

- **Keyboard bottom row clipped by the rounded corners.** Reported on Send
  Message, but ALL EIGHT keyboards in the tree were `410` wide flush to
  `LV_ALIGN_BOTTOM_MID, 0, 0`, which puts the bottom row exactly where the
  corner radius cuts in. Includes the WiFi password and Configuration keyboards.
  Added `argus_keyboard_fit()` in `theme.cpp` with `ARGUS_KB_SAFE_W` (360) and
  `ARGUS_KB_BOTTOM_INSET` (36) in `theme.h`, and converted all eight sites.
  Per-caller height (180..240) is preserved.

  NOTE: those two constants were chosen by reasoning about the panel geometry,
  NOT measured against the real bezel radius. If a bottom row still clips, raise
  the inset in `theme.h` once rather than editing call sites.

Explicitly NOT changed, by decision:
- NodeInfo announce default stays ON in code. Silenced on-device instead
  (Configuration -> Announce Node to the Mesh -> OFF), which persists to
  `/Meshtastic/config.txt`. Keeps the public-release default matching stock
  Meshtastic behaviour.
- `wardriver_screen.cpp` and `pager.cpp` GPS precision.

Verification:
- Host suite: 172 tests, 1669 checks, 0 failures. Unchanged baseline. Note this
  does not exercise the edits themselves - every changed file is firmware-only
  and outside the host harness, so the suite proves no regression, not that the
  new behaviour is right.
- Build: SUCCESS. RAM 52.8% (173104 / 327680, 8 bytes below the pre-audit image
  - the two chip pointers). Flash 94.3% (2966605 / 3145728, 292 bytes below).
- Verified the bare `DEF` string literal is absent from `firmware.elf`, which
  confirms the chip code is genuinely out of the build rather than just hidden.
- `firmware.bin` SHA-256:
  `6C4562E1B4F2B32C4F59D1C1EEF5B93DB76AA038027A9B02854C38D3C29B90CB`

**Not flashed, and not verified on hardware.** The working tree still carries
the uncommitted tracker-detection fixes and `ARGUS_BLE_DETECT_DEBUG 1`, so this
image is a debug build. Nothing was committed or pushed. The two behavioural
changes that still need a real check on the watch:

1. A peer position request is now ignored while Broadcast Location is off, and
   answered when it is on.
2. A detection line in any of the six logs shows 3-decimal GPS.

---

## Redaction note

This file documents a privacy fix, so it must not itself become the leak. An
earlier draft quoted the owner's real home coordinates three times as evidence
that the GPS truncation worked. They were removed 2026-07-28 and never reached
git (this file has never been tracked; `git log --all -S` over the coordinate
strings returns nothing on any branch).

Rule for anything written into this repo, docs included: **never paste a real
coordinate, MAC, SSID, node id, or capture payload as evidence.** Describe the
shape of the evidence instead - "3 decimals vs 6", "a tracker MAC", "a live
node id". The claim survives; the data does not. A `scripts/pre-commit` hook
enforces this on staged content.
