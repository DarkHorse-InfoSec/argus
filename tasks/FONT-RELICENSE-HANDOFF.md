ARGUS Watch - OFL font replacement completed (handoff back to Claude Code)
============================================================================
Repository: argus-watch  (T-Watch Ultra, ESP32-S3, LVGL 9)
Work with argus-watch as your working directory; all paths below are relative to it.
Build:  pio run -e twatch_ultra   (from the repo root; the pio CLI may not be on PATH)
Flash:  ...pio.exe run -d ... -e twatch_ultra -t upload --upload-port COM19
COM ports: DOWNLOAD mode = 303A:1001 = COM19 (only this flashes; BOOT+RESET to enter). App CDC = 303A:8227 = COM20.
GIT: branch darkhorse-argus. The font work is present in the working tree and is NOT committed or pushed. Do not
     discard it. Preserve every untracked tasks/*-HANDOFF.md document. Do not commit or push unless Domenic explicitly
     asks. No Co-Authored-By lines and no em dashes.

READ FIRST: tasks/ROADMAP.md, tasks/FLASHING-NOTES.md, README.md, and this entire handoff.


>>> STATUS: IMPLEMENTED, FLASHED, AND APPROVED ON HARDWARE <<<
Do not redo the font conversion or revert these files. Review the existing working-tree changes, preserve them, and
include them in the next intentional release commit when Domenic requests one.

The commercial-font release blocker is resolved. Every known Bank Gothic bitmap raster was replaced, all font source
licenses were audited, the final firmware built and flashed successfully, and Domenic approved the result on the
physical watch on 2026-07-25. The clock and wallpaper came up correctly. He confirmed the boot branding, large clock,
date, Time/Tools tile labels, and Settings typography all look great.


>>> WHAT WAS ACTUALLY WRONG <<<
The original handoff correctly identified these three commercial Bank Gothic brand rasters:
  1. src/font_dh_ui.c
  2. src/font_dh_argus.c
  3. src/font_dh_wordmark.c

However, its statement that the Montserrat clock files were already OFL was incorrect. Inspection found that all five
files named lv_font_montserrat_clock_* were actually generated from BankGothicMediumBT.ttf:
  4. src/lv_font_montserrat_clock_56.c
  5. src/lv_font_montserrat_clock_72.c
  6. src/lv_font_montserrat_clock_96.c
  7. src/lv_font_montserrat_clock_110.c
  8. src/lv_font_montserrat_clock_116.c

Leaving those five files untouched would have left commercial Bank Gothic glyph data in the public firmware despite
their misleading Montserrat symbol and filenames.


>>> IMPLEMENTED FIX <<<
Brand fonts:
  - Regenerated src/font_dh_ui.c from Saira Condensed SemiBold at 32 px, 4 bpp, no compression, printable ASCII
    range 0x20-0x7F. Preserved public symbol font_dh_ui.
  - Regenerated src/font_dh_argus.c from Saira Condensed SemiBold at 88 px, 4 bpp, no compression, symbols ARGUS.
    Preserved public symbol font_dh_argus.
  - Regenerated src/font_dh_wordmark.c from Saira Condensed SemiBold at 40 px, 4 bpp, no compression, symbols
    DARKHORSE. Preserved public symbol font_dh_wordmark.

Digital clock:
  - Regenerated all five lv_font_montserrat_clock_* files from the verified Montserrat Medium TTF bundled with LVGL.
  - Preserved sizes 56, 72, 96, 110, and 116 px, 4 bpp output, the exact 15-glyph subset
    " 0123456789:AMP", and every existing public C symbol.
  - Updated tools/gen_clock_font.py comments/output to accurately identify Montserrat and use ASCII punctuation.

No public font symbol was renamed, so existing call sites remain compatible.


>>> SOURCE AND LICENSE AUDIT <<<
Saira Condensed SemiBold:
  - Official Google Fonts OFL source.
  - Source TTF SHA-256:
    30F8ED4D078211003A9715C80C51CE031BAB5C9A17E8771182E4C4599205634B
  - The temporary source TTF was removed after raster generation and is not redistributed.

Montserrat Medium:
  - Source:
    .pio/libdeps/twatch_ultra/lvgl/scripts/built_in_font/Montserrat-Medium.ttf
  - Source TTF SHA-256:
    421F26B23E2BE6B98373D32ACD3CB2897B154D4BF0A77D26534CE476E4CBED53

Added exact SIL Open Font License 1.1 notices:
  - licenses/SairaCondensed-OFL.txt
  - licenses/Montserrat-OFL.txt
  - licenses/Orbitron-OFL.txt
  - licenses/VT323-OFL.txt

README.md now credits all four OFL families. Orbitron is the existing font used for the date, Time/Tools tile labels,
TOOLS heading, and most Settings body text. VT323 remains the terminal/numeric readout face. The repository MIT LICENSE
was not modified.

Final text audit:
  rg -n -i "Bank[ -]?Gothic|BankGothicMediumBT|_Brand/fonts" src tools README.md platformio.ini

The audit returns no matches. Generated font headers identify only Saira Condensed, Montserrat, Orbitron, or VT323.


>>> BUILD AND BINARY VERIFICATION <<<
Final PlatformIO build:
  - RAM:   172,960 / 327,680 bytes, 52.8%
  - Flash: 2,965,033 / 3,145,728 bytes, 94.3%
  - Flash headroom: 180,695 bytes
  - firmware.bin SHA-256:
    2952A4407A4293E0586C568546EF791B7C8C0A2DFD387E628B75D9A70C034885
  - firmware.elf SHA-256:
    9215BAC44D19AE488B2D2A37A515DF92B66F39BD666110F8C7DB624BC7D2C124

The final ELF contains all eight required public font symbols:
  font_dh_argus
  font_dh_ui
  font_dh_wordmark
  lv_font_montserrat_clock_56
  lv_font_montserrat_clock_72
  lv_font_montserrat_clock_96
  lv_font_montserrat_clock_110
  lv_font_montserrat_clock_116

Upload to COM19 completed successfully and esptool verified every written region by hash.


>>> HARDWARE RESULT <<<
Domenic first approved the Saira Condensed boot and interface font weight. After the additional clock-font discovery,
the fully OFL build was flashed again with the SD card installed. The clock and wallpaper returned. Domenic then
confirmed that everything looks great, including the unchanged Orbitron date, Time/Tools tile labels, and Settings
menu typography. No missing glyphs, clipping, overlap, or spacing problem was reported.


>>> FILES IN THIS FONT-LICENSING CHANGE <<<
Expected tracked modifications:
  README.md
  src/font_dh_argus.c
  src/font_dh_ui.c
  src/font_dh_wordmark.c
  src/lv_font_montserrat_clock_56.c
  src/lv_font_montserrat_clock_72.c
  src/lv_font_montserrat_clock_96.c
  src/lv_font_montserrat_clock_110.c
  src/lv_font_montserrat_clock_116.c
  src/loot_screen.cpp
  src/main.cpp
  src/send_message_screen.cpp
  src/theme.h
  tools/gen_clock_font.py
  tasks/ROADMAP.md

Expected new files:
  licenses/SairaCondensed-OFL.txt
  licenses/Montserrat-OFL.txt
  licenses/Orbitron-OFL.txt
  licenses/VT323-OFL.txt
  tasks/FONT-RELICENSE-HANDOFF.md


>>> CLAUDE CODE NEXT ACTION <<<
1. Inspect and preserve the current working-tree changes.
2. Run git diff --check, the Bank Gothic text audit above, and a normal PlatformIO build before release integration.
3. Confirm the binary hash if the source has not changed. A later source change will naturally produce a new hash.
4. Do not regenerate the fonts, rename their public symbols, broaden their subsets, or restore any commercial source.
5. Do not commit or push until Domenic explicitly asks.
