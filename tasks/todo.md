# DarkHorse ARGUS Watch - Fork Plan

## >>> SESSION 2026-09-03: SLOW GPS ACQUISITION. LOG REVIEW. <<<

Reported: "It took a very long time to get a signal. I did multiple tests."

Source artifact: `artifacts/field/20260903/Settings/gpshealth.log` (pulled off
the card as `E:`, 197 records, 4 GPS power cycles). Analysis segments per power
cycle: the file appends across reboots and rail cycles, so `on=` going
backwards is the only honest segment boundary.

### What the log measures

| Cycle | Wall clock | Duration | Result | avg B/s | sats max | bad |
|---|---|---|---|---|---|---|
| 1 | 09-02 16:38-16:50 | 11m52s (partial) | already locked | 863.9 | 12 | 0 |
| 2 | 09-02 16:54-17:11 | 16m45s | NEVER LOCKED | 378.3 | 0 | 0 |
| 3 | 09-02 17:24-17:43 | 18m48s | locked at **11m17s** | 562.7 | 6 | 0 |
| 4 | 09-03 09:36-10:25 | 48m35s | NEVER LOCKED | 369.3 | 0 | 0 |

Byte rates are averaged from the cumulative `chars` counter over the whole
window, NOT from the instantaneous `bps` sample, so a main-loop stall cannot
skew them.

Measured negatives, all of which rule something out:

- **No byte loss anywhere.** `bad=0` across ~103,000 sentences over all four
  cycles. The 2048-byte RX ring is holding; this is not the 2026-08-03 failure.
- **The receiver was alive and talking in every cycle.** 369-864 B/s, always
  far above `kGpsNoDataFloorBps`. Never once classified `NoData`.
- **The rail was never cut mid-test.** `on=` climbs monotonically 0 -> 2915s
  through cycle 4, so nothing (sleep, mode change) power-cycled the GPS during
  a 48-minute run.
- **Radio coexistence is not a factor.** `wifi=0` and `lora=0` in all four
  cycles, and the ONE healthy cycle had the MOST BLE consumers (`ble=4`) while
  the 48-minute total failure had the fewest (`ble=2`).

What actually differed between cycles is how much satellite data reached the
antenna, and the byte rate shows it directly because GSV length scales with
satellites in view:

- Cycles 2 and 4 sat pinned at the empty-stream floor. Cycle 2: 378.1 B/s in
  its first half, 378.6 in its second. Cycle 4: 369.4 then 369.1. Flat for
  16m45s and 48m35s respectively - the receiver never saw anything.
- Cycle 3 climbed: 458.8 B/s average before the first satellite entered the
  fix, 639.7 after, and it was already at 421 B/s on its first 30 s sample.
  Satellites accumulated steadily and a fix followed at 11m17s with only 4-6
  satellites used. That is a marginal-sky acquisition, not a stalled one.

### FINDING 1 - the health classifier reads the wrong field (CODE-VERIFIED)

`gps_health_classify()` takes `sats` from `instance.gps.satellites`, which
TinyGPSPlus binds to **GGA field 7, "Satellites used"**
(`.pio/libdeps/twatch_ultra/TinyGPSPlus/src/TinyGPS++.cpp:277`). That is
satellites in the position solution, NOT satellites in view.

Consequences:

1. `sats == 0` is very nearly synonymous with "no fix", so the
   `NoSatellites` vs `Acquiring` split cannot carry the meaning
   `gps_health.h` claims for it. The header says NoSatellites means
   "receiver sees nothing -> sky"; the field does not support that reading.
2. The hint is therefore wrong exactly when it matters. Through the first
   8 minutes of cycle 3 the watch displayed **"No sats / Needs open sky"**
   while the byte rate was climbing 421 -> 513 B/s, i.e. the receiver was
   visibly making progress and the correct advice was "hold still, it is
   working". Cycle 3 logged 16 ticks of `state=No sats` before 7 ticks of
   `Acquiring`.
3. `Acquiring` can never be reported until GGA already carries a partial
   solution, which makes it nearly dead code on a cold start.

The stream already carries the right answer in **GSV** (satellites in view plus
per-satellite C/N0) and the firmware parses none of it - there is no
`TinyGPSCustom` anywhere in `src/`. Note the irony: the byte rate the health
log already records is a BETTER visibility proxy than the field the classifier
actually branches on.

Fix (not started): add `TinyGPSCustom` taps for GSV satellites-in-view and
C/N0, classify on in-view rather than used-in-fix, and add a
`state=Downloading` / "satellites in view, downloading almanac" case for
"in view > 0, used == 0". Host tests belong in `test/test_gps_health.cpp`
alongside the existing cases.

### FINDING 2 - cold start is real but is NOT the explanation (DEMOTED)

`POWER_GPS` off is a bare `pmu.disableBLDO1()`
(`lib/LilyGoLib/src/LilyGoWatchUltra.cpp:700`), and BLDO1 is the MIA-M10Q's
only rail on this board. If V_BCKP hangs off it, the module loses its RTC,
almanac and ephemeris on every toggle and on every `sleep()` (same file, line
757). The V_BCKP wiring is NOT verified - the only schematic in the repo is
`T-Watch-S3-Plus-GPS V1.0`, a different board. `GPS::factory()` is never called
by the app (checked), so the firmware at least is not wiping module config at
boot.

**But this does not explain the reported symptom, and the reason is prior
measured evidence.** The 2026-08-04 close-out measured a **66-second cold
start** in clear sky (see memory `argus-gps-is-reception-not-hardware`, where
slow acquisition was refuted by its own artifact). So an unretained V_BCKP
costs about a minute when there IS sky - not 11 minutes, and not the total
failure of cycles 2 and 4, where a cold-start receiver would still have brought
satellites into view and raised the byte rate. It did not.

Retaining BBR is therefore a nice-to-have worth maybe a minute of TTFF, not the
root cause. Do not spend a session on AssistNow or a rail redesign on the
strength of this log.

**Second piece of counter-evidence, which finishes it off: hot starts have
already been OBSERVED on this watch.** Both prior GPS memories record a fix at
`on=0s` with only ~200 chars processed, and the 2026-08-04 wardrive was
reconstructed as `TTFF ~0` on that basis. That means the module does retain
ephemeris across at least some power cycles, so the "V_BCKP is dead, everything
is a cold start" story is contradicted by the record. Treat Finding 2 as CLOSED
unless a log shows a consistently cold TTFF after a short off/on in known-good
sky.

This also reframes cycle 2, which was the one result that looked anomalous. If
ephemeris is retained, a restart 3m36s after a 12-satellite fix should have
produced a fix within seconds GIVEN SKY. It produced nothing, and the byte rate
never left the empty floor, so the sky is what changed. That is consistent with
the closed saga rather than a reason to reopen it - but it still rests on where
the watch physically was, which only Domenic can supply.

Instrumentation note: the file opens with `-- rotated (size cap) --`, which is
why cycle 1 is partial and its acquisition is unrecoverable. 64 KB at ~160 B
per 30 s heartbeat is ~3.4 h of continuous GPS, so any longer session loses its
own beginning - exactly the acquisition window. Same trap recorded in the
2026-08-04 memory; it bit again here.

### RESOLVED 2026-09-03 - reception, and one genuine UI defect

Domenic supplied the missing variable:

- ~1700 block: **at his mom's, then in a car driving home.**
- 0936-1025: **inside a building, plugged into the laptop.**

That maps onto the cycles exactly, and cycle 1 - which I had written off as
"already locked, uninteresting" - turns out to be the one that explains
everything:

| Cycle | Where | What the numbers say |
|---|---|---|
| 1 | arriving / inside at mom's | fix HELD while degrading: sats-used 12 -> 5, byte rate flat ~860 |
| 2 | inside at mom's, GPS re-toggled | nothing, 16m45s, byte rate pinned 378 |
| 3 | car driving home | acquired steadily, locked 11m17s, 4-6 sats used |
| 4 | inside, on the laptop | nothing, 48m35s, byte rate pinned 369 |

**Cycle 1 vs cycle 2 is the whole story, and it is textbook GNSS.** At 16:50:39
the watch was indoors holding a fix on 5-8 satellites with the byte rate still
at ~860 B/s, i.e. a full GSV of satellites in view. GPS was toggled off, then
on 3m36s later in the same building - and from a standing start it saw
absolutely nothing for 16m45s.

That is the gap between **tracking sensitivity and acquisition sensitivity**: a
receiver already locked onto a satellite can hold it far below the C/N0 it
needs to find it cold. So a fix that survives being carried indoors cannot be
re-established indoors. Nothing was broken between 16:50 and 16:54; the fix was
simply thrown away and could not be rebuilt.

Note what this makes of cycle 1's own trace: sats-used 12 -> 5 while the byte
rate never moved off ~860. In-view and used-in-fix diverged by more than a
factor of two in the same cycle. That is Finding 1's point visible in the data.

Cycles 2 and 4 are therefore expected, not defects - a watch antenna inside a
building sees nothing, and 48 minutes of it on a charger is just 48 minutes of
nothing. Cycle 3's 11m17s in a moving car (metallised windshield, arm down) is
slow but was genuine, continuous progress the entire time.

**The only real defect is Finding 1, and it is exactly the reported
complaint.** In the car the watch WAS acquiring for the full 11 minutes and
told the user it saw nothing ("No sats / Needs open sky") for the first 7.5 of
them. "It took a very long time to get a signal" is, in the one cycle where a
signal was actually available, a UI problem: the watch had the information to
say "in view, working on it" and said the opposite.

So: no firmware or hardware fault. `argus-gps-is-reception-not-hardware` stays
closed. Fix Finding 1.

Two things the GSV work should carry, both earned here:

1. Log per-satellite **C/N0**. It is the one number that would have settled
   cycle 1 vs cycle 2 outright instead of by inference from byte rate.
2. Consider warning on the GPS toggle: turning the radio off while indoors
   discards a fix that will not come back until there is sky again. Cycle 1 was
   a usable fix and cycle 2 was 17 minutes of nothing, separated only by that
   switch.

### IMPLEMENTED 2026-09-03 (host-verified + builds; NOT yet hardware-verified)

- **`src/gps_gsv.{h,cpp}` (NEW, pure, host-tested)** - GSV accumulator giving
  satellites IN VIEW and their C/N0. Deliberately ignores the numMsg/msgNum/
  numSV fields and counts DISTINCT `(talker, svid)` pairs on a TTL instead, so
  no assumption about how many talkers or signals this receiver emits can be
  wrong. Multi-signal duplicates of one satellite collapse; the same id under
  two talkers stays two satellites. Validates its own NMEA checksum rather than
  trusting TinyGPSPlus, because it keeps separate state from the same stream.
- **`gps_health_classify()` now takes `in_view` and `cno_max`**, not
  used-in-fix. Used-in-fix is no longer a parameter at all, so the inversion
  cannot come back by accident.
- **New `WeakSignal` state** - in view but the best C/N0 is under
  `kGpsCnoAcquireDb` (25 dB-Hz). Same action as NoSatellites, but a far more
  credible thing to say, and it is what indoors actually looks like.
  `Acquiring` now means what it says: in view, acquirable, no fix yet.
