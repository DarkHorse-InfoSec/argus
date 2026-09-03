# ARGUS Offense/Defense build designs (from parallel design teams, 2026-07-22)

## UPDATE 2026-07-23 (daytime autonomous run) - hardware-verified progress + new tools

HARDWARE-VERIFIED by Domenic this session:
- Deauth attack INJECTS (frame count climbs to 1000s) - the sanity-check override +
  the -Wl,--allow-multiple-definition flag work on this core. Big unknown resolved.
- Beacon flood works (now sprays 6 SSIDs/tick so several appear at once).
- Boot, all defensive tiles, Loot, the busy-dialog + bigger fonts.
- Deauth survey->arming->inject handoff fixed (was blocked by the threat pipeline's
  WiFi piggyback); single-owner offense_wifi prevents two injectors colliding.
- Deauther now has a per-AP TARGET PICKER (survey -> tappable AP list -> all or one).
- CONFIRMED root cause of the USB-only boot loop: the wallpaper decode OOMs because
  the USB stack eats internal SRAM at boot (boots fine with wallpaper OFF). Permanent
  fix pending: shrink the wallpaper file on the SD to ~25KB (see the wallpaper memory).

NEW OFFENSE TOOLS added this run (compile-verified, NOT yet hardware-tested):
- Rogue AP (evil twin): broadcasts lure SSIDs (xfinitywifi, attwifi, ...) as an open
  AP, shows associated clients. offense_wifi gained AP mode; single-owner with the
  injectors. rogue_ap.{h,cpp}. Flash after this: ~93.2%.
- Probe sniffer (PROBES): passive - logs nearby devices + the SSIDs they directed-
  probe for (reveals networks a device has joined). Extended the wifi_mgmt fanout with
  src MAC + raw frame. probe_sniffer.{h,cpp}.

STILL DEFERRED (need live BLE testing, not blind): BLE spam / Sour-Apple - the BLE
scan manager owns the single GAP-callback slot, so serializing the advertising
reconfig against the Bluedroid race needs a hardware loop; it also shares the
advertising path with the working notification service.

---

## OVERNIGHT BUILD STATUS (2026-07-22 night) - COMPILE-VERIFIED, NOT HARDWARE-TESTED

Built + committed this session (all compile clean; NONE flashed/tested - I could
not flash while Domenic slept). Each is dormant-until-tapped, so the firmware boots
inert; the radio only comes up on a deliberate START.

- [x] Loot manager (Offense) - lists/offloads/wipes captures. LOW risk.
- [x] Deauth-attack DETECTOR (Defense) - wires the existing pure detector. LOW-MED risk.
- [x] Tail timeline (Defense) - reads the threat_radar store. LOW risk.
- [x] Beacon flood (Offense) + offense_wifi shared injector. MED risk (WiFi TX).
- [x] Deauth attack (Offense) - survey then inject; needs -Wl,--allow-multiple-definition
      (added) for the sanity-check override. MED-HIGH risk; deauth TX UNVERIFIED on this core.

DEFERRED (too risky to blind-ship without a hardware test - do these WITH Domenic):
- [ ] BLE spam - shares the advertising path with the WORKING ANCS/notification
      service; blind-shipping risks regressing notifications + hitting the documented
      Bluedroid GAP-reconfig race. Needs a guard (refuse while notifications active)
      + on-hardware GAP serialization test.
- [ ] Rogue AP / evil-twin ATTACK - WebServer + DNSServer would likely blow the flash
      budget (already at 93.0%); most complex. Needs its own flash-budget + hardware pass.
- [ ] Offense visual identity (palette tokens, mode-aware tile face, pulsing armed
      frame, icon set) - safe but touches shared make_tile/theme; deferred so it can be
      done + eyeballed on-wrist rather than blind.

FIRST on-wrist steps for the built tools: flash, enter Offense, and test EACH new
tool ONE AT A TIME (they bring up the radio on START). Deauth: confirm frames > 0
(if 0, esp_wifi_80211_tx is still rejecting on this core -> the override/flag needs
revisiting). Watch flash headroom (93.0% now) before adding rogue AP.

---


Four design agents produced implementation-ready designs. This is the consolidated,
actionable record + the recommended BUILD ORDER. Full loot draft code is saved in
tasks/drafts/. Nothing here is built yet except the skull2 wallpaper.

Key convention reminder: no em dashes, no Unicode, no Bank Gothic font (font_argus_ui)
in Offense (commercial/unlicensed for public release - use Orbitron font_argus_label_*
and VT323 font_argus_mono_* instead).

---

