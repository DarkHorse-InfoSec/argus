# Flashing & serial gotchas - T-Watch Ultra (ESP32-S3)

Copied from the prior developer's notes so this is in-repo for a new agent. Verified
2026-07-21 on the MSI over USB.

## Two USB identities, and they swap
- **App running:** enumerates as USB-CDC `VID:PID=303A:8227` (e.g. COM20). This port
  VANISHES the instant esptool resets the chip, so `pio run -t upload` on it fails with
  `PermissionError(13, 'A device attached to the system is not functioning.')`
  mid-"Connecting...".
- **Reset/download:** the ESP32-S3 USB-Serial-JTAG enumerates as `303A:1001` (e.g.
  COM19). This one SURVIVES the reset.
- **Flash via the `303A:1001` JTAG port** (`--upload-port COM19`). Reliable. Run
  `pio device list` to see which COM is which by VID:PID. Enter download mode via
  BOOT+RESET.

## Reading serial without bricking into download mode
- Do NOT hand-toggle DTR/RTS when opening the port for reading. On the S3 USB-JTAG the
  wrong DTR/RTS combo forces ROM DOWNLOAD mode (`rst:0x15 ... boot:0x23 (DOWNLOAD)`,
  "waiting for download") and the app never runs. Open with DTR=False/RTS=False
  (pyserial). `tools/panicmon.py` already does this.
- To recover from stuck download mode / force the app to boot:
  `python -m esptool --chip esp32s3 -p COM19 --before default_reset --after hard_reset flash_id`
  (`--after hard_reset` boots the app).
- After the app boots, its CDC re-appears on `303A:8227` (COM20). Read that passively.

## Why there is no panic backtrace on COM20
USB flags are in `boards/lilygo-t-watch-ultra.json` (NOT platformio.ini):
`-DARDUINO_USB_MODE=0` (TinyUSB OTG) and `-DARDUINO_USB_CDC_ON_BOOT=1`. In OTG mode the
hardware USB-Serial-JTAG panic console is NOT active, so the ESP-IDF panic text never
reaches the app CDC. Options to get the backtrace:
1. Enable ESP core-dump to flash (partition + CONFIG_ESP_COREDUMP_*), reproduce, read
   with esp-coredump against `.pio/build/twatch_ultra/firmware.elf`. Preferred - works
   on battery, no cable needed to capture.
2. Temporarily flip to `-DARDUINO_USB_MODE=1` to surface the JTAG console. TRADE-OFF:
   this changes/kills the TinyUSB composite (CDC + the USB SD mass-storage feature), so
   use it only as a throwaway diagnostic build, not for shipping.
Decode raw addresses with `xtensa-esp32s3-elf-addr2line` against firmware.elf.

## Core-dump extraction in this repo
- The Arduino-ESP32 S3 SDK used by PlatformIO already has
  `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` and ELF output enabled.
- `app3M_fat9M_16MB.csv` reserves a 64 KB `coredump` partition at `0xFF0000`.
- Put the watch in download mode on COM19, then run:
  `powershell -ExecutionPolicy Bypass -File .\tools\read_coredump.ps1 -Port COM19`
- The helper reads that fixed partition with esptool because `esp-coredump` direct
  flash mode otherwise expects a full ESP-IDF checkout and `parttool.py`, which the
  PlatformIO Arduino package does not include.
- The helper verifies the flashed app against the local `firmware.bin` before
  decoding with `firmware.elf`. If the application SHA does not match, keep the
  saved raw/core files and do not trust addresses mapped through another ELF.
