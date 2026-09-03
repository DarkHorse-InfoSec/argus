# ARGUS Defensive / Counter-Surveillance Tools — Implementation Plan

**Board:** LILYGO T-Watch Ultra (ESP32-S3, 16MB flash, 8MB PSRAM), PlatformIO env `twatch_ultra`, LVGL 9.5, Arduino.
**Scope:** 10 defensive counter-surveillance tools + duress safety. All are Defense-tier (hidden in Daily, shown in Defense/Offense via the existing `tile_mode()` fall-through in `src/tools_screen.cpp:1531`).
**Authority:** This plan honours the Hardware-Capability Matrix below. Do NOT plan or build a DROPPED tool. Every hardware claim is cited to real source; do not contradict it without re-auditing the variant definition and `LilyGoLib`.

---

## 1. Feasibility Summary (capability matrix — authoritative)

**Acoustic status up front:** The PDM MEMS mic is REAL and initialized (`variants/lilygo_twatch_ultra/pins_arduino.h`: `MIC_SCK=17`, `MIC_DAT=18`, `USING_PDM_MICROPHONE`; brought up in `LilyGoWatchUltra.cpp:1189 initMicrophone()` at 16 kHz, `I2SClass mic`). **The rate is fixed at 16 kHz (`PDM.h`: "can only be up to 16KHZ and cannot be changed"), so Nyquist = 8 kHz.** Consequence: **ultrasonic-beacon detection is INFEASIBLE (DROPPED)** — near-ultrasonic tracking beacons live at 18-22 kHz, physically above the 8 kHz ceiling. **Acoustic drone detection is DEGRADED** (blade/motor harmonics partly fall under 8 kHz, but it is research-grade DSP with no mic array / no direction) and is NOT planned as a shipping tool in any tranche.

| # | Tool | Verdict | Buildable now? | Basis / gap |
|---|------|---------|----------------|-------------|
| 1 | RF bug sweep (sub-GHz) | **FEASIBLE** | Now | SX1262 RSSI sweep primitive built: `lora_analyze_screen.cpp:203 radio.getRSSI(false)`, `:226 setFrequency` hop. Sub-GHz ISM only. |
| 2 | Faraday-bag verifier | **FEASIBLE** | Now | WiFi RSSI + BLE scan + `radio.getRSSI()` + GPS sat count all exist. Must sequence WiFi/BLE (exclusive). |
| 3 | Self-surveillance audit | **FEASIBLE** | Needs raw-mgmt hook | Promiscuous capture exists (`analyze_screen.cpp:65-96`); needs probe-req (0x40) fan-out. |
| 4 | WIDS (active-attack detect) | **FEASIBLE** | Needs raw-mgmt hook | `DeauthFloodDetector`, `RogueApDetector` built in `src/detect/`; needs raw mgmt-frame delivery. |
| 5 | NFC reader-field detection | **FEASIBLE** | Now | `NFCReader.rfalIsExtFieldOn()` live register read (`rfal_rfst25r3916.cpp:2420`). |
| 6 | Universal tracker sweep + time-correlation | **FEASIBLE** | Now | `tracker_ident` + `surveillance_device.classify_ble` + `threat_radar.cpp` correlation engine all built. |
| 7 | GPS-jamming detection | **DEGRADED** | Needs new low-level work | Needs GSV `TinyGPSCustom` per-sat C/N0 parsing (not wired). No UBX jam indicator. |
| 8 | Personal-safety duress (IMU) | **FEASIBLE** | Now (needs on-wrist tuning) | Raw BHI260 accel streamed at `main.cpp:928`; LoRa TX via `aprs.cpp:260` / `meshtastic.h:30`. |
| 9 | RF hidden-camera fingerprint | **FEASIBLE** | Now | `surveillance_device.classify_wifi/classify_ble` (`surveillance_device.h:114`) built, passive. |
| 10 | TSCM "sweep this room" meta-mode | **FEASIBLE (minus ultrasonic)** | After 1/5/9 | Orchestrates 1+5+9. Radio sequencing is the integration risk. |
| — | Ultrasonic-beacon detection | **DROPPED** | Never | 8 kHz Nyquist < 18-22 kHz beacons. Physically unreachable. |
| — | Acoustic drone detection | **DEGRADED / not planned** | — | Audible-band harmonics only, hard DSP, no direction, noisy. Not a shipping tool. |
| — | IMSI-catcher / Stingray | **DROPPED** | Never | No cellular baseband. |
| — | Thermal / laser mic | **DROPPED** | Never | No IR sensor; laser-mic infeasible. |

