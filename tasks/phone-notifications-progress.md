# Phone Notifications: Live Progress

## SESSION 2 FINAL (2026-07-21 night) - all shipped + flashed to watch
- Phone notifications: iOS (ANCS) + Android (Gadgetbridge) WORKING end-to-end, banner
  over clock, persistence across reboot. Debug logging gated behind ARGUS_NOTIFY_DEBUG
  (off in normal builds - no notification content on serial).
- WiFi scan low-mem FREEZE FIXED: start_scan() now guards BLE-up + low-SRAM like
  wifi_radio_screen, fails gracefully instead of hanging (src/wifi_screen.cpp).
- Tools default order reordered to defense-first (Radar/AirTag/Flock/Skimmers...).
- WiFi saved passwords: /Settings/wifi_passwords.txt, tap known network -> auto-connect
  (src/wifi_pass_store.*, wired into wifi_screen).
- Screenshot auto-capture dev tool (env `screenshots`) -> img/argus/*.png.
- README rebranded 13:37 -> ARGUS, new screenshots, added Radar/Notify/HexHound/Pwn.
- Watch left running the NORMAL twatch_ultra firmware (all of the above), boots clean.
- NOT committed: repo has ~50 files of pre-existing WIP tangled with this work; commit
  decision still parked with Domenic (needs his WIP sequencing / no interactive staging).

---


Tracks the build against `phone-notifications-scope.md`. Updated as work lands.
Started 2026-07-21 (Domenic out until ~4 PM; watch left plugged into MSI).

Legend: [ ] todo, [~] in progress, [x] code-complete + compiles, [V] hardware-verified.

## Autonomy boundary
- Host-testable logic: I can fully verify (compile + unit test) while away.
- Anything touching BLE radio / LVGL / the phones: I get it code-complete + building,
  then it needs the watch + phones + Domenic to VERIFY. Never reported as [V] from a
  compile alone.

## Phase 0 - ANCS bench spike
- [x] ancs consumer module (Bluedroid GATTS+GATTC): advertise ANCS solicitation,
      bond, subscribe Notification Source + Data Source, fetch attrs via Control
      Point. src/ancs.{h,cpp}. Compiles + links clean.
- [x] Serial dump path (notify::publish -> "[notify] ... title=... body=...").
- [x] Flash spike to watch (env ancs_spike, COM19/JTAG), smoke-test boot +
      advertise: serial shows "[spike] running=1 connected=0 stored=0" steadily,
      no crash / boot-loop. ancs::start() succeeds.
- [ ] VERIFY pairing to iPhone SE 2nd gen (needs Domenic + phone @ 4 PM).

WATCH STATE: currently running the ANCS SPIKE firmware (blank screen is expected -
it is a headless BLE test build). To restore normal watch:
  pio run -e twatch_ultra -t upload --upload-port <JTAG COM>
To run the 4 PM iPhone test on the spike: see 4pm-pairing-checklist below.

## Phase 1 - shared layer + mode owner   [CODE-COMPLETE, builds, boots]
- [x] notification_store (pure) - 8 host tests PASS
- [x] notify_center hub (stamps time, buzzes, mirrors to Serial)
- [x] device_mode: Daily-wear vs Field-tool arbiter - pure core 4 host tests PASS
      (device_mode_plan.cpp) + device actuation (device_mode.cpp)
- [x] notifications_screen (LVGL list + Enable/Disable toggle; gesture-back)
- [x] Tools "Notify" tile wired (bell icon, stable key "notify")
- [x] haptic-on-arrival via instance.vibrator() in notify_center (new-notif only)
- [x] Full firmware builds (Flash 91.2%, RAM 63.7%) and BOOTS stable (USB CDC up
      16s+, no panic/boot-loop). WATCH NOW RUNS THIS INTEGRATED BUILD.
- [ ] VERIFY UI at 4 PM: Tools > Notify > Enable, mode toggle, list render (needs touch)
- [ ] REMAINING (needs hardware to verify safely): full arbiter should STOP any BLE
      scanner (AirTag/Flipper/etc.) before ANCS grabs the single GAP callback slot.
      Today it relies on no scanner being active when you Enable. Documented gotcha.

## Phase 2 - iPhone / ANCS end-to-end   [CODE-COMPLETE, in final build]
- [x] full attribute fetch (app/title/message) via Control Point, Data Source
      reassembly + parse, publish to store, category mapping.
- [x] reconnect: re-advertise on disconnect. Bond persists in Bluedroid NVS.
- [x] boot + advertise VERIFIED on hardware (ancs_spike env earlier).
- [ ] VERIFY pairing + live notification on iPhone SE 2nd gen @ 4 PM.

## Phase 3 - Android via Gadgetbridge   [CODE-COMPLETE, in final build]
- [x] research done: emulate InfiniTime/PineTime; notifications via STANDARD
      Alert Notification Service (0x1811) New Alert char (0x2A46). Gadgetbridge
      supports this out of the box. No ARGUS app.
- [x] src/ans.cpp: ANS GATT server, advertises name "InfiniTime", minimal Device
      Info Service (fw 1.7.0), parses <category><count>\0<title>\0<body> -> publish.
- [x] platform picker (iPhone/Android) on the Notify screen -> device_mode starts
      ancs OR ans. Compiles + links.
- [ ] VERIFY on Pixel 9 Pro XL / Z Fold 3 / +2 with Gadgetbridge (F-Droid) @ 4 PM.

## Phase 4 - actions (optional)   [CODE-COMPLETE, in final build]
- [x] ancs::dismiss(uid) = ANCS Perform Notification Action (negative).
- [x] CLEAR button on the Notify screen: clears local store and dismisses on the
      iPhone too when connected.
- [ ] reply / call accept-decline: NOT built (deferred; ANS Notification Event
      char + ANCS positive action are the hooks). Documented, not in v1.

## BUG FOUND + FIXED during first hardware test (2026-07-21)
Symptom: watch showed "Waiting for phone to pair..." but was invisible to BOTH
iPad and Android scans. ANCS said "[ancs] started" but the device never appeared.
Root cause: ancs.cpp set adv-data and scan-response back-to-back
(esp_ble_gap_config_adv_data_raw then _config_scan_rsp_data_raw). Bluedroid
processes ONE GAP config at a time; the 2nd was dropped, so the
SCAN_RSP_DATA_RAW_SET_COMPLETE event (which triggers start_advertising) never
fired -> advertising never started. "[ancs] started" was misleading (it prints at
the end of start(), before the async adv chain).
Fix: serialize the chain - set scan-rsp only in the ADV_DATA_RAW_SET_COMPLETE
handler, start advertising in the SCAN_RSP_..._COMPLETE handler. Added diagnostics
(config return codes, adv/scan-rsp set status, ADV_START_COMPLETE) and an
ancs::is_advertising() flag latched from ADV_START_COMPLETE.
VERIFIED via ancs_spike env: serial now reports "advertising=1" (controller
confirms on-air). Same fixed ancs.cpp is in the integrated build now on the watch.
Also fixed: CLEAR button was clipped by the display's rounded corner; moved to a
bottom-centered button.

## RESULT: BOTH PLATFORMS WORKING END-TO-END (2026-07-21 PM, hardware-verified)
- Android (Pixel/Gadgetbridge/ANS): notifications + banner + persistence. VERIFIED.
- iOS (iPad/ANCS): notifications + banner. VERIFIED via real iMessage
  ("[notify] app=com.apple.MobileSMS ... body=Test").
- iOS took two fixes: (a) Service-Changed re-discovery (iOS exposes ANCS async after
  bonding), (b) the Pixel was hogging the watch's single BLE connection -> turn the
  other phone's Bluetooth OFF so iOS can see the advert and connect.
- KNOWN: one phone at a time (single BLE connection). To switch phones, turn off BT
  on the currently-bonded phone. Future: unbond-on-mode-switch or multi-connection.
- iPhone SE not yet tried but should behave like the iPad now.

## SESSION 2 (hardware, 2026-07-21 PM) - what got verified + fixed
- [x] Android/Gadgetbridge END-TO-END WORKING: emulates InfiniTime, Gadgetbridge
      connects, notification delivered + parsed ("[notify] ... title/body"). Serial
      confirmed. Advertising-config-race bug fixed (see above).
- [x] Smartwatch-style BANNER over the clock face: src/notify_popup.{h,cpp}. LVGL
      top-layer card, BLE-task -> UI-thread hand-off (notify::take_pending, spinlock),
      auto-dismiss 6s, tap to open list. VERIFIED popping up on hardware. Position
      tuned to y=72 (below the top curve) per user.
- [x] PERSISTENCE across reboot: device_mode saves enabled+platform to NVS
      (Preferences "argusnotify") on every successful toggle; device_mode_restore_boot()
      re-enables at boot (guarded: no-op if WiFi or a BLE scanner already owns the
      radio, and keeps the preference). Called last in setup(). NEEDS on-device verify.
- [x] iOS discoverability fix: put the NAME in the main adv packet (iOS lists by
      that) and moved the ANCS SOLICITATION to the scan response. NEEDS iPhone/iPad
      verify (previously "Argus Watch" never appeared in iOS Bluetooth list).

## >>> HARDWARE VERIFICATION CHECKLIST <<<
Watch currently runs the FULL integrated firmware (Phases 0-4, advertising fix).
Boots stable, advertising confirmed on-air. Background serial monitor is running
on the MSI (logs to /tmp/watch_serial.log) to capture the pairing.

iPhone SE (ANCS):
  1. On the watch: swipe to Tools, tap the "Notify" tile (bell icon).
  2. Platform picker should read "iPhone (ANCS)" (default). If not, tap it.
  3. Tap ENABLE NOTIFICATIONS. Status -> "Waiting for phone to pair...".
     - If it says "Turn WiFi off first": WiFi is on. Turn WiFi off, retry.
  4. iPhone: Settings > Bluetooth > tap "Argus Watch" > Pair (accept the prompt).
  5. Send yourself an iMessage / trigger any notification.
  6. EXPECT: watch buzzes, notification card (app/title/body) appears in the list,
     status turns green "Phone connected".
  7. Tap CLEAR: list empties and the notification clears on the iPhone too.

Android (Gadgetbridge) - Pixel 9 Pro XL first:
  1. Install Gadgetbridge from F-Droid; grant it Notification Access.
  2. On the watch Notify screen (notifications OFF), tap the platform picker until
     it reads "Android (Gadgetbridge)".
  3. Tap ENABLE NOTIFICATIONS (WiFi must be off).
  4. Gadgetbridge > add device > it should discover "InfiniTime" > pair.
  5. Trigger a notification. EXPECT: watch buzzes + card appears.
  6. Repeat on Z Fold 3 + the other two (one at a time; unpair the previous).

If ANCS misbehaves in the integrated UI, the headless spike is a fallback proof:
  pio run -e ancs_spike -t upload --upload-port <JTAG COM> ; pio device monitor

KNOWN LIMITATION to watch for: do NOT have a BLE detector (AirTag/Flipper/Skimmer/
Flock) running when you tap ENABLE - ANCS/ANS and the scanners share the one GAP
callback slot and the arbiter does not yet auto-stop scanners. Enable from a clean
state. (Full auto-stop is the one remaining integration item, needs hardware to
verify safely.)

## Hardware notes (from Domenic)
- iPhone SE 2nd gen (iOS, ANCS-capable).
- Android: Pixel 9 Pro XL, Samsung Z Fold 3, + 2 more.
- Watch: T-Watch Ultra, left plugged into MSI while out.