## A. Offense attack tools (WiFi deauth, evil-twin ATTACK, beacon flood, BLE spam)

**The constraint everything hangs off:** WiFi and BLE cannot be up at once (shared
radio/SRAM; bringing one up while the other is live HARD-HANGS the watch). Guards
already exist: WiFi bring-up checks ble_is_active(); BLE bring-up checks
wifi_is_active(). Every TX tool MUST route through these guards. Refusal UI already
exists: show_radio_conflict_dialog(bool is_ble_feature) in tools_screen.cpp.

**Build a shared owner first:** new src/offense_wifi.{cpp,h} = a single WiFi-TX owner
with the coexistence guard + PINNED channel control (the existing beacon manager hops
every 200ms; deauth and rogue-AP must pin a channel). API: offense_wifi_claim(mode,
channel) [refuse if ble_is_active() or a beacon scan already owns WiFi],
offense_wifi_tx(frame,len) [wraps esp_wifi_80211_tx(WIFI_IF_STA,...)],
offense_wifi_set_channel(ch), offense_wifi_release(). Single-owner refcount.

**GOTCHA (verify on hardware FIRST for deauth):** stock ESP-IDF rejects deauth/disassoc
via ieee80211_raw_frame_sanity_check(). Standard workaround is a weak-symbol override
in one .cpp: `extern "C" int ieee80211_raw_frame_sanity_check(int32_t,int32_t,int32_t){return 0;}`.
Beacon frames usually pass without it; deauth usually needs it. Verify against the
pinned core version before building deauth on top.

**Per tool:** each becomes an Offense-classified tile (tile_mode() -> Offense,
Offense-gate the click handler like on_handshake_clicked). Deauth + rogue-AP open a
target-picker screen (need a target); flood + BLE-spam can be blind toggles.
- Deauth: new deauth.{cpp,h} + deauth_screen.{cpp,h}. Two-phase: survey (reuse beacon
  manager) to build target list, then claim+pin channel, inject deauth (0xC0) /
  disassoc (0xA0) frames on an lv_timer. Targeted (addr1=client or broadcast) + all-APs.
- Rogue AP: new rogue_ap.{cpp,h} (NOT evil_twin.cpp - that is the DETECTOR).
  WiFi.softAP() cloning a surveyed SSID; optional DNSServer + WebServer captive portal
  writing creds to /RogueAP/. Add /RogueAP to offense_wipe TIER1_DIRS. bg_tick to pump
  handleClient().
- Beacon spam: new beacon_spam.{cpp,h} (NOT detect/beacon_flood.h - detector). Fabricate
  beacon (0x80) frames, spray channels 1-13, fast lv_timer. Beacons pass the sanity check.
- BLE spam: new ble_spam.{cpp,h}. Reuse ancs.cpp advertising path
  (esp_ble_gap_config_adv_data_raw + start_advertising), rotate Apple/MS/Google payloads
  + random MAC every ~20-50ms. Bring BLE up via ble_scan_add() keep-alive (carries the
  wifi_is_active() guard); show_radio_conflict_dialog(true) on refusal.

**Legal/authorization guard (all four TRANSMIT / affect third parties):** add a
one-time-per-session "AUTHORIZED ENGAGEMENT" ACK modal before the first TX-tool
activation (RAM flag, re-armed each Offense unlock); write an audit line to SD; add any
new output dir to offense_wipe TIER1_DIRS; duty-cycle + auto-stop timers on every TX tool.

**BUILD ORDER (safest first): 1) offense_wifi + Beacon flood (ship FIRST - smallest,
proves the shared owner + coexistence, bounded blast radius). 2) BLE spam (independent
radio, proven ancs path). 3) Deauth (needs offense_wifi + the sanity-check override;
higher harm). 4) Rogue AP (most complex + highest legal exposure; last).**

---

## B. Defense detectors (deauth-attack detector, tracker timeline)

**BIG FIND:** the pure detect::DeauthFloodDetector ALREADY EXISTS and is host-tested
(src/detect/deauth_flood.{h,cpp}), the DeauthFlood ThreatState domain exists, and
detect::feed() already maps it (Elevated->Medium, Flood->High). Also, the beacon manager
already runs with WIFI_PROMIS_FILTER_MASK_MGMT, so deauth (0xC) / disassoc (0xA) frames
ALREADY arrive at promisc_cb - they are just dropped by the beacon-only parse. So the
deauth detector is WIRING, not new detection:
- Add a raw management-frame fanout to wifi_beacon_manager: WifiMgmtFrame struct +
  wifi_mgmt_add/remove/consumer_count (PIGGYBACK-ONLY: refuse unless a beacon scan is
  live), and one dispatch_mgmt() call in promisc_cb. BSSID = addr3 (bytes 16..21).