- **`gps_screen_pump()`** replaces `instance.gps.loop()` at the single call site
  in `main.cpp`. The library's loop() reads the port itself and left no way to
  tee the stream; this drains the port once per main-loop iteration, exactly as
  before, and feeds both parsers. Every TinyGPSPlus counter the health log
  reports is still driven by `encode()`.
- **Log line carries `view=` and `cno=`**; `tools/gpshealth_ttff.py` parses
  them and still reads pre-2026-09-03 logs (the fields are optional in the
  regex, and absent reads as None, never as a recorded zero).
- **Satellites row shows both**: "5 / 11 in view 38dB", because the two
  diverged by more than 2x inside cycle 1 and showing one hides the story.
- **Toggle warning**: switching the radio off while it held a fix sets the
  Status row to "Off - fix discarded". Put in that row rather than the header
  label, which is auto-sized and offset +60 from centre and would run off the
  right edge.

### HARDWARE-VERIFIED 2026-09-03, indoors

Flashed and confirmed on the watch. The caveat below ("NOT verified: that this
receiver's actual GSV matches what the parser expects") is now CLOSED - the
parser was written from the NMEA spec and never from a capture off this module,
and it turns out to be right:

```
view=1 cno=10  state=Weak sig
view=1 cno=13  state=Weak sig
view=1 cno=8   state=Weak sig
view=1 cno=26  state=Acquiring     <- crosses the 25 dB-Hz threshold
view=0 cno=0   state=No sats
```

- GSV parses off the real MIA-M10Q: satellites extracted, not a constant zero.
- C/N0 values are real and correctly positioned: 8-13 dB-Hz indoors with
  occasional 26 and 34. Wrong quad indexing would have surfaced elevation
  (0-90) or azimuth (0-359) values here instead.
- `WeakSignal` fires (5 ticks) and `Acquiring` fires at 26 dB-Hz. The
  25 dB-Hz threshold, chosen from physics rather than from our data,
  discriminates correctly on real signals.
- `bad=0` across 15153 sentences this cycle: `gps_screen_pump()` replacing
  `instance.gps.loop()` loses no bytes.
- `tools/gpshealth_ttff.py` reads the new fields off a real file.

### FIXED 2026-09-03: a log record that contradicted itself

Bring-up exposed a defect introduced by this very change:

```
13:41:20 MODE ... view=1 cno=34 ... state=No sats
```

`view`/`cno` were sampled fresh inside `gps_health_capture()`; `state` is the
cached `s_health_state`, recomputed once per second. On a TICK both refresh in
the same call chain and agree. MODE and OFF are emitted the instant the event
happens, so those three fields described two different moments - and a future
investigation reading that line would conclude the mode switch killed the sky.

Fixed by caching `s_health_view` / `s_health_cno` next to `s_health_state`, so
one snapshot drives both the classification and the record.

Deliberately NOT fixed by recomputing the state at capture time. The OFF record
is emitted after `gps_powered` has already gone false, so recomputing would
always classify it "Off" and destroy the one thing that record exists to
preserve: that the radio was LOCKED when the user switched it off. That is
visible in the 2026-09-02 log (`OFF ... stable=1 state=Locked`) and the
"fix discarded" warning depends on it.

### FIXED 2026-09-03: the card reader was unreachable in Daily

Reported: mounting the SD to a host required switching to Defense first.

Cause: `tools_apply_mode()` (`src/tools_screen.cpp`) hides the whole Tools grid
in Daily, and `usb_sd_screen_show()` was only reachable from a Tools tile. The
code comment there already recorded it as a known gap ("Daily gates the whole
grid, so those have no other home yet").

NOT fixed by un-gating the grid in Daily. Daily is the innocent-watch mode and
the grid's absence IS the disguise; showing it would defeat the mode's purpose.
Instead a "USB SD card reader" entry was added to Settings, which is reachable
in every mode via a BOOT press from the clock face. Mounting a card as a USB
drive is what any ordinary device does - it costs nothing to expose and needs
no hiding. Confirmed working in Daily on hardware.

Notify and LoRa APRS are in the same neutral class and remain Tools-only, on
purpose: APRS is an RF transmitter, so whether it belongs in Daily is a real
question rather than an oversight.

### Original verification status (kept for the record)

Verification status, stated precisely:
---

## >>> SESSION 2026-09-02: CLOCK 7 HOURS SLOW. THE RTC/OFFSET PAIR. <<<

Reported: face read **6:54 AM at 1:55 PM local** - 7h01m slow, i.e. a
whole-hour offset error plus about a minute of RTC drift, not an arbitrary
stale seed.

### What was actually wrong in the code (audited, not measured on the device)

The face is a PAIR - the RTC (documented UTC) plus `clock_utc_offset`
(persisted to `/Settings/timezone.txt`). Four writers, two of them broken:

| Writer | Wrote | Offset | Verdict |
|---|---|---|---|
| GPS fix (`gps_screen.cpp`) | UTC | set + persisted | OK |
| NTP (`timezone_bg_tick`) | UTC | set + persisted | OK |
| Manual Time | **LOCAL** | set to 0 **in RAM only** | BROKEN |
| Build-time fallback | **LOCAL** | never touched (stays -4) | BROKEN |

Manual Time is the one that produces exactly this symptom. `manual_time=1` is
saved to `settings.txt`; the matching `offset=0` was saved nowhere. So the next
boot restored the last DETECTED offset and applied it to an RTC holding local
time. A watch that last had a GPS fix in Las Vegas (UTC-7) and was then set by
hand in Vermont comes back up 7 hours slow. That arithmetic matches the report
exactly, but it has NOT been confirmed against the device - see "still to
check" below.

### The fix

Single invariant, enforced at every writer: **the RTC holds UTC, and whatever
writes it also records the offset that maps it back to local.**

- `src/clock_time.{h,cpp}` (NEW, pure, host-tested): `local_to_utc`,
  `utc_to_local`, `tm_utc_to_local`, `offset_plausible`,
  `effective_saved_offset`. Civil-date arithmetic, no `mktime()` - see
  lessons.md for why. Replaces the three hand-rolled copies of
  `tm_hour += off; mktime()` in main.cpp.
- Manual Time converts the entered local time to UTC and **persists the offset
  it converted with**.
- The build-time fallback peeks at the saved offset, stores UTC, and is rebased
  after `timezone_load_on_boot()` if the restored offset differs.
- `/Settings/timezone.txt` is now versioned (`offset=<h> v=2`). A v1 file read
  while Manual Time is ON is migrated to `offset=0`, which is the value the old
  firmware failed to write - so an affected card **self-heals on the first boot
  of this build**.
- One `[clock]` line on the serial console at boot: RTC, offset, manual flag,
  build-seed flag. The next report of this is measurable instead of inferred.
- `test/test_clock_time.cpp`: 7 tests, round trips over the cross-product of
  every legal offset x every hour, plus month/year/leap boundaries and the
  v1 migration table.

### RESOLVED 2026-09-02, verified on the device

After Manual Time was used to enter the correct local time on the fixed build,
and Manual Time then switched back off:

```
[tz] file offset=-4 v=2 manual=0 -> paired=-4
[clock] rtc(utc)=2026-09-02 19:29:05 utc_off=-4 manual=0 seeded=0
```

Host `date -u` read 19:29:25 about twenty seconds after that line printed, so
the RTC is within a second or two of true UTC. All four state elements agree:
RTC holds real UTC, offset is the correct -4, the card record has migrated to
`v=2`, and `manual=0` means GPS/NTP will maintain it rather than being blocked.

Note the recovery only worked because the entry ran on the FIXED build:
`clock_screen_apply_manual_time()` now converts the entered local time with the
offset in force (15:29 + 4 = 19:29 UTC). On the old build the same keystrokes
would have written 15:29 into the RTC and re-created the original defect.

### MEASURED on the device 2026-09-02 (boot log, after flashing this build)

```
[tz] file offset=-4 v=1 manual=0 -> paired=-4
[clock] rtc(utc)=2026-09-02 12:15:13 utc_off=-4 manual=0 seeded=0
```

Real local at that instant was ~15:15 EDT, so true UTC was ~19:15.

- **The offset was never wrong.** `-4` is correct for Eastern DST, and it came
  off the card, not from the compiled-in default.
- **The RTC was wrong: 12:15 against a true UTC of 19:15, i.e. 7 h behind.**
  12:15 is Eastern-minus-3, which is PACIFIC WALL CLOCK. The RTC had been
  running as a Las Vegas clock since DEF CON and nothing had re-synced it.
- Manual Time was OFF at boot, so the v1 migration did not fire and `paired`
  came back unchanged at -4.

So the face was 7 h slow because it subtracted a correct 4 from a register that
was already 3 behind. All three pre-measurement hypotheses named the wrong
half: every one of them assumed the OFFSET was wrong. The RTC was.

**This build does not repair an already-corrupted RTC** and flashing it changed
nothing on the face. It removes the writer that corrupts the RTC in the first
place (old Manual Time wrote local wall clock into a register the world clock,
Meshtastic and every log stamp read as UTC). Recovery needs a real time source:
Settings > Set Time on this build (converts with the -4 in force, so the RTC
lands on true UTC), or a WiFi/NTP sync, or a GPS fix.

### Why "turn Manual Time off and connect WiFi" did nothing

The Tools grid is hidden entirely in Daily mode (`tools_apply_mode()`,
tools_screen.cpp:2024, `default: visible = false`), and `wifi_screen_show()` is
the only path that scans and calls `WiFi.begin()`. Turning the radio on from the
Daily UI never associates, so `ARDUINO_EVENT_WIFI_STA_GOT_IP` never fires and
the NTP/geolocation worker never runs. Joining a network requires Defense mode,
with Bluetooth off (`ARGUS_RADIO_COEXIST` is not in the build flags, so
`start_scan()` refuses while the BLE controller is up).

### Known remaining gap: the watch cannot tell that its own RTC is lying

With Manual Time on, the offset is whatever was last detected, so the RTC's
"UTC" can be honestly wrong even while the face is right - the world clock and
the Meshtastic screen stay skewed. There is no UI to set the UTC offset
directly; the clean exit is to turn Manual Time OFF and let a GPS fix or a WiFi
connect set both halves. A zone picker in Settings would close it properly.

---

## >>> SESSION 2026-08-04: THE CLEAR-SKY WARDRIVE. GPS QUESTION CLOSED. <<<

A 44-minute drive, 08:37:36-09:21:53, card pulled afterwards. This is the test
the previous three sessions kept naming and could not run: "outdoors, clear sky.
If TTFF there is fast and consistent, then everything this session is reception,
and there is no hardware fault."

Card archived to `artifacts/field/20260804/` (gitignored - it is raw survey
output: real coordinates, real MACs, real detection history).

### GPS: NO HARDWARE FAULT. THE INTERMITTENT-ANTENNA HYPOTHESIS IS DEAD.

From `/Settings/gpshealth.log`, one unbroken GPS power cycle of 6919 s
(1 h 55 m) that contains the whole drive:

- `sats=12` on 43 of the 51 surviving lines, `bad=0` throughout, `bps` 1010-1142.
- `sentencesWithFix` advances at exactly 2.00/s (60 per 30 s heartbeat, GGA+RMC
  at 1 Hz). At the last good line, `fix=13802` against `on=6915s`: 6901 s of
  fixed operation out of 6915 s powered, i.e. **99.8% fix retention**, and
  `fix/2` extrapolates back through `on=0`, so TTFF for this cycle was
  effectively zero (a hot start).
- The only loss is at the very end: satellites go 12 -> 8 -> 5 -> 0 across
  09:21:30-09:22:30 and `LOST` fires at 09:22:34, which is when the drive ended
  and the watch went indoors at the destination. `bps` stays ~890-950 with
  `bad=0` while `sats=0`, the same "alive, talking into the void" signature as
  before - and here we know exactly what caused it, because it is a building.

Combined with the earlier refutations (byte loss, slow acquisition, mode, USB,
SD card), candidate (b) "intermittent antenna connection" is now refuted too:
a flaky joint does not hold 12 satellites for 115 minutes of vibration in a
moving vehicle. **It was always (a), shielding.** The 16-minute zeros of 08-03
were a watch face-down indoors; the original "in a car" failure was a
windshield plus an arm-down wrist. The firmware is innocent and always was.

The product defect that made this take four sessions is already fixed (3bbf834
explains WHY there is no fix, on screen and in the WarDrive gate). Nothing
further to build here.

INSTRUMENTATION GAP, worth knowing before relying on this log again: the 64 KB
rotation discarded everything before 08:59:57, which is precisely the
acquisition window. The TTFF number above is reconstructed from the
`fix`-counter slope, not read from a START/LOCK pair, because those lines
rotated away. A heartbeat that writes ~150 B every 30 s fills 64 KB in about
3.5 h, so any session longer than that loses its own beginning. If acquisition
timing matters again, either raise the cap or keep the first N lines of a power
cycle across rotation.

### WARDRIVE OUTPUT: CLEAN

`/Wardrive/20260804_083736.csv`, 496 rows (368 WIFI, 128 BLE), 14 columns,
WigleWifi-1.6 preamble intact.

- **0 malformed rows, 0 duplicate MACs** (496 distinct MACs in 496 rows) - the
  hash table and the CSV rewrite path are both behaving.
- **0 zero-position rows**, consistent with a GPS that never lost lock during
  the drive. Yesterday's run had 3, during a real dropout. The honest-0,0
  behaviour from 236a180 is doing exactly what it should in both directions.
- 285 distinct coordinates across 496 rows, most-repeated appearing 6 times -
  no stuck position.
- Track spans ~16.9 km N-S, ~5.6 km E-W, altitude 241-450 m. Plausible.
- 69 of the rows are first-seen in the final minute at the destination, which
  is a dense multi-SSID enterprise deployment, not a flush artifact: the
  timestamps spread across 30 distinct seconds within that minute.

### DEFECT FOUND AND FIXED: A BLANK SSID WAS NEVER BACKFILLED

`drain_queue()` wrote `rec->ssid` only on the branch that CREATES a record. The
update branch touched RSSI and position only. So whichever beacon happened to
create the record decided that AP's name for the entire session, and if that
first beacon carried no SSID the AP stayed blank no matter how many later
beacons named it.

MEASURED IMPACT, because the raw blank rate is misleading. 256 of 368 WiFi rows
(69.6%) have an empty SSID, and that number invites the conclusion that we are
losing most names. We are not. Cross-checking the two consecutive drives over
the same route: of 233 WiFi MACs present in BOTH runs, 153 agreed blank, 77
agreed named, and only **3 flipped** (2 named on 08-03 and blank on 08-04, 1 the
other way). Across all 29 archived sessions only 5 MACs are ever both. So the
~70% is overwhelmingly genuinely hidden APs, and the bug costs on the order of
1% of names, not 70%. Recording that here because the tempting version of this
finding is much bigger than the true one.

Fixed anyway - it is three lines and strictly correct. Fill a blank from a
later frame; never overwrite a name we already have, so a malformed or spoofed
beacon cannot rewrite survey data. Detection logic is unaffected:
`evil_twin_check()` runs on the raw beacon, not on the stored record.

VERIFIED: firmware builds, RAM 53.3% / flash 94.6% - byte-identical footprint to
the previous build. Host suite 4414 checks / 0 failures.

### DETECTORS ON A REAL DRIVE

`threat_log.txt` is `<boot_sec> <Category> <from>-><to>` and concatenates every
boot, so it MUST be segmented on boot_sec resets before any rate is computed.
It holds 5580 lines across 26 boot sessions; today is the last one.

**The tracker strobe fix is confirmed in the field, across sessions in one
file.** Airtag threat-level transitions per boot, with the count that occurred
within 2 s of the previous one:

  session   duration   transitions   sub-2s
  seg 6      29086 s        806        382
  seg 20      4666 s        415        162
  seg 21      9120 s         53          7
  seg 22     21033 s         22          1
  seg 23     30646 s         18          0
  seg 25      6572 s          2          0   <- today

That is e60a24f and 236a180 landing and holding. Today the tracker indicator
moved twice in 110 minutes and never sub-2 s.

During the drive window itself: RogueAp 50 transitions (24 raise/clear cycles,
median 22 s apart, levels 1-2), BeaconFlood 8, Surveillance 3, Airtag 0.

THREE FALSE-POSITIVE MECHANISMS WORTH KNOWING BEFORE DEF CON. All three are
"correct code, wrong conclusion" - none is a crash, and none is urgent:

1. **Evil Twin fires on shared default SSIDs.** Both 08-03 and 08-04 flagged
   SSID "STARLINK", same ROGUE BSSID on both days but a DIFFERENT "LEGIT"
   BSSID each day. Two unrelated Starlink terminals broadcasting the vendor
   default is not an evil twin, and whichever one we saw first becomes "legit".
   A con floor is full of shared defaults.
2. **BeaconFlood fires on dense enterprise APs.** All 8 transitions came in the
   final 90 seconds, on arrival at a site with many virtual BSSIDs per radio.
   That is a real beacon rate, just not an attack. A con floor is worse.
3. **The tail detector called a LIKELY Vehicle tail.** A globally-unique (not
   randomized) BLE MAC, 3 waypoints, 5669 m span, 12 min dwell, RSSI -47.
   RSSI -47 sustained over 5.7 km means the device was almost certainly INSIDE
   the car - the user's own phone, a passenger's, or the vehicle itself. This
   is the expected failure mode of tail detection while wardriving in a car,
   and it is what CounterTail's familiar list exists to absorb (one familiar
   entry was written today).

None of these is fixed. Logging them as known behaviour rather than opening
work, because each needs a design decision about false-positive tolerance that
should not be made unilaterally the week of the con.

---

## >>> SESSION 2026-08-03b: GPS never locked / WarDrive unreachable (BUILT, PENDING FLASH) <<<

User report: "I didn't get to WarDrive, GPS never kicked on, this is the second
time that has happened." Conditions: outdoors but under cover / in a car. The
user could not recall what the GPS screen's Satellites row had shown.

WHAT THE ARTIFACTS SAY (not what was assumed). `/Settings/bledetect.log` on the
SD logs every GPS cell transition, so time-to-first-fix is reconstructable for
all 27 recorded sessions. GPS is NOT dead: it locked in 9-30 s in the early
short sessions, but the recent ones are 527 s, 792 s, 1539 s, 3309 s, 8266 s,
and two sessions never locked at all. The most recent session (ended 20:34,
99 min long) took 3309 s to first fix. So both reported failures are sessions
SHORTER than the time-to-first-fix, not a dead radio.

WarDrive is downstream, not broken: `wardriver_screen.cpp:505` hard-gates START
on `gps_screen_has_stable_lock()`, so no fix means the readiness dialog refuses
the tap. Fixing GPS fixes WarDrive.

TWO CANDIDATE ROOT CAUSES, and nothing on the device could tell them apart:
1. NMEA byte loss. GPS is 38400 8N1 (3840 B/s) and `instance.gps.loop()` is
   pumped once per main-loop iteration (`main.cpp:2486`). The ESP32 core
   defaults HardwareSerial to a 256-byte RX ring = 67 ms of NMEA, while this
   firmware's own comments record iterations of "hundreds of ms" under
   wardriver load and a ~250 ms block in `start_wardriving()`. Every overflow
   corrupts a sentence, TinyGPSPlus rejects it on checksum, and with enough
   loss a module that HAS a fix never lands a parseable one inside
   `gps_fresh()`'s 5 s window. Reads on screen as "never locked".
2. Genuine RF cold start. A wrist GPS under cover / in a car may simply not
   resolve, and t=3309 s was just when the sky opened up.

Both are consistent with the evidence. Guessing between them is how this
recurs, so this change fixes (1) - which is unambiguously wrong regardless -
and instruments the discriminator so the next run answers it from an artifact.

FIX
- `src/gps_screen.cpp` `gps_uart_up()` (new): the single sanctioned
  power-on ordering, `Serial1.end()` -> `setRxBufferSize(2048)` ->
  `powerControl(POWER_GPS, true)`. `setRxBufferSize()` is a no-op while the
  driver is installed, so it MUST sit in that window. 2048 B = 533 ms of
  tolerance, covering the documented stalls. Deliberately not larger: the ring
  is internal DRAM and this build is sensitive to the largest contiguous
  internal block (tasks/COEXIST-NOTES.md). Replaces the duplicated end/power
  sequence in `on_toggle()` and `gps_screen_restore_power()`.
- `src/gps_screen.cpp` GPS HEALTH block (new): samples TinyGPSPlus's existing
  `charsProcessed` / `passedChecksum` / `failedChecksum` / `sentencesWithFix`
  counters, all baselined per power cycle. Two new GPS-screen rows (NMEA byte
  rate, ok/bad checksums) and an SD log at `/Settings/gpshealth.log`, written
  on START, on every stable-lock LOCK/LOST edge, and on a 30 s heartbeat.
  Rotates at 64 KB. Carries NO position data, so it is safe to quote whole.

HOW TO READ THE NEXT FAILURE, from `/Settings/gpshealth.log`:
- `bps=0`                          -> no NMEA at all: rail, UART or wiring.
- `bps` healthy, `bad` climbing    -> byte loss. Firmware. Raise the ring.
- `bps` healthy, `bad` flat,
  `sats>0`, `fix` flat             -> module fine, no sky. Not firmware.

VERIFIED: firmware builds (RAM 52.9%, flash 94.5%); host suite 4343 checks /
0 failures.

### FIELD RESULT 2026-08-03 17:17-17:24 - BYTE LOSS IS REFUTED

Flashed to the watch, ran 6 min indoors, read `/Settings/gpshealth.log`.

Cause (1), NMEA byte loss, is DEAD. Across 6731 good sentences the failed-
checksum count never moved off 1 (that one is the partial sentence at power-on),
and the byte rate held steady at 805-936 B/s the entire run. The UART, the rail
and the RX ring are all healthy. The 2 KB ring stays as correct hardening - 256
bytes really is only 67 ms - but it is NOT the fix, and it should not be
described as one.

The immediate indoor lock does NOT validate the change either: the first log
line shows a fix at on=0s with only 231 characters processed. That is a hot
start on ephemeris retained from the fix ~2 h earlier, and it would have
happened on the old firmware too.

THE ACTUAL SIGNATURE. Satellites decay 6,6,7,4,6,5,8,3,3,4,4 and then collapse
to 0, staying 0 for the last 90 s, while `bps` holds ~820 and `fix`
(sentencesWithFix) freezes at 395. So the receiver is powered, running and
streaming NMEA at full rate while tracking NOTHING. That is an RF/reception
failure, not a firmware one. Candidate mechanisms, in order of testability:
  a. 2.4 GHz desense from radios brought up alongside (BLE active scanning and
     WiFi both transmit, not just receive).
  b. BLDO1 sag under the added PMU load, which would cost RF sensitivity while
     leaving the module's digital core happily streaming.
  c. Coincidence: body/wrist position or moving deeper indoors.

The user's account is that this followed a Daily -> Defense switch, but the log
carried no mode or radio state, so that correlation rests on recollection. That
is the same gap that made the original failures undiagnosable.

### INSTRUMENTATION ROUND 2 (BUILT, PENDING FLASH)

Every health line now carries `mode=` `ble=` `wifi=` `lora=` (via
`argus_mode_current()`, `ble_scan_consumer_count()`,
`wifi_radio_screen_is_powered()`, `lora_screen_is_powered()`), and a mode change
writes an immediate `MODE` line rather than waiting up to 30 s for the next
heartbeat. If satellites fall as those columns come up, it is (a) or (b); if
they fall independently of them, it is (c).

### FIELD RESULT ROUND 2 - THE FAILURE IS BINARY, AND SLOW ACQUISITION IS OUT

Three sessions now, all on the round-1 build (no mode/radio columns yet):

  s  window        len    max sats  lines w/ any sat  bytes/sentence
  1  17:17-17:33   15 min     8            11             46.6
  2  17:33-17:51   18 min     8            36             51.7
  3  17:54-18:10   16 min     0             0             30.8

Session 2 is a clean bill of health and the most useful line in the table: a
COLD start indoors (0 sats at 30 s, 3 at 60 s, locked at 66 s) then 6-8
satellites held for 18 unbroken minutes at ~900 B/s with bad=0. Indoor
reception at that location is fine, and the receiver, UART and ring are all
sound.

Session 3, three minutes later, saw ZERO satellites for 16 minutes - not one
line with a single satellite. Its low byte rate is a CONSEQUENCE, not a cause:
sentences shrank 51.7 -> 30.8 bytes and the rate fell 17.6 -> 12.0/s, which is
exactly what a receiver emits with nothing to report (empty GGA/RMC fields, a
GSV reporting nothing in view). Alive, talking into the void.

So the original "slow to lock" framing is wrong too. This is not slow
acquisition. It is a total loss of reception that persists until something is
reset, and the 55-minute TTFF was almost certainly this same condition ending.

WHAT THE ARTIFACTS FIX IN TIME (everything else is inference, and was not
recorded):
- Flash completed just before 17:17:49 (first health line; the log only exists
  in the new firmware), so USB was connected at ~17:17.
- A 541 s hole in session 1's 30 s heartbeat, 17:24:01 -> 17:33:02 WITHIN one
  GPS power cycle (on=371s -> on=912s), is the card being out of the watch
  while it was read in the reader.
- USB was connected again around 17:29-17:33 (app CDC enumerated as COM20).
- USB is disconnected now.
Nothing recorded the plug state during sessions 2 and 3. Note this cuts against
a simple "USB noise did it" story: USB was connected right at session 2's
start, and session 2 was healthy.

### INSTRUMENTATION ROUND 3 (BUILT, PENDING FLASH)

Every health line now carries `mode=` `ble=` `wifi=` `lora=` `usb=` `chg=`, and
a mode change writes an immediate `MODE` line instead of waiting up to 30 s.
`usb=`/`chg=` come straight from `instance.pmu.isVbusIn()/isCharging()`. This
exists because the plug state could not be reconstructed after the fact, and
the watch is necessarily on USB for every flash, so USB contaminates exactly
the runs taken right after one.

### A/B RESULT 2026-08-03 18:51-20:27 - MODE AND USB BOTH REFUTED

95 minutes, one unbroken GPS power cycle, round-3 build. User-reported switch
times (20:05 Defense, 20:15 Offense) match the logged MODE lines to the second,
so the device clock is sound.

  mode      lines   satellites
  daily     146     137 lines at 12, 9 at 11
  defense    23      22 lines at 12, 1 at 10
  offense    24      24 lines at 12

`lock=1 stable=1` on 193 of 193 lines. Zero LOST events. `bad=0` throughout.
USB went 1 -> 0 at 19:55:05 and satellites sat at 12 on both sides of it.

So the mode switch does nothing, and USB does nothing. Together with the earlier
refutation of byte loss, every firmware-side hypothesis is now dead:
  - NMEA byte loss / RX ring   REFUTED (bad=0 across ~93000 sentences)
  - slow acquisition           REFUTED (66 s cold start; 12 sats for 95 min)
  - Daily/Defense/Offense      REFUTED (no effect on any transition)
  - USB charging noise         REFUTED (healthy plugged AND unplugged)

WHAT IS LEFT is the one variable never recorded: physical reception. The dead
runs are not degraded, they are absolute - 16 minutes of exactly zero
satellites, receiver streaming a clean, checksum-valid sentence set that says
"I see nothing". Two candidates remain, and they are NOT the same problem:
  a. Shielding. Face-down on a desk, under something, in a bag, or the original
     report's "in a car" - a metallised windshield plus an arm-down wrist is a
     near-perfect GPS block. This is physics, and the firmware is innocent.
  b. An intermittent antenna connection. The healthy/dead/healthy/dead
     alternation across today's sessions is also the classic signature of a
     flaky solder joint or connector, and that would be a HARDWARE fault worth
     knowing about before DEF CON.

THE REAL PRODUCT DEFECT, and it is ours either way: the user cannot tell (a)
from (b) from a genuine failure. WarDrive answers a tap with a bare red X next
to "GPS lock" and no account of why, and the GPS screen shows "--" identically
for "no sky", "still acquiring" and "receiver dead". That is what turned a
reception problem into three sessions of debugging.

### NEW REPORT: "IT TAKES LONGER TO GET A GPS SIGNAL ONCE THE SD CARD IS IN"

Reported 2026-08-03 after the Status row was confirmed working. Taken
seriously: SD cards are aggressive broadband EMI sources, SD clock harmonics
near GPS L1 (1575.42 MHz) are a known problem in GPS+SD designs this small, and
write-current spikes are a second path to the same symptom. It also fits the
ORIGINAL complaint better than anything else so far, because wardriving is the
one feature that needs the card and a fix AT THE SAME TIME.

THE INSTRUMENT REQUIRED THE THING IT MEASURES. The health log lives ON the SD
card, so it could only ever observe the card-IN condition. Every session
analysed today had the card in, because otherwise there was no log to read.
Worse, the 30 s heartbeat ADDED SD writes that were not there before, so the
instrument perturbs the effect under test.

FIX - RAM BACKLOG (BUILT + HOST-TESTED, PENDING FLASH):
- `GpsBacklog` in `src/gps_health.h`: pure index-math ring (head/count/dropped),
  storage owned by the caller, so it is host-testable. Aggregate-initialised at
  definition in gps_screen.cpp because `slot()` takes a modulo by capacity and
  must never see 0.
- `src/gps_screen.cpp`: records are now CAPTURED whether or not a card is
  present and buffered in RAM when it is not; `gps_health_emit()` drains the
  backlog oldest-first the moment a card appears. main.cpp already polls
  card-detect and hot-mounts, so inserting the card mid-run writes the card-OUT
  history retroactively with its original timestamps. A `card=` column now says
  which condition each line was captured under.
- Overflow keeps the NEWEST and reports `dropped` in the flush marker rather
  than truncating silently: a backlog that quietly lost its head would misdate
  an acquisition and send the next investigation the wrong way.
- 48 records x 32 bytes = 1.5 KB static .bss (does not fragment the heap the
  coexist work is sensitive to), ~24 min at the 30 s heartbeat.
- `test/test_gps_health.cpp`: 5 ring cases - order when not full, exactly full,
  overflow keeps newest with a correct dropped count, wrap after a flush (the
  off-by-one that would silently corrupt ordering), and zero-capacity safety.

VERIFIED: firmware builds (RAM 53.3%, flash 94.6%); host suite 4414/0.

RESULT 2026-08-04, ON BATTERY, THREE ROUNDS - NOT SUPPORTED.

Measured from the log rather than a stopwatch (`card=` column, START -> LOCK):

  round   card OUT   card IN    favours the hypothesis?
  1        48 s       289 s      yes
  2       125 s        32 s      NO
  3       168 s       777 s      yes

  card out: 48-168 s     card in: 32-777 s

Round 2's card-IN run locked faster than EVERY card-out run in the set. The
ranges overlap and the overlap is not marginal, so three samples per arm with
one clean contradiction does not establish an SD effect.

An interim note here claimed "8-10x, and it runs against the confound". That
was built on round 1 alone and is withdrawn: round 1 was reported by hand, and
round 2 - the sample that could falsify it - was not. Read the log, not the
stopwatch.

WHAT THE DATA DOES SHOW: acquisition here is dominated by RECEPTION and swings
from 32 s to 13 min largely regardless of the card. The failure mode is
identical every time - zero satellites for minutes, a slow climb to 3, stuck
there (4 are needed), then a fix the moment the 4th arrives. The 777 s run:
8 min at sats=0 with bps ~370-460 and bad=0, sats=3 at 05:30, sats=4 and LOCK
at 05:34:30. Byte rate tracks satellite count throughout (more GSV data), which
is a good internal consistency check on the instrument itself.

DECISION: do NOT build more for the card question. Settling it against that
variance needs 8+ samples per arm in a fixed position, which is a lot of user
time for a hypothesis the data does not favour. The backlog stays regardless -
it closed a real blind spot and is the only reason card-out data exists.

THE TEST THAT ACTUALLY MATTERS, STILL NOT RUN: outdoors, clear sky. If TTFF
there is fast and consistent, then everything this session - the 16-minute
zeros of 08-03, tonight's 13-minute crawl, and the original in-a-car failure -
is reception, and there is no hardware fault. If it is STILL slow under open
sky, that is the intermittent-antenna signal and it matters before DEF CON.

### DEFECT IN THE STATUS DWELL, FOUND BY THE EXPERIMENT ITSELF (FIXED)

The Status row's duration measured TIME IN THE CURRENT STATE and reset on every
state change. With satellites flickering 0 -> 1 -> 0 the classification flaps
NoSatellites <-> Acquiring, so the counter kept zeroing: a two-minute failure
displayed as a timer that never climbed. Reported as "the time reset".

Now measures TIME WITHOUT A FIX (`s_nofix_since_ms`), reset only on an actual
stable lock, so it climbs monotonically through a failure and answers the
question actually being asked. The state label and the duration are now
independent, which is what they should always have been. Same accessor, so the
WarDrive dialog inherits the fix.

Incidentally confirms the receiver was intermittently SEEING a satellite during
Arm B rather than sitting at a flat zero - a different picture from the
16-minute absolute zeros of 2026-08-03.

### RELEASE HYGIENE: THE 20 INHERITED UPSTREAM SCREENSHOTS ARE GONE

`img/*.bmp` (20 files, 8.7 MB) were r3dfish's screenshots. Verified referenced
by NOTHING: the README gallery uses `img/argus/*.png` throughout, and a
repo-wide `git grep` finds no other user (the only `.bmp` hits are the
screenshot FORMAT described in README and unrelated LVGL examples).

Deleted rather than shipped with the previous session's black-box redactions:
only 4 of the 20 were ever audited, the pre-commit hook cannot see inside
images, and republishing someone else's captures under DarkHorse branding is
the same act commit aae5d58 already declined for their mesh packets.
Attribution in README and LICENSE is untouched.

NOTE: the hook then blocked the README's own redaction. Zeroing the digits
still leaves a 4-decimal coordinate PAIR, which is exactly what the rule
matches, so a redaction that keeps the shape does not clear it. Replaced with
`<lat>, <lon>` placeholders, which describe the badge format without pasting
anything coordinate-shaped. The hook was right twice (it caught this note
quoting the offending string too) and was not bypassed.

### THE FIX: MAKE THE FAILURE LEGIBLE (BUILT + HOST-TESTED, PENDING FLASH)

- `src/gps_health.h` (new, pure, C++11, no Arduino/LVGL): `GpsHealth` =
  Off / NoData / NoSatellites / Acquiring / Locked, `gps_health_classify()`,
  `gps_health_label()`, `gps_health_hint()`, `gps_health_duration()`.
  Two ordering rules carry the weight, and both are pinned by tests:
  - NoData is tested BEFORE NoSatellites. A silent link also reports zero
    satellites, so the naive order tells a user with a dead receiver to go
    outside. The link is the more specific and more actionable answer.
  - NoData must PERSIST `kGpsNoDataDwellSec` (5 s) before it is believed. The
    byte rate is legitimately 0 on the first sample of a power cycle, so
    reporting instantly would flash a hardware fault at every power-on.
  The byte floor is 20 B/s, deliberately near zero: the question is "is it
  saying anything", not "is it saying much" (a receiver seeing nothing still
  emits ~370 B/s of empty sentences).
