/*
 * xsettings.cpp
 *
 * Copyright (C) 2022 octopoulos
 * Copyright (C) 2021 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_KEYB_MAPPING "4,5,27,28,80,82,79,81,42,40,30,31,32,33,34,,,,,,,,26,18,22,9,7,8,13,15,14,12"
// value >= 32 = axis
#define DEFAULT_PAD_MAPPING "0,1,2,3,13,11,14,12,4,6,9,10,7,8,5,,,,,,,,36,37,32,33,32,33,34,35,34,35"

enum ASPECT_RATIO
{
	ASPECT_RATIO_169,
	ASPECT_RATIO_43,
	ASPECT_RATIO_NATIVE,
	ASPECT_RATIO_WINDOW,
};

enum NET_BACKEND
{
	NET_BACKEND_USER,
	NET_BACKEND_SOCKET_UDP,
	NET_BACKEND_PCAP,
};

enum RENDERER
{
	RENDERER_DX9,
	RENDERER_DX11,
	RENDERER_OPENGL,
	RENDERER_VULKAN,
	RENDERER_NONE,
};

typedef char str32[32];
typedef char str2k[2048];

typedef struct _XSettings
{
	// [cpu]

	// [gpu]
	int anisotropic;
	int aspect_ratio;
	int display_nearest;
	int dither;
	int fbo_nearest;
	int graph_nearest;
	int integer_scaling;
	int line_smooth;
	int overlay_nearest;
	int polygon_smooth;
	int renderer;
	int resolution_scale;
	int scale_nearest;
	int shader_hint;
	int shader_nearest;
	int stretch;
	int surface_part_nearest;
	int surface_texture_nearest;

	// [input]
	str2k input_guid[4];
	str2k input_keyb[4];
	str2k input_pad[4];

	// [audio]
	int use_dsp;

	// [system]
	str2k bootrom_path;
	str2k dvd_path;
	str2k eeprom_path;
	str2k flash_path;
	int   hard_fpu;
	str2k hdd_path;
	int   memory;

	// [network]
	int   net_backend;
	int   net_enabled;
	str2k net_local_addr;
	str2k net_pcap_iface;
	str2k net_remote_addr;

	// [advanced]

	// [emulator]
	int   performance_overlay;
	int   resize_height;
	int   resize_on_boot;
	int   resize_width;
	int   short_animation;
	int   start_fullscreen;
	int   startup_game;
	str2k window_title;

	// [gui]
    int   row_height;
    str32 shortcut_controls;
    str32 shortcut_eject;
    str32 shortcut_fullscreen;
    str32 shortcut_games;
    str32 shortcut_gpu;
    str32 shortcut_intercept;
    str32 shortcut_log;
    str32 shortcut_monitor;
    str32 shortcut_open;
    str32 shortcut_pads;
    str32 shortcut_pause;
    str32 shortcut_reset;
    str32 shortcut_screenshot;
	float ui_scale;

	// [debug]
	str2k intercept_filter;

	// [misc]
	int   check_for_update;
	str2k recent_files[6];
	str2k user_token;
} XSettings;

int         xsettingsCompare(XSettings* previous);
void        xsettingsDefaults(const char* section);
int         xsettingsFailed(void);
void*       xsettingsFind(const char* name);
const char* xsettingsFolder(const char* newFolder);
void        xsettingsInit(void);
void        xsettingsLoad(void);
int         xsettingsSave(void);

extern XSettings xsettings;

#ifdef __cplusplus
}
#endif