- In detect_pipeline.cpp: a detect::DeauthFloodDetector instance, an mgmt_cb that ingests
  and feeds the DeauthFlood domain, piggyback attach/detach in the tick (when others>0),
  and s_deauth.tick() in the aging critical section. Surfaces automatically via the
  existing HADES-red + threat_log path; add a DeauthSnapshot read API + a read-only
  deauth_screen (VT323 rate readout). Tile defaults to Defense (no tile_mode change).
  Correlation bonus: rogue-AP + deauth-flood together auto-escalates (the MITM pattern).
- Do NOT route deauth to threat_radar (that scores GPS co-movement; a stationary attacker
  would score zero - category error). ThreatState DeauthFlood domain is the right home.

**Tracker timeline ("who's been following me"):** build on the existing threat_radar
TrContact store (each contact has first_ms/first_time/last_ms/category/level/span/rssi).
Add threatradar_get_timeline() (fill like get_threats() but sort by first_ms ASC).
New tracker_tail_timeline_screen: a scrollable vertical timeline, one node per contact,
left rail + level-colored dots, VT323 timestamps, oldest-first so a long-lived tail reads
as an unbroken run. Entry: a Timeline button on the Threat Radar screen + a Defense tile.
No main.cpp change (rides threatradar_bg_tick).

---

## C. Offense visual identity

**Palette (add tokens to theme.h; NO steel-blue in Offense):** amber
ARGUS_OFFENSE_ACCENT (0xF0,0xA0,0x30) = idle/structure/rims/glyphs/chip; HADES_RED
(0xDB,0x61,0x5A) = armed/capturing/transmitting/threat; green (0x3C,0xDC,0x78) = LIVE
numeric readouts always; warm near-black tile faces (top 0x22,0x18,0x0C -> bottom
0x12,0x0A,0x04). State machine: amber = holstered; red = firing; pulsing red vs solid
red distinguishes "firing" from "counter-detected".

**Fonts:** Orbitron (font_argus_label_*) for headers/labels; VT323 mono (font_argus_mono_16/48,
green) for ALL live readouts/counters/status log (ARMED, CAPTURING, TX: DEAUTH, LOOT 4.2MB).
Never Bank Gothic.

**Tiles:** give make_tile() a mode-aware face - Offense tiles get the warm-charcoal face +
dim-amber rim (idle) that turns RED rim (width 3) when that tool is running (not the green
fill the Defense detectors use). Fix tile_drag_finish() to restore ARGUS_OFF_AMBER_DIM in
Offense (it currently hardcodes steel-dim -> would leak Defense color).

**Icon set (via tools/gen_icon_sprites.py, amber for capability / red for hot):** capture
(skull grabbing a packet), inject (arrow piercing a host), spoof (twin AP fans, one masked),
jam/flood (tower spraying bolts + broken link), spam (bursting popup cards), loot (open
chest + exfil arrow + green data squares), wardriver (car trailing WiFi arcs). Register each
in gen_icon_sprites.py ICONS + a procedural draw_*_icon fallback.

**Chrome:** an offense_any_running() aggregator (OR of all *_is_running()); when true, PULSE
the existing lv_layer_top border frame red via an lv_anim on border_opa (700ms, playback,
infinite, OPA_40..COVER) instead of the coarse 1Hz tick. Chip bg amber->red, label VT323
"OFF"->"HOT".

**Wallpaper (skull2):** clock-face only (grid stays flat black for tile/readout contrast).
IMPORTANT readability caution from the team: keep clock wallpaper opacity ~90-130 - past
~130 the red skull can bloom on AMOLED and eat thin text. (NOTE: current implementation set
190 for prominence per Domenic's "make it pop"; verify legibility on-wrist and tune
background opacity down if the time is hard to read.) Optionally recolor the matrix rain
amber in Offense.

---

## Overall implementation sequence (each its own build + flash + on-wrist test)

1. skull2 wallpaper (DONE, built; needs offense.jpg on SD + on-wrist opacity check).
2. Loot manager (drafted + isolated + reuses the tested wipe; tasks/drafts/). Lowest risk.
3. Defense deauth detector (mostly wiring; pure detector already exists + tested).
4. Tracker timeline (reads existing store).
5. Offense visual identity (palette/fonts/tile-face/frame-pulse) + icon set.
6. Attack tools per their own order: beacon flood -> BLE spam -> deauth -> rogue AP,
   each with the authorization ACK gate.