---

## 2. Shared Framework (P0 — build first, everything depends on it)

Three primitives every tool below plugs into. Build and host-test these before any tool.

### 2a. `DetectorRegistry` (replaces the two `#if ARGUS_*_THREAT_PIPELINE` blocks)
Replaces the bespoke glue at `main.cpp:2258 detect_pipeline_tick()` / `:2261 ble_detect_pipeline_tick()`.
- `Detector` base: `{ const char* key; ArgusMode min_mode; void on_tick(uint32_t now_sec, ArgusMode mode); }`.
- Detectors self-register at setup; `registry_tick(now_sec, argus_mode_current())` is called ONCE from the existing `!lvgl_priority` 1 Hz slot (`main.cpp:2149`/`2167`).
- **Daily-suppression is enforced centrally in the registry** — it passes `mode` and silences any UI-raising detector when `mode==Daily` (detector may still run for the ThreatLog). Policy lives here, not in each detector. Mirrors `tile_mode()` tiering.
- **Effort: S. Host-testable (pure), like `device_mode_plan()`.**

### 2b. `RadioScheduler` — exclusive-radio lease arbiter (the backbone)
The central integration problem is WiFi↔BLE mutual exclusion: `ble_scan_manager.cpp:49,111` refuses BLE bring-up when `wifi_is_active()` because `esp_bt_controller_enable()` **HANGS** (hard freeze, reset required) with WiFi holding SRAM.
- API: `radio_request(owner_id, RadioSet, Mode{Piggyback|Exclusive}, priority) -> Granted|Denied|Queued`; `radio_release(owner_id)`.
- Radios: WiFi + BLE (mutually exclusive); LoRa (SX1262), GNSS (UART), NFC (SPI 13.56 MHz), mic — independent single-owner rails, may sample concurrently with whichever of WiFi/BLE owns SRAM.
- **Passive** detectors request `Piggyback`: granted read-only ride-along IFF a base scan already runs (wraps existing `wifi_beacon_consumer_count()` / `ble_scan_consumer_count()`); NEVER powers a radio up alone.
- **Active** sweeps request `Exclusive`: to grant BLE the scheduler drives WiFi fully down first (**reuse the `wifi_is_active()` guard VERBATIM — do not reimplement**), runs the leg, releases, then grants WiFi. A multi-radio sweep is a *sequence* of leases.
- **Effort: decision core M (pure, host-testable); actuation/sequencing + on-wrist latency tuning L.** This is the highest-risk framework piece.

### 2c. Raw-mgmt-frame fan-out (WiFi capture prerequisite for #3, #4)
`wifi_beacon_manager.cpp:31` drops every non-beacon: `if ((frame[0] & 0xFC) != 0x80) return;`. Deauth (0xC0), disassoc (0xA0), probe-req (0x40) are received but discarded before any consumer sees them.
- Add `wifi_mgmt_add(cb)` delivering `MgmtFrame{const uint8_t* payload, int len, int8_t rssi, uint8_t ch}` for **every** mgmt frame, fanned out from `promisc_cb` alongside the existing beacon parse. IDF filter is already `WIFI_PROMIS_FILTER_MASK_MGMT` (`wifi_beacon_manager.cpp:138`), so all mgmt frames already arrive. Mirrors the existing `s_data_capture` DATA fan-out (`:111-114`).
- **Effort: S — but touches the shared capture core; verify it does not perturb the beacon survey or the handshake DATA path.**