- `test/test_gps_health.cpp` (new): 11 cases on the boundaries between
  explanations. Suite 4343 -> 4387 checks, 0 failures.
- `src/gps_screen.{h,cpp}`: `gps_screen_health()` / `gps_screen_health_secs()`,
  classified once per 1 Hz tick. New FIRST row on the GPS screen, "Status",
  showing e.g. `No sats 3m12s`. `state=` added to every health-log line.
- `src/wardriver_screen.cpp`: the readiness dialog's GPS line now carries the
  reason and the dwell, e.g. `X GPS lock` + `Needs open sky (3m12s)` instead of
  a bare red X.

VERIFIED: firmware builds (RAM 52.9%, flash 94.6%); host suite 4387/0.
UNVERIFIED until flashed: the on-screen rendering and the dialog layout.

STILL OPEN, and NOT a firmware question: whether the dead runs were shielding
or an intermittent antenna. Reproduce a zero-satellite state with CLEAR SKY
overhead. If that reproduces, it is hardware, and worth knowing before DEF CON.

## >>> SESSION 2026-08-03: charging indicator (BUILT + host-tested, PENDING FLASH) <<<

User report: "when charging you have no idea, so an indicator to let you know
that the battery is charging."

ROOT CAUSE (not a missing feature; an invisible one). `update_battery()` in
main.cpp already appended `LV_SYMBOL_CHARGE` to the battery label while
`pmu.isCharging()` was true. That symbol is U+F0E7. `bat_label` is styled with
`font_dh_mono_16`, a VT323 subset whose single cmap covers only U+0020..U+007E
(`src/font_dh_mono_16.c:572`), and its `lv_font_t` never sets `.fallback`.
LVGL found no glyph and drew nothing, so the charge state has been rendering
as a trailing space since the font was swapped in. Nothing was wrong with the
PMU read.

