/*
 * xemu Settings Management
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

#include "qemu/osdep.h"
#include <stdlib.h>
#include <SDL_filesystem.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <glib.h>

#include "xemu-settings.h"
#include "inih/ini.c" // FIXME

enum config_types {
	CONFIG_TYPE_STRING,
	CONFIG_TYPE_INT,
	CONFIG_TYPE_FLOAT,
	CONFIG_TYPE_BOOL,
	CONFIG_TYPE_ENUM,
	CONFIG_TYPE__MAX
};

struct xemu_settings {
	// [system]
	char *flash_path;
	char *bootrom_path;
	char *hdd_path;
	char *dvd_path;
	char *eeprom_path;
	int   memory;
	int   short_animation; // Boolean
	int   hard_fpu; // Boolean

	// [audio]
	int use_dsp; // Boolean

	// [display]
	int scale;
	float ui_scale;
	int render_scale;

	// [input]
	char *controller_1_guid;
	char *controller_2_guid;
	char *controller_3_guid;
	char *controller_4_guid;
	char *controller_1_keyb;
	char *controller_2_keyb;
	char *controller_3_keyb;
	char *controller_4_keyb;
	char *controller_1_pad;
	char *controller_2_pad;
	char *controller_3_pad;
	char *controller_4_pad;

	// [network]
	int   net_enabled; // Boolean
	int   net_backend;
	char *net_local_addr;
	char *net_remote_addr;
	char *net_pcap_iface;

	// [misc]
	char *user_token;
	int check_for_update; // Boolean
};

struct enum_str_map {
	int         value;
	const char *str;
};

static const struct enum_str_map display_scale_map[DISPLAY_SCALE__COUNT+1] = {
	{ DISPLAY_SCALE_CENTER,  "center"  },
	{ DISPLAY_SCALE_SCALE,   "scale"   },
	{ DISPLAY_SCALE_WS169,   "scale_ws169" },
	{ DISPLAY_SCALE_FS43,    "scale_fs43" },
	{ DISPLAY_SCALE_STRETCH, "stretch" },
	{ 0,                     NULL      },
};

static const struct enum_str_map net_backend_map[XEMU_NET_BACKEND__COUNT+1] = {
	{ XEMU_NET_BACKEND_USER,       "user" },
	{ XEMU_NET_BACKEND_SOCKET_UDP, "udp"  },
	{ XEMU_NET_BACKEND_PCAP,       "pcap" },
	{ 0,                           NULL   },
};

#define X_BOOL(section, name, def) \
	{ CONFIG_TYPE_BOOL, #section, #name, offsetof(struct xemu_settings, name), {.default_bool = def} }
#define X_ENUM(section, name, def, enum_map) \
	{ CONFIG_TYPE_ENUM, #section, #name, offsetof(struct xemu_settings, name), {.default_int = def}, {}, {}, enum_map }
#define X_FLOAT(section, name, def, vmin, vmax) \
	{ CONFIG_TYPE_FLOAT, #section, #name, offsetof(struct xemu_settings, name), {.default_float = def}, {.min_float = vmin}, {.max_float = vmax} }
#define X_INT(section, name, def, vmin, vmax) \
	{ CONFIG_TYPE_INT, #section, #name, offsetof(struct xemu_settings, name), {.default_int = def}, {.min_int = vmin}, {.max_int = vmax} }
#define X_STRING(section, name, def) \
	{ CONFIG_TYPE_STRING, #section, #name, offsetof(struct xemu_settings, name), {.default_str = def} }

struct config_offset_table {
	enum config_types type;
	const char *section;
	const char *name;
	ptrdiff_t offset;
	union {
		const char *default_str;
		int default_int;
		float default_float;
		int default_bool;
	};
	union {
		int min_int;
		float min_float;
	};
	union {
		int max_int;
		float max_float;
	};
	const struct enum_str_map *enum_map;
} config_items[XEMU_SETTINGS__COUNT] = {
	// Please keep organized by section
	[XEMU_SETTINGS_SYSTEM_FLASH_PATH]       = X_STRING(system , flash_path       , ""),
	[XEMU_SETTINGS_SYSTEM_BOOTROM_PATH]     = X_STRING(system , bootrom_path     , ""),
	[XEMU_SETTINGS_SYSTEM_HDD_PATH]         = X_STRING(system , hdd_path         , ""),
	[XEMU_SETTINGS_SYSTEM_DVD_PATH]         = X_STRING(system , dvd_path         , ""),
	[XEMU_SETTINGS_SYSTEM_EEPROM_PATH]      = X_STRING(system , eeprom_path      , ""),
	[XEMU_SETTINGS_SYSTEM_MEMORY]           = X_INT   (system , memory           , 64, 64, 128),
	[XEMU_SETTINGS_SYSTEM_SHORT_ANIMATION]  = X_BOOL  (system , short_animation  , 0),
	[XEMU_SETTINGS_SYSTEM_HARD_FPU]         = X_BOOL  (system , hard_fpu         , 1),

	[XEMU_SETTINGS_AUDIO_USE_DSP]           = X_BOOL  (audio  , use_dsp          , 0),

	[XEMU_SETTINGS_DISPLAY_SCALE]           = X_ENUM  (display, scale            , DISPLAY_SCALE_SCALE, display_scale_map),
	[XEMU_SETTINGS_DISPLAY_UI_SCALE]        = X_FLOAT (display, ui_scale         , 1.0f, 1.0f, 4.0f),
	[XEMU_SETTINGS_DISPLAY_RENDER_SCALE]    = X_INT   (display, render_scale     , 1   , 1   , 10),

	[XEMU_SETTINGS_INPUT_CONTROLLER_1_GUID] = X_STRING(input  , controller_1_guid, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_2_GUID] = X_STRING(input  , controller_2_guid, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_3_GUID] = X_STRING(input  , controller_3_guid, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_4_GUID] = X_STRING(input  , controller_4_guid, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_1_KEYB] = X_STRING(input  , controller_1_keyb, DEFAULT_KEYB_MAPPING),
	[XEMU_SETTINGS_INPUT_CONTROLLER_2_KEYB] = X_STRING(input  , controller_2_keyb, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_3_KEYB] = X_STRING(input  , controller_3_keyb, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_4_KEYB] = X_STRING(input  , controller_4_keyb, ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_1_PAD]  = X_STRING(input  , controller_1_pad , DEFAULT_PAD_MAPPING),
	[XEMU_SETTINGS_INPUT_CONTROLLER_2_PAD]  = X_STRING(input  , controller_2_pad , ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_3_PAD]  = X_STRING(input  , controller_3_pad , ""),
	[XEMU_SETTINGS_INPUT_CONTROLLER_4_PAD]  = X_STRING(input  , controller_4_pad , ""),

	[XEMU_SETTINGS_NETWORK_NET_ENABLED]     = X_BOOL  (network, net_enabled      , 0),
	[XEMU_SETTINGS_NETWORK_NET_BACKEND]     = X_ENUM  (network, net_backend      , XEMU_NET_BACKEND_USER, net_backend_map),
	[XEMU_SETTINGS_NETWORK_NET_LOCAL_ADDR]  = X_STRING(network, net_local_addr   , "0.0.0.0:9368"),
	[XEMU_SETTINGS_NETWORK_NET_REMOTE_ADDR] = X_STRING(network, net_remote_addr  , "1.2.3.4:9368"),
	[XEMU_SETTINGS_NETWORK_NET_PCAP_IFACE]  = X_STRING(network, net_pcap_iface   , ""),

	[XEMU_SETTINGS_MISC_USER_TOKEN]         = X_STRING(misc   , user_token       , ""),
	[XEMU_SETTINGS_MISC_CHECK_FOR_UPDATE]   = X_BOOL  (misc   , check_for_update , -1),
};

static const char *settings_path;
static const char *filename = "xemu.ini";
static struct xemu_settings *g_settings;
static int settings_failed_to_load = 0;

static void *xemu_settings_get_field(enum xemu_settings_keys key, enum config_types type)
{
	assert(key < XEMU_SETTINGS__COUNT);
	assert(config_items[key].type == type);
	return (void *)((char*)g_settings + config_items[key].offset);
}

int xemu_settings_set_string(enum xemu_settings_keys key, const char *str)
{
	char **field_str = (char **)xemu_settings_get_field(key, CONFIG_TYPE_STRING);
	if (*field_str) {
		if (!strcmp(*field_str, str))
			return 0;
		free(*field_str);
	}
	// FIXME: is it freed at program exit?
	*field_str = strdup(str);
	return 0;
}

int xemu_settings_get_string(enum xemu_settings_keys key, const char **str)
{
	*str = *(const char **)xemu_settings_get_field(key, CONFIG_TYPE_STRING);
	return 0;
}

int xemu_settings_set_int(enum xemu_settings_keys key, int val)
{
	int *field_int = (int *)xemu_settings_get_field(key, CONFIG_TYPE_INT);
	struct config_offset_table* config = &config_items[key];
	if (config->min_int < config->max_int) {
		if (val < config->min_int) val = config->min_int;
		if (val > config->max_int) val = config->max_int;
	}
	*field_int = val;
	return 0;
}

int xemu_settings_get_int(enum xemu_settings_keys key, int *val)
{
	int value = *(int *)xemu_settings_get_field(key, CONFIG_TYPE_INT);
	struct config_offset_table* config = &config_items[key];
	if (config->min_int < config->max_int) {
		if (value < config->min_int) value = config->min_int;
		if (value > config->max_int) value = config->max_int;
	}
	*val = value;
	return 0;
}

int xemu_settings_set_float(enum xemu_settings_keys key, float val)
{
	float *field_float = (float *)xemu_settings_get_field(key, CONFIG_TYPE_FLOAT);
	struct config_offset_table* config = &config_items[key];
	if (config->min_float < config->max_float) {
		if (val < config->min_float) val = config->min_float;
		if (val > config->max_float) val = config->max_float;
	}
	*field_float = val;
	return 0;
}

int xemu_settings_get_float(enum xemu_settings_keys key, float *val)
{
	float value = *(float *)xemu_settings_get_field(key, CONFIG_TYPE_FLOAT);
	struct config_offset_table* config = &config_items[key];
	if (config->min_float < config->max_float) {
		if (value < config->min_float) value = config->min_float;
		if (value > config->max_float) value = config->max_float;
	}
	*val = value;
	return 0;
}

int xemu_settings_set_bool(enum xemu_settings_keys key, int val)
{
	int *field_int = (int *)xemu_settings_get_field(key, CONFIG_TYPE_BOOL);
	*field_int = val;
	return 0;
}

int xemu_settings_get_bool(enum xemu_settings_keys key, int *val)
{
	*val = *(int *)xemu_settings_get_field(key, CONFIG_TYPE_BOOL);
	return 0;
}

int xemu_settings_set_enum(enum xemu_settings_keys key, int val)
{
	int *field_int = (int *)xemu_settings_get_field(key, CONFIG_TYPE_ENUM);
	*field_int = val;
	return 0;
}

int xemu_settings_get_enum(enum xemu_settings_keys key, int *val)
{
	int value = *(int *)xemu_settings_get_field(key, CONFIG_TYPE_ENUM);
	*val = value;
	return 0;
}

static bool xemu_settings_detect_portable_mode(void)
{
	bool val = false;
	char *portable_path = g_strdup_printf("%s%s", SDL_GetBasePath(), filename);
	FILE *tmpfile;
	if ((tmpfile = qemu_fopen(portable_path, "r"))) {
		fclose(tmpfile);
		val = true;
	}

	free(portable_path);
	return val;
}

void xemu_settings_set_path(const char *path)
{
	assert(path != NULL);
	assert(settings_path == NULL);
	settings_path = path;
	fprintf(stderr, "%s: config path: %s\n", __func__, settings_path);
}

const char *xemu_settings_get_path(void)
{
	if (settings_path != NULL) {
		return settings_path;
	}

	char *base = xemu_settings_detect_portable_mode()
	             ? SDL_GetBasePath()
	             : SDL_GetPrefPath("xemu", "xemu");
	assert(base != NULL);
	settings_path = g_strdup_printf("%s%s", base, filename);
	SDL_free(base);
	fprintf(stderr, "%s: config path: %s\n", __func__, settings_path);
	return settings_path;
}

const char *xemu_settings_get_default_eeprom_path(void)
{
	static char *eeprom_path = NULL;
	if (eeprom_path != NULL) {
		return eeprom_path;
	}

	char *base = xemu_settings_detect_portable_mode()
	             ? SDL_GetBasePath()
	             : SDL_GetPrefPath("xemu", "xemu");
	assert(base != NULL);
	eeprom_path = g_strdup_printf("%s%s", base, "eeprom.bin");
	SDL_free(base);
	return eeprom_path;
}

static int xemu_enum_str_to_int(const struct enum_str_map *map, const char *str, int *value)
{
	for (int i = 0; map[i].str != NULL; i++) {
		if (strcmp(map[i].str, str) == 0) {
			*value = map[i].value;
			return 0;
		}
	}

	return -1;
}

static int xemu_enum_int_to_str(const struct enum_str_map *map, int value, const char **str)
{
	for (int i = 0; map[i].str != NULL; i++) {
		if (map[i].value == value) {
			*str = map[i].str;
			return 0;
		}
	}

	return -1;
}


static enum xemu_settings_keys xemu_key_from_name(const char *section, const char *name)
{
	for (int i = 0; i < XEMU_SETTINGS__COUNT; i++) {
		if ((strcmp(section, config_items[i].section) == 0) &&
			(strcmp(name, config_items[i].name) == 0)) {
			return i; // Found
		}
	}

	return XEMU_SETTINGS_INVALID;
}

static int config_parse_callback(void *user, const char *section, const char *name, const char *value)
{
	// struct xemu_settings *settings = (struct xemu_settings *)user;
	fprintf(stderr, "%s: [%s] %s = %s\n", __func__, section, name, value);

	enum xemu_settings_keys key = xemu_key_from_name(section, name);

	if (key == XEMU_SETTINGS_INVALID) {
		fprintf(stderr, "Ignoring unknown key %s.%s\n", section, name);
		return 1;
	}

	if (config_items[key].type == CONFIG_TYPE_STRING) {
		xemu_settings_set_string(key, value);
	} else if (config_items[key].type == CONFIG_TYPE_INT) {
		int int_val;
		int converted = sscanf(value, "%d", &int_val);
		if (converted != 1) {
			fprintf(stderr, "Error parsing %s.%s as integer. Got '%s'\n", section, name, value);
			return 0;
		}
		xemu_settings_set_int(key, int_val);
	} else if (config_items[key].type == CONFIG_TYPE_FLOAT) {
		float float_val;
		int converted = sscanf(value, "%f", &float_val);
		if (converted != 1) {
			fprintf(stderr, "Error parsing %s.%s as float. Got '%s'\n", section, name, value);
			return 0;
		}
		xemu_settings_set_float(key, float_val);
	} else if (config_items[key].type == CONFIG_TYPE_BOOL) {
		int int_val;
		if (strcmp(value, "true") == 0) {
			int_val = 1;
		} else if (strcmp(value, "false") == 0) {
			int_val = 0;
		} else if (strcmp(value, "") == 0) {
			return 1;
		} else {
			fprintf(stderr, "Error parsing %s.%s as boolean. Got '%s'\n", section, name, value);
			return 0;
		}
		xemu_settings_set_bool(key, int_val);
	} else if (config_items[key].type == CONFIG_TYPE_ENUM) {
		int int_val;
		int status = xemu_enum_str_to_int(config_items[key].enum_map, value, &int_val);
		if (status != 0) {
			fprintf(stderr, "Error parsing %s.%s as enum. Got '%s'\n", section, name, value);
			return 0;
		}
		xemu_settings_set_enum(key, int_val);
	} else {
		// Unimplemented
		assert(0);
	}

	// Success
	return 1;
}

static void xemu_settings_init_default(struct xemu_settings *settings)
{
	memset(settings, 0, sizeof(struct xemu_settings));
	for (int i = 0; i < XEMU_SETTINGS__COUNT; i++) {
		if (config_items[i].type == CONFIG_TYPE_STRING) {
			xemu_settings_set_string(i, config_items[i].default_str);
		} else if (config_items[i].type == CONFIG_TYPE_INT) {
			xemu_settings_set_int(i, config_items[i].default_int);
		} else if (config_items[i].type == CONFIG_TYPE_FLOAT) {
			xemu_settings_set_float(i, config_items[i].default_float);
		} else if (config_items[i].type == CONFIG_TYPE_BOOL) {
			xemu_settings_set_bool(i, config_items[i].default_bool);
		} else if (config_items[i].type == CONFIG_TYPE_ENUM) {
			xemu_settings_set_enum(i, config_items[i].default_int);
		} else {
			// Unimplemented
			assert(0);
		}
	}
}

int xemu_settings_did_fail_to_load(void)
{
	return settings_failed_to_load;
}

void xemu_settings_load(void)
{
	// Should only call this once, at startup
	assert(g_settings == NULL);

	g_settings = malloc(sizeof(struct xemu_settings));
	assert(g_settings != NULL);
	memset(g_settings, 0, sizeof(struct xemu_settings));
	xemu_settings_init_default(g_settings);

	// Parse configuration file
	int status = ini_parse(xemu_settings_get_path(),
		                   config_parse_callback,
		                   g_settings);
	if (status < 0) {
		// fprintf(stderr, "Failed to load config! Using defaults\n");
		settings_failed_to_load = 1;
	}
}

int xemu_settings_save(void)
{
	FILE *fd = qemu_fopen(xemu_settings_get_path(), "wb");
	assert(fd != NULL);

	const char *last_section = "";
	for (int i = 0; i < XEMU_SETTINGS__COUNT; i++) {
		if (strcmp(last_section, config_items[i].section)) {
			fprintf(fd, "\n[%s]\n", config_items[i].section);
			last_section = config_items[i].section;
		}

		fprintf(fd, "%s = ", config_items[i].name);

		if (config_items[i].type == CONFIG_TYPE_STRING) {
			const char *v;
			xemu_settings_get_string(i, &v);
			fprintf(fd, "%s\n", v);
		} else if (config_items[i].type == CONFIG_TYPE_INT) {
			int v;
			xemu_settings_get_int(i, &v);
			fprintf(fd, "%d\n", v);
		} else if (config_items[i].type == CONFIG_TYPE_FLOAT) {
			float v;
			xemu_settings_get_float(i, &v);
			fprintf(fd, "%f\n", v);
		} else if (config_items[i].type == CONFIG_TYPE_BOOL) {
			int v;
			xemu_settings_get_bool(i, &v);
			if (v == 0 || v == 1) {
				fprintf(fd, "%s\n", !!(v) ? "true" : "false");
			} else {
				// Other values are considered unset
			}
		} else if (config_items[i].type == CONFIG_TYPE_ENUM) {
			int v;
			xemu_settings_get_enum(i, &v);
			const char *str = "";
			xemu_enum_int_to_str(config_items[i].enum_map, v, &str);
			fprintf(fd, "%s\n", str);
		} else {
			// Unimplemented
			assert(0);
		}
	}

	fclose(fd);
	return 0;
}
