#pragma once
#include <lvgl.h>

// DRAFT (not yet in the build - lives in tasks/drafts/). Move to src/ + wire the
// integration lines from tasks/OFFENSE-BUILD-DESIGNS.md to activate. Adaptation
// notes vs the raw draft: title font_dh_ui (Bank Gothic, banned) -> font_dh_label_28;
// verify lv_event_get_target_obj against the pinned LVGL 9 version before building.
//
// Loot manager (OFFENSE mode only): lists captured engagement artifacts on the
// SD card - /pwn (*.pcap), /Wardrive (*.csv), /PingSweeps (*.txt), /Screenshots
// - with per-file sizes and per-directory totals, and lets the operator offload
// (via USB-SD mass storage) or shred them. Wiping reuses the Tier-1 shred from
// offense_wipe (overwrite-then-unlink), so operator wipes and the duress wipe
// destroy data identically. Reached from the Loot tile in the Offense grid.

void loot_screen_create();
void loot_screen_show();
bool loot_screen_is_active();