FIX
- `src/charge_state.h` (new, pure, C++11): `ChargeState` =
  Discharging / Charging / Topped, `charge_state_raw()`, and `ChargeIndicator`,
  a debouncer fed one sample per 1 Hz tick.
  - Three states, not two: `isCharging()` goes false the moment the AXP2101
    terminates, so a watch sitting full ON the charger looked identical to one
    running on the cell. `Topped` = `isVbusIn() && !isCharging()`.
  - Asymmetric debounce. Plug-in publishes immediately (that is the edge the
    user is waiting on). Unplug holds 2 ticks (cable chatter on the pogo pins).
    Charging <-> Topped holds 5 ticks, because near termination the PMU hunts
    CC -> CV -> DONE -> CC and flips `isCharging()` every couple of seconds.
    Publishing that raw would strobe the bolt, the same defect class as the
    tracker threat-level strobe (b98cea8, e60a24f).
- `src/main.cpp`: new `bat_bolt`, a 4-point lightning zigzag drawn with
  `lv_line` inside the battery body at x=3..13, stroked white at width 2.
  Primitives, not a font glyph, for the reason above and matching the existing
  timer/stopwatch icons. White specifically, NOT `ARGUS_ACCENT` /
  `status_accent_active()`: those flip to threat-red on a tail, and a battery
  bolt is chrome, not a live-threat surface.
  - Hidden when discharging; steady at full opacity when Topped; alternates
    full <-> 40% line_opa once per 1 Hz tick while Charging. Pulse is driven
    from the existing status tick, so no new timer and no new wake source.
    Deliberately a slow breathe rather than a hard on/off blink, which on this
    watch face reads as fault/alert.
  - `line_opa` on the line itself, NOT `opa` on a wrapper: `opa` < 255 on a
    parent makes LVGL composite the subtree through an intermediate layer
    every redraw.
  - `bat_charge.prime()` at the end of setup so a watch booted already on the
    charger shows the bolt on the first frame.
