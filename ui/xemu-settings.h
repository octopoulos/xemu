/*
 * xemu Settings Management
 *
 * Primary storage for non-volatile user configuration. Basic key-value storage
 * that gets saved to an INI file. All entries should be accessed through the
 * appropriate getter/setter functions.
 *
 * Copyright (C) 2020-2021 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef XEMU_SETTINGS_H
#define XEMU_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_KEYB_MAPPING	"4,5,27,28,80,82,79,81,42,40,30,31,32,33,34,,,,,,,,26,18,22,9,7,8,13,15,14,12"
// value >= 32 = axis
#define DEFAULT_PAD_MAPPING 	"0,1,2,3,13,11,14,12,4,6,9,10,7,8,5,,,,,,,,36,37,32,33,32,33,34,35,34,35"

enum xemu_settings_keys {
	XEMU_SETTINGS_SYSTEM_FLASH_PATH,
	XEMU_SETTINGS_SYSTEM_BOOTROM_PATH,
	XEMU_SETTINGS_SYSTEM_HDD_PATH,
	XEMU_SETTINGS_SYSTEM_EEPROM_PATH,
	XEMU_SETTINGS_SYSTEM_DVD_PATH,
	XEMU_SETTINGS_SYSTEM_MEMORY,
	XEMU_SETTINGS_SYSTEM_SHORT_ANIMATION,
	XEMU_SETTINGS_SYSTEM_HARD_FPU,
	XEMU_SETTINGS_AUDIO_USE_DSP,
	XEMU_SETTINGS_DISPLAY_SCALE,
	XEMU_SETTINGS_DISPLAY_UI_SCALE,
	XEMU_SETTINGS_DISPLAY_RENDER_SCALE,
	XEMU_SETTINGS_INPUT_CONTROLLER_1_GUID,
	XEMU_SETTINGS_INPUT_CONTROLLER_2_GUID,
	XEMU_SETTINGS_INPUT_CONTROLLER_3_GUID,
	XEMU_SETTINGS_INPUT_CONTROLLER_4_GUID,
	XEMU_SETTINGS_INPUT_CONTROLLER_1_KEYB,
	XEMU_SETTINGS_INPUT_CONTROLLER_2_KEYB,
	XEMU_SETTINGS_INPUT_CONTROLLER_3_KEYB,
	XEMU_SETTINGS_INPUT_CONTROLLER_4_KEYB,
	XEMU_SETTINGS_INPUT_CONTROLLER_1_PAD,
	XEMU_SETTINGS_INPUT_CONTROLLER_2_PAD,
	XEMU_SETTINGS_INPUT_CONTROLLER_3_PAD,
	XEMU_SETTINGS_INPUT_CONTROLLER_4_PAD,
	XEMU_SETTINGS_NETWORK_NET_ENABLED,
	XEMU_SETTINGS_NETWORK_NET_BACKEND,
	XEMU_SETTINGS_NETWORK_NET_LOCAL_ADDR,
	XEMU_SETTINGS_NETWORK_NET_REMOTE_ADDR,
	XEMU_SETTINGS_NETWORK_NET_PCAP_IFACE,
	XEMU_SETTINGS_MISC_USER_TOKEN,
	XEMU_SETTINGS_MISC_CHECK_FOR_UPDATE,
	XEMU_SETTINGS__COUNT,
	XEMU_SETTINGS_INVALID = -1
};

enum DISPLAY_SCALE
{
    DISPLAY_SCALE_CENTER,
    DISPLAY_SCALE_SCALE,
    DISPLAY_SCALE_WS169,
    DISPLAY_SCALE_FS43,
    DISPLAY_SCALE_STRETCH,
    DISPLAY_SCALE__COUNT,
    DISPLAY_SCALE_INVALID = -1
};

enum xemu_net_backend {
	XEMU_NET_BACKEND_USER,
	XEMU_NET_BACKEND_SOCKET_UDP,
	XEMU_NET_BACKEND_PCAP,
	XEMU_NET_BACKEND__COUNT,
	XEMU_NET_BACKEND_INVALID = -1
};

// Determine whether settings were loaded or not
int xemu_settings_did_fail_to_load(void);

// Override the default config file paths
void xemu_settings_set_path(const char *path);

// Get path of the config file on disk
const char *xemu_settings_get_path(void);

// Get path of the default generated eeprom file on disk
const char *xemu_settings_get_default_eeprom_path(void);

// Load config file from disk, or load defaults
void xemu_settings_load(void);

// Save config file to disk
int xemu_settings_save(void);

// Config item setters/getters
int xemu_settings_set_string(enum xemu_settings_keys key, const char *str);
int xemu_settings_get_string(enum xemu_settings_keys key, const char **str);
int xemu_settings_set_int(enum xemu_settings_keys key, int val);
int xemu_settings_get_int(enum xemu_settings_keys key, int *val);
int xemu_settings_set_float(enum xemu_settings_keys key, float val);
int xemu_settings_get_float(enum xemu_settings_keys key, float *val);
int xemu_settings_set_bool(enum xemu_settings_keys key, int val);
int xemu_settings_get_bool(enum xemu_settings_keys key, int *val);
int xemu_settings_set_enum(enum xemu_settings_keys key, int val);
int xemu_settings_get_enum(enum xemu_settings_keys key, int *val);

#ifdef __cplusplus
}
#endif

#endif
