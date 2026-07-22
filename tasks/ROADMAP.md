# ARGUS Roadmap - "the anti-surveillance watch that also has teeth"

Strategic direction (Domenic, 2026-07-22): ARGUS stays a **defensive anti-surveillance
daily-wearable first**, and adds a **select offensive layer** ("teeth"). We do NOT try to
out-marauder Marauder; we own the lane nobody else does (defense + daily wear + mesh).
Reference build studied: siberbabba's WDGWatch / "SCR TERMINAL" (same T-Watch Ultra board).

Legend: [ ] todo  [~] in progress  [x] done  [DES] do on the DES-70072 machine.

---

## 0. Personas / mode selection (ARCHITECTURE - do this first, everything hangs off it)
Decision: a **persisted persona** the watch boots into, switchable. Offensive surface is
HIDDEN in daily mode = real opsec (plausible deniability if glanced at / confiscated).
Extends the existing `device_mode.*` (Daily-wear vs Field-tool) built for notifications.
- [ ] **Daily Wear** - clock, notifications, timepieces, calendar. Tools grid + detectors
      hidden. BLE-first. Looks like an innocent smartwatch.
- [ ] **Field Tool** - full defensive + offensive suite. WiFi-first.
- [ ] **Guardian** (optional) - passive: Threat Radar + detectors run silently in the
      background, minimal UI, no offensive tiles. "Watches your back."
- [ ] Switch mechanism: persist last persona (NVS, like the notification platform), boot
      into it. Deliberate switch via a boot chooser (skippable) OR a hidden long-press.
      Boot chooser maximizes opsec (intentional posture); hidden gesture is stealthier.
- [ ] Gate the Tools grid + offensive modules behind the persona so daily mode is clean.

## 1. Quick wins (started 2026-07-22)
- [x] 3-wide tile grid (first pass): tiles 118px, sprites scaled to ~78px, label font 14.
- [ ] [DES] 3-wide grid POLISH: retune the 3 procedural icons (Pager/Flipper/HexHound draws)
      and the no-SD glyph fallbacks (draw_*_icon, currently 48px @ y=44 for the old 180px
      tile) for 118px tiles; check long labels ("LoRa APRS") don't clip.
- [x] System Info in Settings (heap/PSRAM/battery/uptime, refresh on show).
- [ ] Optional: live 1s refresh of System Info while Settings is open (currently snapshot-on-open).