- `src/settings_screen.cpp`: System Info now reads `chg` / `full` / (nothing)
  off the same `charge_state_raw()`, so the text readout cannot disagree with
  the bolt. That path was always correct (plain ASCII), just three-state now.

VERIFIED
- Host suite: 11 new cases in `test/test_charge_state.cpp`. 1840 checks,
  0 failures. The CC/CV/DONE hunt and the cable-chatter cases specifically
  assert the bolt does NOT strobe.
- `pio run -e twatch_ultra` SUCCESS.
- Size delta measured against a stashed baseline build, since flash sits at
  94.3%: +616 B flash (2967037 -> 2967653), +48 B RAM. Both percentages
  unchanged (flash 94.3%, RAM 52.8%).
- Bolt polyline rasterized offline: reads as a clean lightning bolt at 10x16,
  and cannot collide with the digits (VT323 16 is monospace at 6 px/glyph, so
  the widest reading "100%" spans x=16..40 inside the 56 px inner width).

HARDWARE CONFIRMED 2026-08-03
- Flashed over the USB-Serial-JTAG port (303A:1001), hash verified. NOTE: at
  least one OTHER ESP32-S3 is attached to this machine and registers a stale
  COM port that Windows lists as present but esptool cannot open. The ghost's
  port number and MAC both DRIFT between sessions, so never hard-code either.
  If an upload fails with "port doesn't exist", that is the wrong device, not a
  broken watch. Identify the watch by the `pio device list` LOCATION field: the
  live device has one, the ghost does not. Confirm with a read-only
  `esptool.py ... flash_id` before writing.
- After upload the app CDC (303A:8227) did not re-enumerate and stayed on the
  JTAG identity through `--after hard_reset`. It came back on its own; an
  RTS-driven reset does not physically detach USB, so the host keeps the old
  enumeration cached. Not a boot failure - do not chase it as one.
- User confirmed on the watch: clock + wallpaper in Daily mode, and the charging
  bolt visible and pulsing while on the charger at 87%. The three-state design
  is doing its job; steady-when-topped has not been observed yet.

## >>> WARDRIVE VERIFICATION 2026-08-03: b98cea8 CONFIRMED on real data <<<

Audited `/Wardrive/20260803_083714.csv` from that morning's commute session
(08:37:44 -> 09:33:04 local, 697 rows, 541 WiFi / 156 BLE). This is the field
verification `tasks/TRACKER-DETECTION-VALIDATION-HANDOFF.md` was waiting on for
the position-stamping half of b98cea8.

- **Old-bug signature GONE.** The pre-fix CSV had ZERO unknown-position rows
  across 26 GPS dropouts, because `gps.location.isValid()` kept handing back
  stale coordinates. This file has 3 honest `0,0` rows.
- **The dropout is visible and correctly shaped.** AccuracyMeters blows past
  150 m at 08:59:12-08:59:18 (HDOP degrading), then the stable lock drops and
  rows go `0,0` at 08:59:25-08:59:32. Degrade-then-drop, ~20 s, exactly what a
  tunnel or parking structure looks like. The old code would have stamped
  last-known coordinates straight through it.
- **Invariant holds exactly:** acc == 0 if and only if position is 0,0. Zero
  violations in either direction.
- **HDOP floor respected:** no positioned row below 5.0 m. Distribution 49.9%
  at 5 m, 46.9% 5-15 m, 2.3% 15-50 m, 0.4% >150 m.
- **Movement is real, not frozen:** 358 distinct latitudes over a ~16.8 x 5.5 km
  box. A stale-coordinate bug would show clamped/repeating positions.
- **Dedup intact:** 697 rows, 697 distinct MACs, 0 duplicates.
- Cross-check: the 09:24 photo showed 331 WiFi / 95 BT mid-session; the finished
  file holds 541/156. Consistent with a session still running at that moment.

Blank-SSID rows re-examined (was 168, now 380 of 541 = 70%, high enough to
re-check): still benign. 371 of 380 carry `[WPA2-PSK-CCMP][ESS]`, so they are
infrastructure beacons, not misfiled client frames - the ESS capability bit is
what `wifi_beacon_manager` filters on. 216 of 380 have the locally-administered
bit set, which is how multi-BSSID APs derive virtual BSSIDs for hidden/backhaul
SSIDs. Conclusion from the b98cea8 commit message stands; no action.

STILL UNVERIFIED: the tracker/threat-decay half of b98cea8 (kSlowDecaySec=90),
and the charge bolt's steady "Topped" state. If the bolt ever flickers between
pulsing and steady near full, raise `CHARGE_SETTLE_TICKS` in `src/charge_state.h`.

## >>> SESSION 2026-07-24: exact panic captured and fixed <<<
- Two exact matching core dumps were captured after the diagnostic flash. Both
  show `IllegalInstruction` in the same chain:
  `loop -> LilyGoUltra::loop -> SensorBHI260AP::update ->
  bhy2_get_and_process_fifo -> parse_fifo ->
  BoschParseStatic::parseData -> corrupt indirect target`.
  The invalid PCs changed from `0x3065A54A` to `0x3025A54A`, while the valid
  callback object remained `instance.sensor` at `0x3FCC1894`.
- The SD phase log proves this happens before the first `loop` marker and before
  `wp-defer`, so it is the previously-known first-boot sensor loop, not a crash
  inside the raster read/render itself.
