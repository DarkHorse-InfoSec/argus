# Scope: Phone Notifications on Argus Watch (iOS + Android)

Status: SCOPING (not started). Owner: Domenic. Drafted 2026-07-21.

Goal: mirror incoming phone notifications (app name, title, body) onto the
T-Watch Ultra over BLE, for both iPhone and Android. Fits the "slim BLE-focused
daily-wear companion" direction in todo.md.

---

## 0. The one constraint that shapes the whole feature

On this board **WiFi and BLE cannot run at the same time** (`esp_bt_controller_enable()`
hangs while WiFi is up; see `src/ble_scan_manager.cpp:46`, enforced via the
radio-conflict dialog in `src/tools_screen.cpp:33`).

Phone notifications require a **persistent** BLE connection to the phone. Therefore:

> **While notification mirroring is ON, the WiFi tools (site survey, evil-twin,
> ping sweep, wardriving) are unavailable, and vice-versa.**

This is a product decision, not a bug. The watch already treats the radios as
mutually exclusive everywhere; notifications just become another "BLE-mode"
consumer. Locked resolution: a top-level Daily-wear vs Field-tool mode (Decision A).

This constraint is the single biggest thing to get right. Everything else is
standard embedded-BLE work.

---

## 1. iPhone / iOS path: ANCS (NO companion app)

iOS ships **ANCS (Apple Notification Center Service)** built in. No App Store app,
no user account, nothing to distribute. This is the easy, high-value path and ships
first.

Roles (per Apple spec):
- iPhone = Notification Provider = **GATT server**.
- Watch  = Notification Consumer = **GATT client**.

Flow the watch implements:
1. Advertise, including the ANCS **solicited-service UUID** (`7905F431-...`) so iOS
   knows to expose ANCS.
2. Accept the iPhone connection and **bond** (pairing + encryption; ANCS
   characteristics are encryption-gated, so unbonded = no data).
3. Discover ANCS on the iPhone; subscribe to **Notification Source** + **Data Source**.
4. On each notification event, write to the **Control Point** to fetch attributes
   (app identifier, title, message, date), parse, store, display.
5. Persist the bond in **NVS** so it survives reboot (iOS won't re-expose ANCS
   without a valid bond).

Stack fit: Bluedroid supports ANCS-client (ESP-IDF ships a Bluedroid ANCS example).
Doable, moderately heavy. Reuses the existing peripheral bring-up from `mouse_hid.cpp`.

iOS effort (watch-side only): ~1-1.5 weeks after the shared layer exists.

---

## 2. Android path: companion required (no ANCS equivalent)

Android has **no built-in BLE notification service**. Something on the phone must
capture notifications via Android's `NotificationListenerService` (a permission the
user explicitly grants) and forward them over BLE.

**Locked choice: ride Gadgetbridge (Decision B).** Gadgetbridge (open-source,
F-Droid) already does notification-forwarding for many watches. The watch implements
a device protocol Gadgetbridge supports; users install Gadgetbridge instead of any
ARGUS app.
- Cost: reverse/implement a supported protocol on the watch (~2-3 weeks watch-side),
  constrained to what that protocol expresses, but **zero app for us to maintain**.
- Aligns with the project's open-source / privacy posture.

(Rejected alternative, for the record: build our own ARGUS companion app. Full
control and branding, but a whole Android sub-project plus ongoing OS/permission/store
maintenance. Not worth it for v1.)

---

## 3. Shared watch-side components (needed for BOTH platforms)

Build these once, both paths use them:
1. **BLE peripheral + bonding + NVS bond store:** generalize the `mouse_hid.cpp`
   peripheral bring-up; add persistent bonding (`BLESecurity`, store bonds in NVS).
2. **Notification model + ring buffer:** struct {app, title, body, timestamp,
   category}; bounded store (e.g. last 20), newest-first, RAM-only or small NVS.
3. **Notifications UI screen:** LVGL list screen (mirror an existing screen like
   `wifi_screen.cpp`), tap to expand, clear/clear-all, haptic on arrival (reuse the
   Meshtastic haptic path already in `configuration_screen.cpp`).
4. **Daily-wear / Field-tool mode owner:** the mode arbiter that brings BLE up in
   Daily-wear, keeps WiFi/notifications from colliding, and gates which tools/screens
   are reachable. Reuses the existing conflict-dialog machinery as the fallback
   (`tools_screen.cpp:33`, `settings_screen.cpp:413`).
5. **Watch->phone actions (optional, later):** dismiss/reply. ANCS supports
   "Perform Notification Action"; Gadgetbridge has its own equivalent.

---

## 4. Flash / RAM budget (real risk, solvable)

- App is at **2.86 MB / 3.00 MB = 90.9%** (`app3M_fat9M_16MB.csv`). Adding ANCS +
  custom GATT service + a UI screen + store is roughly +30-80 KB, which may not fit
  the 3 MB box as-is.
- **Lever:** the chip is **16 MB**; current layout is 3 MB app + 9 MB FAT. Shrinking
  the FAT and bumping the app partition to 4 MB reclaims ~1 MB of headroom. This is a
  partition-CSV change (data-loss on the FAT, so schedule it deliberately).
- **RAM:** Bluedroid is already linked; the incremental RAM is the bond store +
  notification buffer + one GATT connection, so modest. Current RAM 61.8%.
- Not considered: switching Bluedroid to NimBLE would save flash/RAM but is a large
  cross-cutting refactor (all of airtag/flipper/skimmer/ble_scan_manager/mouse_hid
  use Bluedroid APIs). Out of scope for v1.

---

## 5. Decisions (LOCKED, Domenic, 2026-07-21)

- **Decision A, radio-mode UX: DAILY-WEAR vs FIELD-TOOL MODE.** A top-level mode
  switch. "Daily wear" = BLE up, notifications mirroring, WiFi tools disabled.
  "Field tool" = WiFi/pentest up, notifications paused. Bigger UX change than a lone
  toggle, so it becomes a first-class Phase 1 concept (a single mode owner that
  arbitrates the radio and gates tool reachability), not a bolt-on. The existing
  radio-conflict dialog is the fallback for anything that tries to cross modes.
- **Decision B, Android delivery: RIDE GADGETBRIDGE.** No ARGUS app to build or
  maintain. Watch implements a Gadgetbridge-supported device protocol; users install
  Gadgetbridge from F-Droid.
- **Decision C, sequencing: iPHONE (ANCS) FIRST, ANDROID AFTER.** Ship ANCS as soon
  as it works (no app dependency); Gadgetbridge/Android follows.

---

## 6. Locked plan of record

1. **Phase 0, ANCS bench spike (2-3 days).** De-risk bonding + ANCS-on-Bluedroid +
   flash fit before committing. Success = one iPhone notification's title/body over
   serial.
2. **Phase 1, shared layer + Daily-wear/Field-tool mode owner (Decision A):**
   peripheral+bonding+NVS, notification model, UI screen, and the mode arbiter that
   gates the radios and tool reachability. (~1.5-2 wks)
3. **Phase 2, iPhone/ANCS end-to-end (Decision C):** full attribute fetch, parse,
   display, persist bond, reconnect. **Ship here.** (~1-1.5 wks)
4. **Phase 3, Android via Gadgetbridge (Decision B):** implement the supported
   protocol on the watch; no app to ship. (~2-3 wks)
5. **Phase 4, actions (optional):** dismiss/reply.

Repartition to a 4 MB app happens whenever Phase 1/2 first blows the 3 MB box.

Next concrete step: the **Phase 0 spike**. The exact Gadgetbridge-compatible protocol
for Phase 3 can be settled during Phase 1; it does not block the spike.
