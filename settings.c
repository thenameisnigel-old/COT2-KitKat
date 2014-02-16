/*
 * Copyright (C) 2012 Project Open Cannibal
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "make_ext4fs.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

#include "extendedcommands.h"
#include "nandroid.h"
#include "mounts.h"
#include "flashutils/flashutils.h"
#include "edify/expr.h"
#include <libgen.h>
#include "mtdutils/mtdutils.h"
#include "bmlutils/bmlutils.h"
#include "cutils/android_reboot.h"

#include "settings.h"
#include "settingsparser.h"

int UICOLOR0 = 0;
int UICOLOR1 = 0;
int UICOLOR2 = 0;
int UITHEME = 0;

int UI_COLOR_DEBUG = 0;

void show_cot_options_menu() {
  static char* headers[] = {
    "COT Options",
    ""
    NULL
  };
  
#define COT_OPTIONS_ITEM_RECDEBUG	0
#define COT_OPTIONS_ITEM_SETTINGS	1
  
  static char* list[3];
  list[0] = "Advanced Options";
  list[1] = "COT Settings";
  list[2] = NULL;
  
  for (;;) {
    int ret;
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item) {
      case GO_BACK:
	return;
      case COT_OPTIONS_ITEM_RECDEBUG:
	ret = show_advanced_menu();
	break;
      case COT_OPTIONS_ITEM_SETTINGS:
	show_settings_menu();
	break;
    }
  }
}

#ifdef ENABLE_LOKI
#define FIXED_ADVANCED_ENTRIES 7
#else
#define FIXED_ADVANCED_ENTRIES 6
#endif

int show_advanced_menu() {
  char buf[80];
  int i = 0, j = 0, chosen_item = 0;
  /* Default number of entries if no compile-time extras are added */
  static char* list[MAX_NUM_MANAGED_VOLUMES + FIXED_ADVANCED_ENTRIES + 1];
  
  char* primary_path = get_primary_storage_path();
  char** extra_paths = get_extra_storage_paths();
  int num_extra_volumes = get_num_extra_volumes();
  
  static const char* headers[] = { "Advanced Menu", "", NULL };
  
  memset(list, 0, MAX_NUM_MANAGED_VOLUMES + FIXED_ADVANCED_ENTRIES + 1);
  
  list[0] = "Reboot Recovery";
  
  char bootloader_mode[PROPERTY_VALUE_MAX];
  property_get("ro.bootloader.mode", bootloader_mode, "");
  if (!strcmp(bootloader_mode, "download")) {
    list[1] = "Reboot to download mode";
  } else {
    list[1] = "Reboot to bootloader";
  }
  
  list[2] = "Power Off";
  list[3] = "Wipe Dalvik Cache";
  list[4] = "COT Settings";
  list[5] = "Debugging Options";
  #ifdef ENABLE_LOKI
  list[6] = "Toggle Loki Support";
  #endif
  
  char list_prefix[] = "Partition ";
  if (can_partition(primary_path)) {
    sprintf(buf, "%s%s", list_prefix, primary_path);
    list[FIXED_ADVANCED_ENTRIES] = strdup(buf);
    j++;
  }
  
  if (extra_paths != NULL) {
    for (i = 0; i < num_extra_volumes; i++) {
      if (can_partition(extra_paths[i])) {
	sprintf(buf, "%s%s", list_prefix, extra_paths[i]);
	list[FIXED_ADVANCED_ENTRIES + j] = strdup(buf);
	j++;
      }
    }
  }
  list[FIXED_ADVANCED_ENTRIES + j] = NULL;
  
  for (;;) {
    chosen_item = get_filtered_menu_selection(headers, list, 0, 0, sizeof(list) / sizeof(char*));
    if (chosen_item == GO_BACK || chosen_item == REFRESH)
      break;
    switch (chosen_item) {
      case 0: {
	ui_print("Rebooting recovery...\n");
	reboot_main_system(ANDROID_RB_RESTART2, 0, "recovery");
	break;
      }
      case 1: {
	if (!strcmp(bootloader_mode, "download")) {
	  ui_print("Rebooting to download mode...\n");
	  reboot_main_system(ANDROID_RB_RESTART2, 0, "download");
	} else {
	  ui_print("Rebooting to bootloader...\n");
	  reboot_main_system(ANDROID_RB_RESTART2, 0, "bootloader");
	}
	break;
      }
      case 2: {
	ui_print("Shutting down...\n");
	reboot_main_system(ANDROID_RB_POWEROFF, 0, 0);
	break;
      }
      case 3: {
	if (0 != ensure_path_mounted("/data"))
	  break;
	if (volume_for_path("/sd-ext") != NULL)
	  ensure_path_mounted("/sd-ext");
	ensure_path_mounted("/cache");
	if (confirm_selection("Confirm wipe?", "Yes - Wipe Dalvik Cache")) {
	  __system("rm -r /data/dalvik-cache");
	  __system("rm -r /cache/dalvik-cache");
	  __system("rm -r /sd-ext/dalvik-cache");
	  ui_print("Dalvik Cache wiped.\n");
	}
	ensure_path_unmounted("/data");
	break;
      }
      case 4:
      {
	show_settings_menu();
	break;
      }
      case 5:
      {
	show_recovery_debugging_menu();
	break;
      }
      #ifdef ENABLE_LOKI
      case 6:
	toggle_loki_support();
	break;
	#endif
      default:
	partition_sdcard(list[chosen_item] + strlen(list_prefix));
	break;
    }
  }
  
  for (; j > 0; --j) {
    free(list[FIXED_ADVANCED_ENTRIES + j - 1]);
  }
  return chosen_item;
}