- Hardware A/B: Wallpaper ON + "Motion brightens screen" OFF survived 12-15+
  battery boots. Turning Motion back ON eventually restored the boot loop and
  produced the second identical BHI260 dump.
- Backtrace-driven fix keeps the persisted Motion feature enabled, but defers
  starting its 10 Hz BHI260 stream until 15 s after boot, after the wallpaper
  render/current window.
- HARDWARE CONFIRMED: 12-15+ battery cold boots with Wallpaper and "Motion
  brightens screen" both ON passed. Motion brightened the screen after the
  15-second defer, and Daily/Defense/Offense wallpaper transitions all passed.
- Cleanup complete: current-raster-only loading remains; the disproven 4 KB
  bounce buffer and temporary `/Settings/bootlog.txt` phase writes were removed.
  Flash core-dump support and `tools/read_coredump.ps1` remain permanently.
- Final cleaned release build passed. `firmware.bin` SHA256:
  `C9B5620D6477774EC10381E05CDB6DE2477B6DCACF5E966EAB49303CF6A282B2`.
- CLEANUP REGRESSION: the first cleaned build required many attempts to pass the
  ARGUS boot screen. After clearing the stale dump slot, a new exact dump matched
  that ELF and reproduced the same BHI260 chain and invalid target
  `0x3065A54A`. This independently proves the removed wallpaper bounce buffer
  was not on the failing path.
- Follow-up fix: SensorLib's `BoschParseStatic` callbacks now make qualified,
  non-virtual calls to `SensorBHI260AP` instead of reading the failing indirect
  target from the object vtable. `scripts/patch_sensorlib.py` applies the guarded
  dependency patch reproducibly. The resulting firmware was flashed and
  byte-verified; SHA256:
  `9A605B854AAD843021FDF409CB22A6652FCFFE96A115450AE5491B38DCD75785`.
  HARDWARE CONFIRMED: 12-15 battery cold boots with Wallpaper and Motion both ON
  passed without another loop.
- Confirmed the stock Arduino-ESP32 S3 SDK already has ELF core dumps to flash
  enabled, and the board partition table reserves 64 KB at `0xFF0000`.
- Preserved the old on-device dump before flashing:
  `artifacts/coredump/ondevice-before-diagnostic.raw.bin`. Its embedded application
  SHA (`c58b2d62419e622f`) does not match the surviving local ELF
  (`0ca9155b8c7b96bf`), so exact source-line decoding is intentionally refused.
  Direct inspection found a corrupted execution PC `0x3065A54A` and return address
  `0x821B688A`. A provisional map puts the return in the BHI260/SensorLib region,
  but this is NOT an exact root-cause result without the matching old ELF.
- Rebuilt and flashed the current-raster-only diagnostic firmware on COM19. An
  esptool `verify-flash` digest check passed against the current
  `.pio/build/twatch_ultra/firmware.bin`, so the next dump has an exact matching
  `firmware.elf`.
- Hardened `tools/read_coredump.ps1`: it reads the fixed partition directly,
  preserves raw + core ELF artifacts, verifies the flashed app, and stops on an
  ELF/SHA mismatch instead of printing plausible but false source lines.
## >>> SESSION 2026-07-24: coexist attempt + Daily/Offense UI fixes <<<
Context: watch boots on battery with wallpaper OFF (wallpaper=0 on SD). Worked through
BLE+WiFi+LoRa coexistence, then a batch of UI fixes.

- **BLE+WiFi+LoRa COEXISTENCE — ATTEMPTED, FELL BACK (guards restored).** Added master flag
  `src/radio_coexist.h` (`ARGUS_RADIO_COEXIST`). 1 = boot BLE keepalive + all mutual-exclusion
  guards `#if !ARGUS_RADIO_COEXIST`-gated out (manager guards in wifi_beacon_manager/offense_wifi
  AND the UI pre-checks in wifi_radio_screen on_toggle+restore_power / wifi_screen start_scan);
  0 = guards active. ON HARDWARE: boots fine, but tapping WiFi with BLE up HARD-FREEZES (even
  with NO phone connected), and WiFi-enable-at-boot BOOT-LOOPS. The WiFi+BLE hang is real and
  baseline on our build. **Domenic: the hang predates the notifications feature** — so ANCS is
  NOT the culprit (corrected in memory). Flag set back to **0**; all scaffolding preserved for a
  future deeper attempt (measure contiguous internal SRAM, trim resident footprint / diff vs
  upstream r3dfish). Full journey in memory `reference-simultaneous-ble-wifi-lora`.
- **TIME HUB ICONS RED IN OFFENSE (fixed, confirmed).** `tt_recolor_red` only set bg/border/line;
  the 6 clock tiles are `/Icons/*.png` sprites (confirmed on SD) so they ignored those. Added
  `lv_obj_set_style_image_recolor` + `_opa(COVER)` so sprites tint solid red in Offense (matches
  Tools), natural in Daily/Defense. Domenic: "Icons are red."
- **BOOT BUTTON RESPONSIVENESS (fixed, confirmed).** (a) Knock (L-S-L) now armed ONLY on the
  clock screen — elsewhere BOOT runs its back/home action immediately, so an accidental long-ish
  tap can't buffer a stray [L] that eats the next press. (b) Added ISR-latch recovery: the FALLING
  `back_btn_pressed` ISR now recovers a fast tap the duration-poll dropped to loop latency (as a
  SHORT press; 150ms cooldown suppresses bounce). Factored the dispatch into `boot_dispatch()`.
  Domenic: "button feels responsive."
- **NOTIFY LAUNCHER IN DAILY (flashed 2026-07-24, awaiting on-hardware confirm).** Daily had no
  way to reach notifications (only the Tools "Notify" tile, gated in Daily). Added a "Notify" tile
  to the Time hub (`notifications_screen_show()`), procedural gold-bell icon (no SD sprite needed),
  bumped `s_ttiles[9]->[10]`. Time hub is now 10 tiles.
- **WALLPAPER: fade-render FIXED, but an INTERMITTENT PANIC is STILL OPEN (unsolved end of session).**
  On battery only, wallpaper-on, the watch intermittently PANICS (reset=4) / INT_WDT (reset=5) at the
  ~10-12s load/render and boot-loops. Left boot-looping - recover via pull-SD or wallpaper=0. Disproven:
  bounce buffer, load-in-setup (made it worse). Leading theory: power/PSRAM glitch on battery. NEXT: get
  the real backtrace (ESP core-dump / USB-Serial-JTAG console), revert load-all-3. See argusprompt.txt.
  The fade-render fix itself WORKED:
  it re-rendered the full 410x502 image ~15x in <1s -> INT_WDT on battery. Dropped the fade
  (render once at bg_opa in background.cpp apply_current). Kept the 10s defer + PSRAM preload.
  Added a "wallpaper applied ms=" bootlog marker. Per-mode wallpapers now render + HOLD on all
  three modes. Enable via Settings -> Wallpaper toggle.
- **MODE LANDING (confirmed correct, NO change):** Offense -> Tools screen (PIN-gated, high-intent,
  land on the arsenal); Defense -> wallpaper/home (ambient posture). Already works this way.
- **STILL OPEN:** (1) first-boot-after-RESET loop — loops once on the 1st reset then boots fine;
  SEPARATE from the wallpaper (happens with wallpaper OFF too). Diagnose via the logged reset=
  codes in /Settings/bootlog.txt (9=brownout vs 5/6=watchdog) — needs SD in laptop. (2) the deeper
  BLE+WiFi+LoRa coexistence investigation (parked, scaffolding preserved behind ARGUS_RADIO_COEXIST=0).
- Flash/test loop: download mode = COM19 (303A:1001), app CDC = COM20 (303A:8227). SD was in the
  laptop during the notify flash — **must be reinserted** before testing (clock icon sprites live
  on it).

---

## >>> DARKHORSEIFY ROUND 6 (2026-07-21) <<<
Fixed the heading/boot-logo cream regression (28 titles -> argus_accent(); boot splash ->
ARGUS_ACCENT). Then user: proceed with scroll perf (1) + 48px fonts (2); also "Time icons
need DarkHorseifying" (3).
- TIME ICONS (3): added alarm/stopwatch/timer/calendar glyphs to gen_icon_sprites.py (steel-
  blue), rendered + copied to SD /Icons/. time_screen.cpp: upgraded make_tile to the Tools
  gradient+rim style, added tile_icon() SD-load-w/-procedural-fallback, wired the 4 tiles.
- 48px FONTS (2): generated SUBSET fonts (tiny flash) - font_dh_mono_48 (VT323, digits/colon
  "0123456789:.- ") for alarm/stopwatch/timer big readouts; font_dh_label_48 (Orbitron,
  "START STOP") for wardriver button. Remaining montserrat_48 = 3 SYMBOL icons (kept).
- SCROLL PERF (1): on_grid_scroll cb on tools_grid calls main_loop_request_lvgl_priority(8)
  each LV_EVENT_SCROLL, topping up the priority window so background ticks don't stutter the
  flick. (Primary lever = scheduling, matches "lags behind". If still laggy after flash, the
  throughput lever is lighter tiles / opaque icons.)
- Flash ~91% (subset 48px fonts are ~8KB total). Icons on SD = 0 flash.
- NEXT: flash; user confirms scroll + Time icons + 48px displays. Custom watchface still pending.

---

## >>> DARKHORSEIFY ROUND 5 (2026-07-21, flashed) <<<
User: Send Message not branded; ALL Tools/Time subpages need brand everywhere except heading;
Tools scroll lags ("RAM issue?"). Viewed 5 submenu photos (wifi/wifi-pw/analyze/hexhound/pager
- mostly good already).
- SEND MESSAGE: its title was montserrat_36 (not Bank Gothic) - fixed to font_dh_ui. Body
  already Orbitron.
- COMPREHENSIVE FONT SWEEP: prior sweep only hit montserrat_20. Generated Orbitron 14/16/28
  (font_dh_label_14/16/28); tools/fontify_all.py migrated 109 more body labels across 28 files,
  symbol-safe (11 protected) + keyboard-safe (kb keys are symbols) + matrix_bg excluded.
  48px (9 uses: START btn, stopwatch/timer digits) DEFERRED - flash-heavy at 48px.
- Flash now 90.7% (2852405/3145728). ~293KB free. Deferring 48px fonts protects budget.
- SCROLL LAG = NOT RAM (61.8% int, PSRAM has room). Cause: (1) partial-refresh display shares
  ONE main loop between LVGL + background work via a priority system (main_loop_request_lvgl_
  priority) - background work mid-scroll makes animation "lag behind"; (2) 16 transparent icon
  images alpha-blended per frame. FIX LEVERS (offered, not done - own flash to measure): stronger
  LVGL priority window during active scroll; pause matrix rain while Tools scrolls; lighten tiles
  (opaque icons / drop gradient).
- STILL OPEN: 48px brand fonts; scroll perf pass; custom watchface (mockup, awaiting sign-off);
  wire surveillance detector to live scan (hardware-gated).

---