## 2. Requested features (all feasible on this board)
- [ ] **BadUSB** - ESP32-S3 native USB HID keyboard; Ducky-style payloads when plugged into
      a target over USB-C. Same class as the existing BLE Mouse HID. Ship behind an
      authorized-testing disclaimer (like Tesla CP). Payloads from SD (/BadUSB/*.txt).
- [ ] **BadBLE** - extend BLE HID from mouse to KEYBOARD injection (BLE HID takeover vs
      devices that auto-accept HID). Small step from src/mouse_hid.cpp.
- [ ] **BLE UART terminal** - Nordic UART Service (6E400001-...) CLIENT: scan -> connect ->
      TX/RX text with any NUS device (Flippers, IoT, DIY). Plus a NUS SERVER so a phone can
      command the watch (mirror WDGWatch's JSON-over-NUS idea).
- [ ] **Consolidate HID tools**: fold Mouse + BadBLE(keyboard) + BadUSB into one "HID" tile
      (reclaims a tile; groups the injection tools). Tesla CP: DECIDE keep-as-curiosity vs
      cut (dead vs 2022+ Teslas - crypto auth).

## 3. Human Radar (defensive, take from WDGWatch)
- [ ] WiFi RSSI/CSI-variance motion detection: lock an AP, watch how a moving body perturbs
      its signal -> presence/motion score + radar-sweep UI. ESP32 supports CSI callbacks.
      Fits ARGUS's defense identity ("someone moving behind that wall / following").

## 4. Outside-the-box tools NOBODY else has (the differentiators)
- [ ] **1. Ultrasonic tracking-beacon detection (+ optional emit)** - retail/ad networks push
      inaudible ~17-20 kHz audio beacons for cross-device tracking. Use the MIC to detect +
      alert (unique defensive win). Optional deterrence EMIT via speaker, gated behind a
      clear "makes noise, use responsibly" screen. Hardware note: tiny speaker + mic cap true
      >20 kHz; near-ultrasonic (17-20 kHz, what ad beacons actually use) is very doable.
- [ ] **2. GNSS spoofing detection** - flag impossible position jumps, non-physical
      velocities, C/N0 anomalies. Almost no consumer device warns you your GPS is spoofed.
      We already have the GNSS feed (instance.gps). Feed a new domain into ThreatState.
- [ ] **3. RF honeypot / canary** - broadcast a decoy SSID/BLE beacon and LOG every device
      that probes/connects. A wearable tripwire: active network-hunters reveal themselves.
- [ ] **4. LoRa duress beacon + covert C2** - IMU gesture (or watch-removed / stationary-too-
      long) fires an encrypted GPS distress beacon over LoRa to the mesh; same private
      channel = covert red-team coordination beyond WiFi/BLE range.
- [ ] **5. Swarm threat-map** - extend the existing mesh tracker-reputation (src/tracker_rep)
      into a crowd-sourced anti-surveillance map: multiple ARGUS watches share confirmed
      tails/skimmers/Flock hits, so the group sees what one wrist alone would miss.

## 5. Also worth taking from WDGWatch
- [ ] Geo-IP intelligence: port-scan a host + ISP/City/ASN lookup over WiFi, CSV export
      (we already have the Port Scanner; add the geo lookup + export).
- [ ] Live "sonar" radar: RSSI-proximity visualization for nearby WiFi/BLE (a viz layer over
      the existing scanners; pairs with Threat Radar's screen language).
- [x] System Info readout (done).
- SKIP: **ADS-B** (1090 MHz is out of the SX1262's 150-960 MHz band; can't RX aircraft on
  this hardware). SKIP: ultrasonic *deterrence-as-headline* (keep detection as the headline;
  emit is opt-in).

## 6. Offensive basics ("teeth", authorized-testing only, Field/Guardian personas only)
- [ ] WiFi deauth (targeted + broadcast) - DoS, dual-use; disclaimer + persona-gated.
- [ ] Beacon/probe flood, evil-portal (we have evil-twin DETECTION; add the offensive AP).
- [ ] WPA handshake/PMKID capture is already present (Pwn tile) - surface it better.
NOTE: NO jammers (illegal). Keep offensive tools behind the persona + an authorized-use gate.

## 7. Housekeeping / finish-what's-open
- [ ] BLE tail-detection: VERIFIED working (piggyback attach, no boot-loop). Decide: keep
      ARGUS_BLE_THREAT_PIPELINE=1 permanently; turn ARGUS_BLE_DETECT_DEBUG=0 after a real
      tracker-follow test confirms escalation. (main.cpp:80, ble_detect_pipeline.cpp)
- [ ] Font licensing: title/wordmark fonts (font_dh_ui/argus/wordmark) are Bank Gothic Medium
      BT (commercial). Before any PUBLIC push: swap to a free OFL look-alike (Saira Condensed
      / Michroma / Oswald) and regenerate the 3 .c files, OR verify the EULA. Orbitron + VT323
      are OFL (fine). Local commit already done (0f2c473).
- [ ] Theme color picker in Settings (WDGWatch has one; ARGUS has argus_accent - make it
      user-selectable).

## Identity guardrail
Every addition answers: does this strengthen "defensive anti-surveillance daily-wear," or is
it just another marauder feature? Defense + daily wear + mesh is the un-copied niche. Offense
is the garnish, gated behind a persona, never the headline.
