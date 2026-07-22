# ARGUS Offense Redesign - working design doc

Status: THINKING / not built. Started 2026-07-22 at Domenic's direction:
"redesign the entire Offensive part of the watch, wallpaper, icons, fonts,
from a red teamer's point of view." This doc is the strawman to react to, not a
committed plan. Nothing here is implemented yet.

The current offensive tiles, icons, wallpaper and fonts are all TEMPORARY
placeholders inherited from the base project; treat none of them as sacred.

---

## 1. Three personas, three identities

The mode machine (Daily / Defense / Offense) already exists. The redesign is
about giving each mode a distinct *identity* so a glance tells you which one
you're in - and so Offense feels like a different device, not the same grid
recoloured.

| Mode | Persona | Feel | Palette | Job |
|------|---------|------|---------|-----|
| Daily | Civilian | Innocent consumer smartwatch | neutral / brand steel | Reveal nothing |
| Defense | Guardian | Calm situational awareness | steel-blue, red only on a real threat | Detect surveillance around you |
| Offense | Operator | Aggressive console / "weapon armed" | amber + red on black, terminal green accents | Run the engagement |

The point of the redesign: **Offense should not look like Defense with red
borders.** It should read as a purpose-built operator console the moment it
boots into it.

## 2. Offense visual system (the redesign surface)

- **Wallpaper.** Dark operator backdrop - faint grid / scanline / "target
  acquired" reticle motif, near-black so the tiles and readouts pop. Distinct
  from the Daily "PRIVACY IS AN ILLUSION" face and any Defense face. Candidate
  directions: (a) tactical HUD grid, (b) faint world-map / triangulation, (c)
  matrix-rain in amber. Needs Domenic's pick.
- **Palette.** Black base; amber (`ARGUS_OFFENSE_ACCENT`) as the primary; HADES
  red for "armed / capturing / hot"; terminal green for live readouts/counts.
  No steel-blue in Offense (that's Defense's colour - keep them unmistakable).
- **Fonts.** Move to a console/monospace voice for Offense readouts (VT323 is
  already bundled as the brand mono). Headers can stay Orbitron for the tech
  look. AVOID: the commercial Bank Gothic flagged for public release.
- **Icons.** Redraw the offensive-tool glyphs as a coherent set (currently the
  Pwn pawn + placeholders). Same HD-sprite pipeline as the defensive icons
  (tools/gen_icon_sprites.py -> /Icons on SD, procedural fallback in-firmware).
  A red-teamer set wants aggressive, legible-at-a-glance glyphs: capture,
  inject, spoof, jam, loot.
- **Chrome.** The mode badge (now in the header) + the persistent Offense frame
  are the "you are armed" cues. Consider making the frame a subtle animated
  pulse when a capture/attack is actively running vs idle.

## 3. Tool taxonomy - THE key open decision

Strict separation is now enforced (Offense shows only offensive tiles). But the
buckets themselves need Domenic's call. Today's classification (tile_mode()):

- **Offensive (Offense):** Pwn (handshake capture), Mouse (BLE HID inject),
  Tesla CP.
- **Defensive (Defense):** Radar, AirTag, Trackers, Spycam, NFC Field, Flock,
  Skimmers, Flipper, Evil Twin (detector), WiFi survey, Analyze, Pager, TPMS,
  HexHound.
- **Neutral utilities (currently shown in Defense):** Notify, LoRa APRS, USB SD.

Red-teamer questions this raises:
1. **Recon tools are offensive too.** WiFi site-survey, spectrum Analyze, and
   the Wardriver (currently a clock gesture, not a tile) are engagement recon.
   Should they live in Offense, be DUPLICATED into both, or stay Defense?
2. **USB SD in Offense.** Offloading loot (pcaps, wardrive CSVs) is core to an
   engagement. A red-teamer probably wants USB SD reachable in Offense even
   though it's "neutral." Same for a future dedicated "Loot" screen.
3. **New offensive capability to plan for** (all subject to authorization +
   legality; this is the DarkHorse pentest context): deauth/evil-twin ATTACK
   (vs the current detector), beacon/BLE spam, captive-portal, a live target
   list. Which are in scope for the build-out?
4. **Where do the neutral utilities live** once Defense is "defensive only"?
   Options: (a) keep them in Defense, (b) give Daily its own small utility
   surface, (c) a fourth "Utilities" area reachable from any mode.

## 4. UX / interaction

- **Entry (the knock).** L-S-L on BOOT now works (the release-to-release timing
  bug is fixed). But a 3-beat button gesture is inherently fiddly and slow under
  time pressure. Options to weigh: keep the knock purely for deniability;
  add/replace with a different discreet gesture (e.g. a touch pattern on a
  decoy screen); tune KNOCK_GAP_MS / the long threshold. Red-teamer tension:
  deniability (obscure) vs speed (fast to arm on-site).
- **Exit.** Now a Settings "Exit Offense" button (drops to Daily). The redesign
  should make the header mode badge itself the natural place to exit (tap ->
  confirm -> lock), and retire the temporary Settings buttons.
- **Quick-launch.** On an engagement you want to arm a capture in seconds. Worth
  a "last tool" / favourite shortcut once in Offense.
- **Loot + shred.** Tier-1 shred is wired (wipes /pwn, /Wardrive, /PingSweeps,
  /Screenshots). A dedicated Loot screen (list captures, sizes, offload, wipe)
  would round out the operator workflow.

## 5. Hardware / RF reality (confirmed 2026-07-22)

ESP32 cannot run WiFi and BLE simultaneously (shared 2.4GHz radio + the SRAM/
controller contention this firmware already guards). Independently confirmed by
SiberBaba ("you can't use ble and wifi same time on esp32"). The existing
mutual-exclusion guards (ble_scan_manager wifi_is_active(), the radio-conflict
dialog, the low-mem dialog) are the correct architecture - the redesign keeps
them. Any Offense dashboard should SHOW which radio is live so the operator has
RF discipline (don't leak while thinking you're passive).

## 6. Open questions for Domenic (blockers before building)

1. Wallpaper direction for Offense (HUD grid / world-map / amber matrix / other)?
2. Tool taxonomy: recon tools (WiFi/Analyze/Wardriver) -> Offense, both, or Defense?
3. USB SD (and a future Loot screen) reachable in Offense - yes?
4. Where do neutral utilities (Notify/APRS/USB SD) live once Defense is strict?
5. Which new offensive capabilities are in scope to build (authorized-use)?
6. Keep the knock as the entry, or rethink the entry gesture?