## >>> DARKHORSEIFY ROUND 4 (2026-07-20, flashed) <<<
Round 3 approved on hardware ("looks good"). This round:
- CLOCK BIGGER: added lv_font_montserrat_clock_120 (Bank Gothic); CLOCK_TEXT_PAD_X 16->4 +
  CLOCK_LETTER_SPACE -3; the size selector measures WITH that spacing so 120px is chosen
  only when it fits (overflow-safe). Width-limited: "00:00" at 120px = ~400px of 402 usable.
- BRAND FONT EXTENSION: tools/fontify_labels.py migrated 112 montserrat_20 labels ->
  font_dh_label_20 (Orbitron) across 29 files. SYMBOL-SAFE: skips any label var that ever
  holds LV_SYMBOL (8 protected: wifi/bt/gps/sd indicators, flock_icon, wardriver eye,
  threat_radar head). theme.h auto-added. Verified no symbol var got a brand font.
- Flash now 89.9% (2826513/3145728) - WATCH THE BUDGET; baking more fonts into flash is
  getting tight. SD-loaded assets (icons) remain the right call for anything big.
- NEXT: user hardware check (watch for any vanished status icon = missed symbol label).
  Still pending: custom watchface (mockup awaiting sign-off); VT323 readouts on more
  numeric fields if wanted.

---

## >>> DARKHORSEIFY ROUND 3 (2026-07-20, on-hardware iteration) <<<
Wardriver now STARTS on hardware (PSRAM fix confirmed). This round from user feedback:
- WARDRIVER doesn't STOP when pressed: stop_wardriving() (SD flush + WiFi.mode(WIFI_OFF))
  ran BEFORE the button label update, so the tap looked ignored. FIX: flip UI to START +
  lv_refr_now FIRST, then teardown (mirrors the start optimistic-flip).
- Secondary text COLOR: user preferred white/cream, NOT the steel-blue accent I applied.
  ARGUS_TEXT/ARGUS_TEXT_DIM (cream) in theme.h; reverted 192 text_color spots.
- CLOCK LARGER: lv_font_montserrat_clock_110 (Bank Gothic) prepended to the adaptive array.
- BRAND FONTS (user: "don't make everything Bank Gothic"): Bank Gothic = wordmark/clock/
  titles; Orbitron = labels (font_dh_label_20 -> tile labels/date/time tiles); VT323 =
  readouts (font_dh_mono_16 -> battery/mesh). OFL TTFs fetched to tools/brandfonts/.
  Watch-face symbol labels left in Montserrat (icons). See memory darkhorse-font-system.
- NEXT: build (in progress) + flash; confirm on hardware.

---

## >>> DARKHORSEIFY PASS (2026-07-20, differentiate from 13-37) <<<
Directive: "how does this differ from r3dfish/13-37"; DarkHorse-brand the whole UI.
User chose the high-fidelity option for each: custom HD sprites, full custom
watchface, Start-button explains-why.

FINDINGS (ground truth):
- argus-watch is a strict SUPERSET of 13-37 at source (every 13-37 file + ~30 added).
  Tools grid = 16 tiles vs 13-37's 8. "Tools they have we don't" is NOT true in code.
  -> user to name the specific missing tools (likely stale flashed build or the main
     swipe-menu radios vs the Tools grid).
- Time screen: only 5 lines differ from stock 13-37. Essentially untouched.
- Tools screen: 390 lines differ but tile ICONS are still 13-37's procedural rainbow.
- Wardriver Start: gated behind GPS-lock + SD-ready + radio; silent dead tap when unmet.

TASKS:
- [x] Wardriver Start fix: button always clickable, action-level gate, HADES-red dialog
      naming missing GPS/SD/radio. (wardriver_screen.cpp) BUILD OK.
- [x] Text rebrand: already branded (prior 27-title sweep + mesh name); swapped the last
      user-facing "1337" matrix egg -> "REDTEAM". BUILD OK.
