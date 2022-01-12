// xsettings.h
// @2022 octopoulos
//
// This file is part of Shuriken.
// Shuriken is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_KEYB_MAPPING "77,78,76,74,92,96,94,93,42,40,47,48,6,98,41,,,,,,,,225,229,4,26,7,22,80,82,79,81,"
// value >= 32 = axis
#define DEFAULT_PAD_MAPPING  "0,1,2,3,13,11,14,12,4,6,9,10,7,8,5,,,,,,,,36,37,32,33,32,33,34,35,34,35"

enum ASPECT_RATIO
{
	ASPECT_RATIO_169,
	ASPECT_RATIO_43,
	ASPECT_RATIO_NATIVE,
	ASPECT_RATIO_WINDOW,
};

enum FRAME_LIMIT
{
	FRAME_LIMIT_OFF,
	FRAME_LIMIT_AUTO,
	FRAME_LIMIT_30,
	FRAME_LIMIT_50,
	FRAME_LIMIT_5994,
	FRAME_LIMIT_60,
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

enum THEMES
{
	THEME_CLASSIC,
	THEME_CUSTOM,
	THEME_DARK,
	THEME_LIGHT,
	THEME_XEMU,
};

typedef char str32[32];
typedef char str256[256];
typedef char str2k[2048];

typedef struct XSettings
{
	// [cpu]

	// [gpu]
	int anisotropic;
	int aspect_ratio;
	int display_nearest;
	int dither;
	int frame_limit;
	int graph_nearest;
	int integer_scaling;
	int line_smooth;
	int overlay_nearest;
	int polygon_smooth;
	int renderer;
	int resolution_scale;
	int scale_nearest;
	int shader_hint;
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
	int    net_backend;
	int    net_enabled;
	str256 net_local_addr;
	str256 net_pcap_iface;
	str256 net_remote_addr;

	// [advanced]
	int vblank_frequency;

	// [emulator]
	int    performance_overlay;
	int    resize_height;
	int    resize_on_boot;
	int    resize_width;
	int    short_animation;
	int    start_fullscreen;
	int    startup_game;
	str256 window_title;

	// [gui]
	int   font;
	int   grid;
	int   guide;
	int   guide_hold;
	int   guide_hold_time;
	int   row_height;
	int   run_no_ui;
	str32 shortcut_controls;
	str32 shortcut_eject;
	str32 shortcut_fullscreen;
	str32 shortcut_games;
	str32 shortcut_gpu;
	str32 shortcut_intercept;
	str32 shortcut_loadstate;
	str32 shortcut_log;
	str32 shortcut_monitor;
	str32 shortcut_open;
	str32 shortcut_pads;
	str32 shortcut_pause;
	str32 shortcut_reset;
	str32 shortcut_savestate;
	str32 shortcut_screenshot;
	int   text_button;
	int   theme;
	float ui_scale;

	// [debug]
	str2k intercept_filter;

	// [misc]
	int    check_for_update;
	str2k  recent_files[6];
	str256 user_token;
} XSettings;

int         xsettingsCompare(XSettings* previous);
void        xsettingsDefaults(const char* section);
int         xsettingsFailed(void);
void*       xsettingsFind(const char* name);
const char* xsettingsFolderC(const char* newFolder);
void        xsettingsInit(void);
void        xsettingsLoad(void);
int         xsettingsSave(void);

extern XSettings xsettings;

// C compatibility
#include <stdarg.h>

enum
{
	LOG_LOG = 0,
	LOG_ERROR = 1,
	LOG_INFO = 2,
	LOG_WARNING = 3,
};

void LogC(int color, const char* fmt, ...);
void LogCV(int color, const char* fmt, va_list args);
void ShutDownC(void);

#ifdef __cplusplus
}

#	include <filesystem>
#	include <string>

struct Config
{
	char        type;
	const char* section;
	int         restart;
	const char* name;
	ptrdiff_t   offset; // XSetting offset
	union
	{
		const char* defStr;
		int         defInt;
		float       defFloat;
		int         defBool;
	};
	union
	{
		int   minInt;
		float minFloat;
	};
	union
	{
		int   maxInt;
		float maxFloat;
	};
	const char** names;
	const char*  someInts; // |0|1|2|4|
	int          count;    // number of items in names
	void*        ptr;      // pointer to XSetting
	int          size;     // XSetting size

	const char* GetArray(int index);
	void        SetArray(int index, const char* val);
	bool        GetBool();
	void        SetBool(bool val);
	const char* GetEnum();
	void        SetEnum(int val);
	void        SetEnum(const char* val);
	float       GetFloat();
	void        SetFloat(float val);
	int         GetInt();
	void        SetInt(int val);
	const char* GetString();
	void        SetString(const char* val);
	void        ResetDefault();
};

Config*               ConfigFind(std::string name);
std::filesystem::path xsettingsFolder();

#endif