### 2d. Tile + gating contract (every tool, identical shape)
1. **Tile+gating:** add `make_tile(grid, "...")` and a `{tile, "<key>"}` entry to `tile_keys[]` (`tools_screen.cpp:1404-1421`). `tile_mode()` fall-through returns `ArgusMode::Defense` for any unlisted key (`:1538`) — **zero gating-code change** to be Defense-tier.
2. **Loop hook:** implement `Detector::on_tick`, register once; registry calls at 1 Hz. Modal sweeps use their own `lv_timer` (mirror `lora_analyze`'s `HOP_MS`) and need NO 1 Hz hook.
3. **Radio:** call `radio_request()` before touching any radio. Fold verdicts into shared `ThreatState` via `detect::feed()` under `s_mux` (`detect_pipeline.cpp:37`) — **never call `argus_set_threat()` directly.**
4. **Daily suppression:** any alert popup / haptic / TX must be guarded by `argus_mode_current() != ArgusMode::Daily`.

---

## 3. TSCM Bundling Decision

**Net +2 tiles (Faraday, TSCM). TSCM ORCHESTRATES existing tiles; it does NOT absorb them.**

| Leg of TSCM #10 | Source | Fold into TSCM? |
|-----------------|--------|-----------------|
| Sub-GHz RF bug sweep (#1) | headless core of `lora_analyze_screen.cpp` | Orchestrated, tile STAYS (continuous live-spectrum use) |
| Hidden-cam / surveillance fingerprint (#9) | `flock` tile / `surveillance_device.h` | Orchestrated, tile STAYS (frequently-run passive detector) |
| NFC reader-field (#5) | `NFCReader.rfalIsExtFieldOn()` | On-demand leg + optional background alert |
| Ultrasonic leg | — | **DROPPED** (8 kHz Nyquist) |

- **Faraday (#2) stays standalone** — distinct intent (verify *my own* bag, run often, seconds-long), wrong fit as a TSCM leg.
- Rationale: the legs have better homes as live standalone tools; folding them would delete useful tiles to save tiles that are not scarce here. TSCM composes the detectors into one "clean / N anomalies" verdict.

---

## 4. Per-Tool Build Plan (ordered easiest/highest-value first)

### TRANCHE 0 — Framework (P0, blocks most tools)
- [ ] **2a DetectorRegistry** — S — host-test the mode-suppression policy.
- [ ] **2b RadioScheduler** — M (decision) + L (actuation) — host-test decision core; on-wrist test the WiFi-down→BLE-up transition and the async `SCAN_PARAM_SET_COMPLETE` handoff (`ble_scan_manager.cpp:75-77`).
- [ ] **2c raw-mgmt fan-out** — S — regression-test beacon survey + handshake DATA path unchanged.
- **Acceptance:** registry ticks all detectors at 1 Hz with correct Daily suppression; scheduler never brings BLE up with WiFi live (soak test, no hang); `wifi_mgmt_add` delivers 0x40/0xC0/0xA0 frames without perturbing beacon counts.

### TRANCHE 1 — Pure-glue wins (buildable now, no new DSP, minimal capture changes)

STATUS 2026-07-22: **#6 BUILT + FLASHED** (`tracker_sweep.{h,cpp}`, `TR_CAT_TRACKER`, "Trackers" tile; HD icon customized from user art -> SD `/Icons/trackers.png`). **#9 BUILT** (additive `classify_wifi` feed in `detect_pipeline.cpp beacon_cb` -> Surveillance domain) + now has its OWN "Spycam" tile -> results screen (`spycam.{h,cpp}` store + `spycam_screen.{h,cpp}`, per Domenic 2026-07-22) listing detected cameras (class/confidence/SSID/RSSI). **#5 BUILT** (compiled, awaiting flash) - `nfc_field_screen.{h,cpp}` modal tool + "NFC Field" tile. Powers NFC (powerControl+initNFC), NEVER starts discovery (so our field stays off + the auto-EFD from rfal init reflects an EXTERNAL reader), polls `NFCReader.getRfalRf()->rfalIsExtFieldOn()` (efd_o) at 200ms with a 2-poll debounce, buzzes + shows "READER FIELD" + dwell on a hit, powers NFC down on any exit (own tick guards on active screen). NEEDS ON-WRIST VERIFICATION: hold an active RFID reader within a few cm - confirm it trips and doesn't false-trigger on its own residual field. Near-field range only. ALSO built (compiled, awaiting flash): **BOOT input layer** in `main.cpp` (polled duration state machine: short press = old back/Settings-from-clock via `do_boot_back_action()`, long press >= 600ms = home/clock from anywhere; built once so the future Offense knock layers on).

**#6 Universal tracker sweep + time-correlation — S/M — buildable now**
- Module: `tracker_sweep.{h,cpp}` — one BLE consumer via `ble_scan_add()` (mirror `airtag.cpp:168`).
- Per advert: `identify_tracker()` (`detect/tracker_ident.h:109`) + `classify_ble()` (`surveillance_device.h:114`, already knows Tile 0xFEED/0xFEEC, SmartTag 0xFD5A, Chipolo). On any unwanted/tracker hit call `threatradar_observe(mac6, rssi, TR_CAT_TRACKER)`.
- The gap is only wiring: `threat_radar.cpp` (waypoint store ≥120 m `TR_WP_MIN_M:20`, `score_level()` None→Confirmed, SD persist `/ThreatRadar/discovered.txt:161`) already does "followed you across N locations" — Tile/SmartTag/Chipolo/generic 0xFD44 are classified but never enter it.
- Touchpoints: new `TR_CAT_TRACKER` enum in `threat_radar.h:24` + name in `threatradar_category_name()`; reuse `airtag.cpp:43` 5-min dedup table; Daily-suppress the haptic at `threat_radar.cpp:289 instance.vibrator()`; tile `"trackers"` in `tile_keys[]`.
- **On-wrist iteration:** dedup-table sizing in crowds (mall floods 0xFD44).
- **Ceiling (state honestly in UI):** MAC rotation ~15 min caps correlation window for rotating tags (AirTag/Find My); static-MAC (Tile classic) correlate indefinitely. Do NOT promise cross-hour attribution of a rotating tag.
- **Acceptance:** a Tile and a SmartTag carried across ≥2 waypoints ≥120 m apart escalate to Confirmed and persist; Daily shows no haptic.

**#9 RF hidden-camera fingerprint — S — buildable now**
- No new capture. New `spycam` pipeline as a `WifiBeacon` consumer calling `classify_wifi()`; on hit `detect::feed(s_threat, Surveillance, sev)` (severity capped by `Confidence`: High UUID/OUI→High, SSID substring→Low). Fold into `detect_pipeline.cpp:beacon_cb` (already has beacon + lock + `s_threat`). BLE half rides existing `ble_detect_pipeline`.
- Touchpoints: `detect_pipeline.cpp`, tile `"spycam"`. No new `src/detect/` module.
- **Acceptance:** a known wireless-cam OUI/BLE company-id raises Surveillance domain; passive only, no active probing.

**#5 NFC reader-field detection — S — buildable now**
- Module: `nfc_field.{h,cpp}` — `nfc_field_tick()` polls `NFCReader.rfalIsExtFieldOn()` (`rfal_rfst25r3916.cpp:2420`) when NFC powered; debounce ≥2 polls to reject reader self-emission/transients; emit pocket-skim alert with dwell time. Power pattern per `nfc_screen.cpp:198-200` (`instance.powerControl(POWER_NFC,true)` / `initNFC()`).
- `rfalWakeUpModeStart` (`:2475`) available for low-power always-on, but **conflicts with active read/write** — gate field-watch to run only when the reader is not discovering.
- Touchpoints: new module, 1 Hz registry hook, tile `"nfcfield"`. No inter-radio arbitration (independent rail).
- **On-wrist iteration:** debounce count vs transient reader emissions.
- **Caveat:** range is centimetres (13.56 MHz near-field) — catches close-contact pocket-skim, not room-scale.
- **Acceptance:** bringing an active RFID reader within a few cm raises an alert after debounce; none in Daily; no false trigger during our own NFC read.

### TRANCHE 2 — Sequenced radio sweeps (depend on RadioScheduler 2b)

**#2 Faraday-bag verifier — M — buildable now (needs threshold calibration)**
- Modules: `faraday_verify.{h,cpp}` + `faraday_screen.{h,cpp}`. Modal, own `lv_timer`, no 1 Hz hook.
- **Baseline-then-inside delta method** (absolute floors drift): capture outside bag, then inside; PASS = attenuation delta on every channel (default ≥25-30 dB) AND GPS sats→0.
- Phase order respects exclusivity (via scheduler): (1) LoRa floor `radio.getRSSI(false)` + GPS sat count `instance.gps.satellites.value()` together (independent rails); (2) WiFi peak (promiscuous, reuse `analyze_screen.cpp` capture), then **full teardown**; (3) BLE peak (transient `ble_scan_cb` only after WiFi torn down).
- Touchpoints: new modules, tile `"faraday"`, `RadioScheduler` for the WiFi→BLE handoff.
- **On-wrist iteration:** per-channel dB thresholds; GPS sat count decays slowly after shielding (tracking-loop hysteresis) — allow ~15-30 s GPS settle or treat GPS as advisory.
- **Acceptance:** sealing a known-good bag yields PASS with ≥25 dB delta on WiFi/BLE/LoRa and sats→0 within settle window; an open/leaky bag yields FAIL with the offending channel named.

**#4 WIDS (active-attack detect) — M — depends on 2c raw-mgmt hook**
- Module: `wids_pipeline.{h,cpp}` subscribes to `wifi_mgmt_add`. Callback reads subtype (`frame[0]`): 0xC0→Deauth / 0xA0→Disassoc, copies addr3 (`frame+16`) as BSSID, ingests into a long-lived `DeauthFloodDetector` (`detect/deauth_flood.h`), `detect::feed(s_threat, DeauthFlood, map(flag))`. Evil-twin (`RogueApDetector`) is already live in `detect_pipeline.cpp:beacon_cb` — WIDS surfaces the **union** under one tile.
- PMKID-harvest is **HEURISTIC** — passively there is no harvester probe; flag only as *correlated deauth + EAPOL-M1 burst* using existing `handshake_rx_data` EAPOL sightings. Market as "attack in progress", NOT a clean PMKID detector.
- Touchpoints: new module, tile `"wids"`, alert popup guarded by `argus_mode_current() != Daily`.
- **On-wrist iteration:** channel-hop coverage (200 ms hop misses ~92% of a burst's channels — tune dwell or pin to associated channel); rate thresholds vs legit roaming false positives.
- **Acceptance:** a deauth flood on the dwelt channel escalates DeauthFlood domain; evil-twin still fires; no alert in Daily.

**#3 Self-surveillance audit — M — depends on 2c raw-mgmt hook**
- Modules: `leak_audit_screen.{h,cpp}` — user-driven scan (not a 1 Hz detector). Subscribe to `wifi_mgmt_add`, collect probe-reqs (0x40): **directed** probes (non-empty SSID element) = leaked saved-network names; source MAC locally-administered bit (`src[0] & 0x02`) = MAC-randomization on/off. BLE-advertising leakage via `ble_scan_manager`.
- **Sequences** (WiFi↔BLE exclusive, via scheduler): WiFi probe pass → tear down → BLE pass → one report.
- Touchpoints: new screen, tile `"leakaudit"`.
- **Heuristic (do not headline as deterministic):** "which MAC is mine" needs a user-selected device.
- **Acceptance:** the watch's own directed probes appear as leaked SSIDs; randomization state reported correctly for a device with a known setting.

### TRANCHE 3 — Meta-mode (depends on Tranche 1+2)

**#10 TSCM "sweep this room" — M/L — after #1/#5/#9**
- Modules: `tscm_sweep.{h,cpp}` + `tscm_screen.{h,cpp}`. Sequences: (a) sub-GHz RF bug sweep (headless `rf_sweep_run_once()` refactored out of `lora_analyze_screen.cpp`), (b) hidden-cam fingerprint (`classify_*` over a timed WiFi-then-BLE window), (c) NFC reader-field poll. **Ultrasonic leg DROPPED.**
- Returns "clean / N anomalies" with per-check breakdown.
- Touchpoints: new modules, tile `"tscm"`, `RadioScheduler` for the multi-radio sequence.
- **Integration risk (the whole risk):** radio-ownership sequencing — a BLE-under-WiFi add HANGS the watch. The phase machine must prove WiFi is fully deinit'd before the BLE phase and restore prior owners on early exit/abort.
- **Scope honesty in UI:** sub-GHz sweep cannot see 2.4 GHz spy cams (that is the WiFi/BLE leg) or cellular (no baseband) — a "clean" verdict must not over-promise.
- **Acceptance:** running the sweep sequences all legs with no hang across 50 runs; a planted sub-GHz emitter, a known wireless cam, and an active reader each register as anomalies; abort mid-sweep restores all radios.

### TRANCHE 4 — New low-level work + heavy tuning (do LAST)

**#7 GPS-jamming detection — M/L — needs new low-level work**
- Module: `gps_jam.{h,cpp}` — register `TinyGPSCustom` fields on `instance.gps` for GSV `SNR` (field 7, repeating) + total-in-view. TinyGPSPlus parses GGA/RMC only by default; **GSV C/N0 is the one genuinely new low-level piece** (no library add — `TinyGPSCustom` already linked). No UBX binary jam indicator available.
- `gps_jam_tick()` computes mean C/N0 of tracked sats each second. Signature: had-fix (sats-used ≥4) → C/N0 of ALL tracked sats collapses toward 0 within a short window AND sats-used→0. State machine NOMINAL→SUSPECT→JAMMED (sustained N s). Shares `gps_fresh()` gate (`gps_screen.cpp:115`); no radio arbitration (independent receiver).
- Touchpoints: new module, 1 Hz registry hook, tile `"gpsjam"`.
- **On-wrist iteration (hard):** distinguishing jamming from indoor / urban-canyon fade needs real field captures. Benchtop GPS jammers are legally restricted — validate with tunnel / Faraday-bag transitions and shielding, NOT by emitting.
- **Acceptance:** a Faraday-bag transition (fix→all-sats-collapse) trips JAMMED; slow indoor walk-in (gradual/partial fade) does NOT.

**#8 Personal-safety duress (IMU → covert LoRa distress + GPS) — M — buildable now, heavy tuning**
- Modules: pure `duress_detect.{h,cpp}` (host-testable) + thin device shim. `DuressDetector::ingest(ax,ay,az,t_ms)` → `{None,Fall,WatchOff,NoMotion}`. Fall = >~3 g spike then <0.1 g stillness N s; WatchOff = BHI260 wear/no-motion virtual sensor (fallback: sustained flat-and-still); NoMotion = delta < `MOTION_DELTA_G` (`main.cpp:939`) for a window. All pure math, mirrors `tail_detect.h`.
- Feed from the existing `motion_wake_poll()` site (`main.cpp:961`) — sample already in hand, no second IMU tap.
- Escalation with a **15-30 s covert "Cancel?" window** before arming (prevents dropped-watch false SOS).
- Covert TX (radio ownership sequenced via scheduler, mimic `aprs.cpp:93-95`): prefer whichever LoRa stack is already up — `meshtastic_send_text_to()` (`meshtastic.h:35`) if mesh owns SX1262, else `aprs_send_position(comment)` (`aprs.cpp:260`). No-fix path routes a "no-GPS duress" text via `meshtastic_send_text()` (`aprs_send_position` returns false without lock, `:263`). Repeat on slow cadence (~60 s) until cancelled.
- **Gating:** Defense/Offense only; in Daily the detector may run for logging but **must NOT transmit or alert** — guard TX and Cancel UI behind `argus_mode_current() != ArgusMode::Daily`. Tile `"duress"`.
- **On-wrist iteration (heavy):** fall vs hard clap vs setting watch on a table — g-thresholds and stillness windows are empirical. Ship conservative (low false-positive, long cancel window); NoMotion backstops a missed fall, a false SOS is costly.
- **Acceptance:** host tests pass for synthetic fall / watch-off / no-motion traces; on-wrist, an uncancelled fall TXs coded position on the already-powered LoRa stack; a deliberate table-set is cancellable and does not TX; Daily never transmits.

---

## 5. Risk Register

| Risk | Severity | Mitigation |
|------|----------|------------|
| Scheduler brings BLE up while WiFi live → **hard hang, reset required** (`ble_scan_manager.cpp:49`) | **Severe** | Reuse the `wifi_is_active()` guard VERBATIM in `RadioScheduler`; 50-run soak test before shipping any exclusive sweep (#2/#3/#4/#10). |
| Piggyback detector powers a radio alone → battery drain / blocks `device_mode` | High | Enforce "never power a radio alone" in the scheduler grant path; passive = ride-along only. |
| WIDS channel-hop coverage: 200 ms hop misses ~92% of a burst's channels | Med | Tune dwell on WIDS screen or pin to associated channel; document as passive-coverage limit. |
| False positives (WIDS legit roaming, duress table-set, NFC reader self-emission, tracker crowds) | Med | Rate thresholds + debounce + long cancel window; ship conservative; on-wrist tuning loops. |
| GPS jam vs indoor/urban fade ambiguity | Med | Require full all-sat collapse + sats-used→0; validate via shielding transitions, never by emitting. |
| 1 Hz tick budget (already I2C-heavy, skipped during `lvgl_priority`) | Med | Keep sweep DSP OFF the tick — modal sweeps use own `lv_timer` or a task; registry does light work only. |
| Battery from added always-on detectors | Med | Piggyback-only for passive; modal sweeps are user-initiated, not background. |
| **Legality of active emit** | Med | Only #8 duress TX emits (licensed-band LoRa/APRS/mesh, user-initiated safety). NO GPS-jammer emission for #7 testing (restricted). RF sweep #1 and all others are receive-only. |
| Raw-mgmt fan-out perturbs beacon survey / handshake DATA path | Med | Regression-test beacon counts + handshake capture unchanged after 2c. |
| Over-promising "clean" TSCM verdict (no 2.4 GHz sub-GHz overlap, no cellular) | Low | State scope in UI: sub-GHz sweep + WiFi/BLE fingerprint only; no cellular/IMSI. |

---

## 6. Open Decisions for Domenic

- [ ] **Tile budget:** this adds up to 9 new tiles (`trackers`, `spycam`, `nfcfield`, `faraday`, `wids`, `leakaudit`, `tscm`, `gpsjam`, `duress`) to a grid that already has 17. Do we ship all, or phase the grid (e.g. only Tranche 1-2 tiles first)? Confirm none should fold further.
- [ ] **`RadioScheduler` scope:** promote to a full arbiter in `device_mode.*` now (bigger P0, unblocks everything cleanly), or ship Tranche 1 (BLE/NFC-only, no exclusive sequencing) against the existing snapshot/restore idiom and build the scheduler just before Tranche 2? Recommendation: build the scheduler now — the sweeps are the point.
- [ ] **Duress TX default:** covert distress on a real fall is a safety feature but auto-transmits on licensed bands. Confirm ship-enabled vs opt-in-per-session, and the default cancel-window length (recommend 30 s).
- [ ] **Duress destination:** APRS position beacon (public) vs mesh `send_text_to(dest_node)` (private, needs a configured contact node)? Need a configured emergency mesh node id, or default to APRS?
- [ ] **GPS-jam validation:** OK to validate #7 only via Faraday-bag/tunnel transitions (no jammer emission)? If a stronger test is wanted, it needs a licensed/shielded facility — flag if in scope.
- [ ] **Acoustic drone (#DEGRADED):** confirm we are NOT attempting it this cycle (8 kHz-limited, research-grade). Plan currently excludes it from all tranches.
- [ ] **Tranche cut line for first release:** recommend Tranche 0+1 (framework + tracker/spycam/NFC) as the first shippable milestone; Tranche 2-4 follow. Confirm.

---

### Build order at a glance
1. **T0 Framework** — DetectorRegistry (S), RadioScheduler (M+L), raw-mgmt hook (S)
2. **T1 Glue wins** — #6 trackers (S/M), #9 spycam (S), #5 NFC field (S)
3. **T2 Sequenced sweeps** — #2 Faraday (M), #4 WIDS (M), #3 self-audit (M)
4. **T3 Meta** — #10 TSCM (M/L)
5. **T4 New low-level + tuning** — #7 GPS-jam (M/L), #8 duress (M)

**Buildable now (no new hardware iteration for core function):** #5, #6, #9, #2.
**Needs on-wrist iteration before trustworthy:** #4 (channel coverage), #7 (jam vs fade), #8 (fall thresholds), #10 (radio sequencing soak).
**Dropped — do not build:** ultrasonic beacon, acoustic drone (not this cycle), IMSI-catcher, thermal/laser mic.