- [x] Custom HD tool-icon sprites: tools/gen_icon_sprites.py -> transparent glyph+glow
      PNGs (SD /Icons/*.png), 14 sprites. Tesla=user Tesla logo(amber), flock=user birds
      (red), pager=13-37 green procedural, flipper+pet kept. Deploy folder staged at
      tools/icon_out/Icons/. Wired via tile_icon() sprite-or-procedural-fallback;
      make_tile upgraded (dark gradient + steel rim). BUILD OK (flash 88.2%, zero cost).
- [~] Full custom watchface: MOCKUP rendered both states (tools/gen_watchface_mockup.py
      -> icon_out/watchface_mockup.png). AWAITING USER SIGN-OFF before editing time_screen.cpp
      (primary screen; time_screen is boot-crash-prone per memory - do not rush).
- [x] Surveillance-device detector (Meta Ray-Ban/body cam/hidden cam/action cam/trackers):
      src/detect/surveillance_device.* + ThreatDomain::Surveillance + threat_map routing +
      16 tests. Host suite 158/1601 checks 0 fail (verified). Pure/unwired; hardware-gated
      integration documented in src/detect/README.md. See memory surveillance-device-detector.
- Port scanner: RECOMMEND keep nested under WiFi (scan a picked host; standalone = manual
  IP entry, bad UX). Map/nav: lean keep under Meshtastic; offered a standalone Map tile.
- NEXT: on watchface sign-off, implement it; fold in detector when subagent lands; then
  compile-verify; hand hardware flash to user (never flash time_screen changes AFK).

---

## >>> DARKHORSEIFY ROUND 2 (2026-07-20, on-hardware iteration) <<<
Confirmed NEW firmware runs on watch (Start button now flips STOP->START = my code path;
old firmware couldn't). "Nothing looks different" earlier = /Icons wasn't on the SD yet.

- WARDRIVER ROOT CAUSE FOUND + FIXED: not the readiness gate. start_wardriving() returned
  false silently (flip STOP then revert). Cause: ap_table heap_caps_calloc of WD_BUCKETS
  (32768) * sizeof(ApRecord ~144B) = ~4.5 MB PSRAM, which FAILS because ARGUS added a 2 MB
  LVGL cache 13-37 lacks. FIX: WD_BUCKETS 32768->16384 (~2.36 MB, load factor 0.61) so it
  fits; AND surface the failure reason via low_mem_show_dialog (s_wd_start_err: "Out of
  memory" vs "WiFi could not start / turn Bluetooth off") instead of the silent revert.
- CLOCK FACE -> BANK GOTHIC: regenerated lv_font_montserrat_clock_{56,72,96}.c from
  a local commercial font file (not in this repo) via gen_clock_font.py (now
  argv-driven). Kept the var names = drop-in, no main.cpp change.
- SECONDARY TEXT -> DARKHORSE ACCENT: tools/accentify_secondary_text.py rewrote 179
  neutral-grey text_color calls across 32 files (bright greys->ARGUS_ACCENT, dim->
  ARGUS_ACCENT_DIM); only text_color, only neutral greys (semantic red/orange/green kept);
  theme.h auto-added where missing (0 files left missing).
- Icons: /Icons copied to the SD card (drive G:). 13 PNGs (pager stays 13-37 procedural).
- Build: (in progress) then flash from download mode (watch present, user driving).

---

## >>> SESSION REPORT (2026-07-20, DES-70072, ALL user-verified on hardware) <<<
Long session, ended with "Everything is working, survived a reset, everything
survives on battery." SHIPPED + VERIFIED:
- WiFi/BLE FREEZE FIX (the original ask): symmetric pre-call coexistence guard so no
  radio toggle can freeze. ble_scan_add refuses if WiFi up (new ble_scan_last_error);
  wifi_beacon start_wifi + wifi_radio_screen on_toggle refuse if BLE controller up;
  guards run BEFORE the hanging WiFi.mode()/esp_bt_controller_enable() call. Tools
  tiles show a "turn off the other radio" dialog instead of a dead tile.
- All-off-at-boot + Settings "Enable at boot" chooser (WiFi/Bluetooth/LoRa/GPS; NFC
  removed; WiFi<->BLE mutually exclusive w/ warning). boot_prefs.* + main.cpp gating.
- Wallpaper: renders again (removed a fragile free-RAM guard check), 100KB compressed
  file-size guard added (background.cpp) so a too-big image can't crash boot, and the
  boot sequence now DECODES THE WALLPAPER BEFORE bringing up radios (main.cpp reorder)
  so wallpaper + a boot radio don't OOM the internal SRAM.
- Privacy wallpaper: re-encoded 147KB->72.5KB palette PNG (Pillow, adaptive 256-color),
  set as /backgrounds/wallpaper.png. Now the default and boots fine w/ radios on.
- Rearrangeable Tools grid (long-press-drag, /Settings/tools_order.txt) + Flock single-
  radio notice: both user-verified. Dialog OK button enlarged. Settings shiftable-row
  cap 32->64 (fixed the boot-row dead-gap).
- BLE-at-boot WORKS (verified on battery). My repeated "it loops" calls were ALL wrong
  (confounds). See memory ble-keepalive-boot-loops (RESOLVED).

KEY LESSON (memory usb-power-sd-brownout-bootloop): the watch boot-loops with the SD in
ONLY on USB power (brownout); FINE on battery. TEST ON BATTERY. Flash with SD out or via
download mode. Also: opening the CDC serial port RESETS the ESP32-S3 (looks like a loop).

FOLLOW-UP WORK (all FLASHED + user-verified on hardware):
- [ram] boot diagnostic REMOVED from low_mem_check() (+ esp_bt.h include dropped).
- WiFi threat pipeline ACTIVATED, PIGGYBACK policy (ARGUS_WIFI_THREAT_PIPELINE=1):
  detect_pipeline_tick attaches its beacon consumer ONLY while another WiFi scan
  (Evil Twin/Flock/Pwn/wardriver) is already running, detaches when none. Never powers
  WiFi on itself, never flips STA->monitor, nothing on the boot path. Added
  wifi_beacon_consumer_count(). Domenic chose piggyback over "on WiFi toggle".
- HexHound HD ART (Domenic loved it): pet + Tools-tile icon now use HD sprite assets
  (5 stages) generated by tools/gen_hexhound_sprites.py, loaded from SD /HexHound/*.png,
  replacing the procedural LVGL shapes. pet_screen.cpp update_sprite() per stage;
  tools_screen draw_pet_icon() -> pup_icon.png. Zero flash cost. See memory
  hexhound-hd-art. Wallpaper+radio boot ORDERING fixed too (decode wallpaper before
  radios or OOM). BLE-at-boot CONFIRMED WORKING on battery.

STILL OPEN (need input or a focused effort):
- BLE-side detectors (ble_spam, tracker_ident, tail_detect): mirror the WiFi piggyback
  pattern via a BLE detect pipeline on ble_scan_add; wire ONE at a time, extend the host
  e2e test first, flash + verify each (per src/detect/README.md). Focused multi-flash job.
- "Other screens glitching": BLOCKED - Domenic said SKIP for now (need which screens + how).
- SLIM DAILY-WEAR VERSION (Domenic wants a thinner watch he'd actually wear daily; 2026-07-20).
  Physics: full ARGUS (WiFi + BLE + SX1262 sub-GHz + GPS/NFC + battery) can't be dress-watch
  thin - the radios/antennas/battery need volume. So the plan is a REDUCED build, not a full
  thin port. Two paths to scope/spec later:
  (1) Custom ESP32-S3 + SX1262 build (keeps full features via the same silicon, only modestly
      slimmer with a smarter PCB/battery; firmware ports w/ a new HAL layer, pure logic reused).
  (2) Slim BLE-focused companion for daily wear (BLE anti-stalking: AirTag/Find My, Flipper,
      skimmer, ble_spam, tracker/tail + HexHound pet + WiFi SCAN list; NO monitor mode, NO
      sub-GHz). Target board options: nRF52 (PineTime/Bangle.js, BLE-only, full rewrite) or a
      thin ESP32-S3 color watch (T-Watch S3 / Open-SmartWatch, keeps WiFi+BLE, no SX1262), or a
      Wear OS / phone companion app. Pairs with the T-Watch Ultra as the "field tool" for the
      RF work. The pure detect/ + hexhound C++ is platform-agnostic and reusable either way.
  ACTION when picked up: Domenic chooses path (1) vs (2) + a target board, then spec the HAL/
  feature subset. TODO: sketch the slim-version feature list + board pick (offered, deferred).
Nothing committed this session (local only, per Domenic - see memory keep-argus-local-no-fork).

## >>> MORNING REPORT (overnight 2026-07-19/20) <<<
Worked autonomously as instructed. HARD CONSTRAINT honored: I cannot physically
power-cycle the watch, so I could not on-device-verify the FINAL flash boots - I
kept the boot/setup path minimal-change, made all new code runtime-guarded, and
host-tested everything. If the morning boot ever loops: BOOT+RST to stable
download mode, then reflash the prior good commit (ebe9918 = last user-confirmed
"it is working" build).

WHAT I FLASHED (final build = last user-confirmed-working commit ebe9918 + only
DORMANT/low-risk additions, so boot behavior is unchanged from what you confirmed):
- Clock-slow FIXED (image cache) - USER-CONFIRMED before I flashed it.
- Low-memory warning toast: lazy runtime overlay, only appears if internal free
  RAM < ~18KB, auto-dismiss 5s, once/min. Never at setup. (cc8e341, threshold
  tuned down after). Tune the threshold once you know normal free RAM.
- Temporary timing diagnostic removed.
- Watch is in DOWNLOAD MODE after the flash (black screen) - power-cycle (hold
  crown) to boot the new build. If it ever loops: BOOT+RST -> reflash ebe9918.

DONE-IN-CODE but GATED OFF (committed, NOT active in the flash):
- WiFi threat pipeline: evil-twin + beacon-flood -> aggregator -> HADES-red accent
  + forensic log (/Settings/threat_log.txt), portMUX-guarded, boot-safe. (cecbbd2)
  GATED OFF (#define ARGUS_WIFI_THREAT_PIPELINE 0) because activating it POWERS the
  WiFi radio ~1s post-boot and holds it all session (bypassing your WiFi toggle,
  draining battery) - an unverified behavior change. To enable: decide attach policy
  (passive piggyback on your WiFi vs opt-in setting), flip the flag, verify on-device.
  The BLE-side detectors still need their (boot-loop-prone) BLE bring-up verified too.

DEFERRED (with honest reasons - no shortcuts means not shipping unverifiable risk):
- AM/PM smaller: crashed boot (span/font at setup); needs on-device iteration.
- BLE-side detectors (ble_spam/tracker/tail) + deauth: require ble_scan_add /
  promiscuous which brings up the BLE controller = the boot-loop risk. Must be
  wired + verified WITH you, one at a time (see src/detect/README.md).
- HexHound visual evolution: pet is procedural; HexHound repo has no sprite art.
  Needs an art/design pass (sprites or a per-stage procedural redesign) - can't
  fake "quality" autonomously.
- "Other screens glitching": need specifics (which screens). Likely partial-
  refresh artifacts (I reverted full-refresh to fix the WiFi ghost); each screen
  may need a targeted fix like the clock got (drop transforms, right invalidation).
- Bluetooth toggle proper fix: async off-thread BLE bring-up, a careful session.

## >>> RETURN BRIEFING (2026-07-19 late session) <<<
The watch is RUNNING the recovered build (ghost fixed, WiFi good, adaptive-font
clock). Committed since that flash but NOT yet flashed: clock-slow DIAG
instrumentation + smaller AM/PM. To pick them up: from stable download mode
(hold BOOT, tap RST, hold BOOT ~2s) I reflash the current build, you power-cycle.

1. CLOCK TICKS BY 3-4s (only with SD inserted; fine without it). Not the wardriver
   (30s cadence) nor USB-MSC (hidden). Instrumented (commit): with a card in,
   read serial for a "[slow] <call> Nms" line - it names the exact blocking call,
   then I fix it and remove the DIAG. THIS is the first thing to do on return.
2. AM/PM is now a smaller font (separate span). Verify it looks right.
3. BACKGROUNDS: resized 410x502 PNGs are in
   a local argus-backgrounds-410x502\ folder (not in this repo) (DarkHorse/HADES/Privacy).
   Copy these into the SD /backgrounds and DELETE the 1242x1242 originals (too big).
   wallpaper.png (= the Privacy image) is the auto-default find_wallpaper() loads.
   Stock-baked-into-firmware deferred (flash 86.4%; do later as a compressed PNG).
4. BLUETOOTH toggle: use BT-FIRST (toggle BT on before WiFi). Auto BLE keepalive
   boot-loops (see [[ble-keepalive-boot-loops]]); proper async fix = fresh session.
5. Boot-loop lesson: NEVER flash while the watch is mid-boot-loop (incomplete
   flash). Force stable download mode (BOOT+RST) first, then flash.

---


Base: fork of `r3dfish/13-37` (upstream remote), branch `argus-argus`, LOCAL ONLY (no push yet).
Goal: take the T-Watch Ultra to the next level for cybersecurity red/blue team,
while keeping it a full watch (clock/alarms/timer/calendar). Bring ARGUS's
engineering rigor (testable modules + host unit tests) and DarkHorse/HADES
branding to the proven 13-37 base; cherry-pick Threat Radar (MIT) features.

## Phase 0 — Baseline — DONE (commit 8afd220)
- [x] Clone r3dfish/13-37, remote `upstream`, branch `argus-argus`
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
- [x] BLE-spam / adv-flood detector: pure src/detect/ble_spam.* (composes with
      src/ble adv parser) + 9 host tests. Build verified. (commit dbf5be2)
- [x] Threat-state aggregator: pure src/detect/threat_state.* - unified posture
      (Calm/Watch/Alert/Critical) with rise/decay hysteresis + correlation
      escalation + 14 host tests. Build verified. (commit 0186624)
- [x] Detector->aggregator mapping layer: src/detect/threat_map.* (severity_of +
      feed() per detector) + 8 host tests. Build verified. (commit a4dc0a2)
- [x] Subsystem architecture + integration guide: src/detect/README.md.

DETECTION SUBSYSTEM COMPLETE: parser -> 5 detectors -> map -> aggregator, all
pure/host-tested/decoupled and compiling into the firmware. Host suite: 102
tests / 944 checks green. See src/detect/README.md for the data flow + the
step-by-step (hardware-gated) integration guide.

Approach (per feedback [[feedback-keep-building-when-afk]]): keep producing
host-tested/additive work while AFK; do NOT idle waiting on integration. No
experimental flashing without the user present.
- [x] WiFi beacon-flood / fake-AP-spam detector: src/detect/beacon_flood.* + 9
      tests, wired into aggregator as ThreatDomain::BeaconFlood. (570afd3, 4c8205a)
- [x] Coarse GPS-to-cell quantizer src/geo_cell.* (~120 m, configurable) so
      tail_detect has real cell ids + 8 tests. (commit 0d60d4d)
- [x] Unwanted-tracker (AirTag / Find My) ident src/detect/tracker_ident.* +
      Airtag wiring (feed_tracker -> ThreatDomain::Airtag). (commit 669c8ca)
- [x] Forensic threat log src/detect/threat_log.* (edge-recorded, 48-event ring,
      SD-append documented) - Phase 4 blue-team. (commit c12ffb8)

## Rendering - display FULL-refresh (commit 7c5a85b) - FLASHED + user-confirmed
Flipped LilyGoLib LilyGo_Display(full_refresh=true) (Ultra already has full-screen
PSRAM buffers, so zero extra memory). Kills the whole partial-refresh artifact
class: clock jump under Matrix AND wallpaper, and Tools-scroll stale rows. Verified
on-device: Matrix+clock stable, Tools smooth, wallpaper smooth.

## Radio persistence (commit 602dc1c) - built, PENDING FLASH CHECK
WiFi/NFC/LoRa power state now persists across reboot (GPS pattern, /Settings/*.txt).
Bluetooth deliberately EXCLUDED (BLE-at-boot boot-looped). Verify: toggle each on ->
reboot -> should return AND boot clean (LoRa brings the SX1262 fully live at boot).

## Full module inventory (pure, host-tested; parser/detectors UNWIRED to scan loop)
  src/ble/adv_parser  BLE AD parser | src/image_dims  wallpaper guard | src/geo_cell  GPS->cell
  detect/: evil_twin->RogueAp  tail_detect->Tail  deauth_flood->DeauthFlood
           ble_spam->BleSpam  beacon_flood->BeaconFlood  tracker_ident->Airtag
           threat_map (verdict->Severity)  threat_state (->ThreatLevel)  threat_log (history)
  test/test_subsystem_e2e  end-to-end composition proof (real inputs through the chain)
Host suite: ~142 tests / 1541 checks green. See src/detect/README.md for data flow
+ the step-by-step integration guide. The e2e test already caught 2 real cross-
module bugs (log all-domain polling; geo_cell must be non-negative) - extend it
BEFORE wiring hardware integration; cheaper than a flash cycle.

## ON YOUR RETURN — briefing
1. FLASH once + verify boot (bundled pending on-device changes):
   - Radio persistence (602dc1c): toggle WiFi/NFC/LoRa on -> reboot -> return + clean boot.
   NOTE: the watch currently runs the full-refresh build (7c5a85b, flashed +
   confirmed) but NOT yet radio persistence / the latest modules - reflash to get them.
   - Already flashed + confirmed this session: full-refresh (7c5a85b), wallpaper OOM
     guard (9205bc7) + auto-/backgrounds (6005ef9), clock fix (d31e7a8).
2. INTEGRATION (hardware-gated; wire one, flash, verify, then next - do NOT batch blind):
   - BLE adv parser -> refactor airtag/flipper/skimmer detectors.
   - evil_twin + beacon_flood -> feed from wifi_beacon_manager (beacon path).
   - deauth_flood -> promiscuous mgmt-frame path (subtype 0xC/0xA, addr3).
   - ble_spam + tracker_ident -> feed from ble_scan_manager adv results.
   - tail_detect -> BLE/wifi sightings keyed by geo::coarse_cell(GNSS); tracker_ident
     hits feed a 2nd TailDetector -> feed_tracker() (Airtag domain).
   - threat_map::feed*() -> threat_state; drive argus_accent()+HexHound; threat_log
     records escalations to /Settings/threat_log.txt.
3. Open feature question: phone notifications over BLE (iPhone/ANCS first). Discussed;
   sequence after integration since it leans on the BLE stack.

## Session commits (argus-argus, LOCAL only, none pushed)
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