void show_recovery_debugging_menu() {
  static char* headers[] = { "Debugging Options",
    "",
    NULL
  };
  
  static char* list[] = { "Report Error",
    "Key Test",
    "Show Log",
    "Toggle UI Debugging",
    NULL
  };
  
  for (;;) {
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    if(chosen_item == GO_BACK)
      break;
    switch(chosen_item) {
      case 0:
      {
	handle_failure(1);
	break;
      }
      case 1:
      {
	ui_print("Outputting key codes.\n");
	ui_print("Go back to end debugging.\n");
	struct keyStruct{
	  int code;
	  int x;
	  int y;
	}*key;
	int action;
	do
	{
	  if(key->code == ABS_MT_POSITION_X) {
	    action = device_handle_mouse(key, 1);
	    ui_print("Touch: X: %d\tY: %d\n", key->x, key->y);
	  } else {
	    action = device_handle_key(key->code, 1);
	    ui_print("Key: %x\n", key->code);
	  }
	}
	while (action != GO_BACK);
	break;
      }
      case 2:
      {
	ui_printlogtail(12);
	break;
      }
      case 3:
      {
	toggle_ui_debugging();
	break;
      }
    }
  }
}

void show_settings_menu() {
  static char* headers[] = {
    "COT Settings",
    "",
    NULL
  };
  
  #define SETTINGS_ITEM_THEME         0
  #define SETTINGS_ITEM_ORS_REBOOT    1
  #define SETTINGS_ITEM_ORS_WIPE      2
  #define SETTINGS_ITEM_NAND_PROMPT   3
  #define SETTINGS_ITEM_SIGCHECK      4
  #define SETTINGS_ITEM_DEV_OPTIONS   5
  
  static char* list[6];
  
  list[0] = "Theme";
  if (orsreboot == 1) {
    list[1] = "Disable forced reboots";
  } else {
    list[1] = "Enable forced reboots";
  }
  if (orswipeprompt == 1) {
    list[2] = "Disable wipe prompt";
  } else {
    list[2] = "Enable wipe prompt";
  }
  if (backupprompt == 1) {
    list[3] = "Disable zip flash nandroid prompt";
  } else {
    list[3] = "Enable zip flash nandroid prompt";
  }
  if (signature_check_enabled == 1) {
    list[4] = "Disable md5 signature check";
  } else {
    list[4] = "Enable md5 signature check";
  }
  list[5] = NULL;
  
  for (;;) {
    int chosen_item = get_menu_selection(headers, list, 0, 0);
    switch (chosen_item) {
      case GO_BACK:
	return;
      case SETTINGS_ITEM_THEME:
      {
	static char* ui_colors[] = {"Hydro (default)",
	  "Blood Red",
	  "Key Lime Pie",
	  "Citrus Orange",
	  "Dooderbutt Blue",
	  NULL
	};
	static char* ui_header[] = {"COT Theme", "", NULL};
	
	int ui_color = get_menu_selection(ui_header, ui_colors, 0, 0);
	if(ui_color == GO_BACK)
	  continue;
	else {
	  switch(ui_color) {
	    case 0:
	      currenttheme = "hydro";
	      break;
	    case 1:
	      currenttheme = "bloodred";
	      break;
	    case 2:
	      currenttheme = "keylimepie";
	      break;
	    case 3:
	      currenttheme = "citrusorange";
	      break;
	    case 4:
	      currenttheme = "dooderbuttblue";
	      break;
	  }
	  break;
	}
      }
	    case SETTINGS_ITEM_ORS_REBOOT:
	    {
	      if (orsreboot == 1) {
		ui_print("Disabling forced reboots.\n");
		list[1] = "Enable forced reboots";
		orsreboot = 0;
	      } else {
		ui_print("Enabling forced reboots.\n");
		list[1] = "Disable forced reboots";
		orsreboot = 1;
	      }
	      break;
	    }
	    case SETTINGS_ITEM_ORS_WIPE:
	    {
	      if (orswipeprompt == 1) {
		ui_print("Disabling wipe prompt.\n");
		list[2] = "Enable wipe prompt";
		orswipeprompt = 0;
	      } else {
		ui_print("Enabling wipe prompt.\n");
		list[2] = "Disable wipe prompt";
		orswipeprompt = 1;
	      }
	      break;
	    }
	    case SETTINGS_ITEM_NAND_PROMPT:
	    {
	      if (backupprompt == 1) {
		ui_print("Disabling zip flash nandroid prompt.\n");
		list[3] = "Enable zip flash nandroid prompt";
		backupprompt = 0;
	      } else {
		ui_print("Enabling zip flash nandroid prompt.\n");
		list[3] = "Disable zip flash nandroid prompt";
		backupprompt = 1;
	      }
	      break;
	    }
	    case SETTINGS_ITEM_SIGCHECK:
	    {
	      if (signature_check_enabled == 1) {
		ui_print("Disabling md5 signature check.\n");
		list[4] = "Enable md5 signature check";
		signature_check_enabled = 0;
	      } else {
		ui_print("Enabling md5 signature check.\n");
		list[4] = "Disable md5 signature check";
		signature_check_enabled = 1;
	      }
	      break;
	    }
	    default:
	      return;
    }
    update_cot_settings();
  }
}

void ui_dyn_background()
{
  if(UI_COLOR_DEBUG) {
    LOGI("%s %i\n", "DYN_BG:", UITHEME);
  }
  switch(UITHEME) {
    case BLOOD_RED_UI:
      ui_set_background(BACKGROUND_ICON_BLOODRED);
      break;
    case KEY_LIME_PIE_UI:
      ui_set_background(BACKGROUND_ICON_KEYLIMEPIE);
      break;
    case CITRUS_ORANGE_UI:
      ui_set_background(BACKGROUND_ICON_CITRUSORANGE);
      break;
    case DOODERBUTT_BLUE_UI:
      ui_set_background(BACKGROUND_ICON_DOODERBUTT);
      break;
      // Anything else is the clockwork icon
    default:
      ui_set_background(BACKGROUND_ICON_CLOCKWORK);
      break;
  }
}