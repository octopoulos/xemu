// xsettings.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>

#include <SDL_filesystem.h>
#include "util/tomlplusplus/toml.hpp"

#include "ui.h"

using namespace std::string_literals;

const std::string shurikenToml = "shuriken.toml";

const char* sAspectRatios[] = { "16:9", "4:3", "Native", "Window" };
const char* sFonts[]        = { "Proggy Clean", "Roboto Medium" };
const char* sFrameLimits[]  = { "off", "auto", "30", "50", "59.94", "60" };
const char* sNetBackends[]  = { "user", "udp", "pcap" };
const char* sRenderers[]    = { "DX9", "DX11", "OpenGL", "Vulkan", "Null" };
const char* sThemes[]       = { "Classic", "Dark", "Light", "Shuriken", "Xemu" };

#define CHECK_TYPE(want)                                                       \
	if (type != want)                                                          \
	{                                                                          \
		ui::LogError("Wrong type for %s, %c instead of %c", name, want, type); \
		return;                                                                \
	}

#define X_ARRAY(section, restart, name, def, count) \
	{ 'a', #section, restart, #name, offsetof(XSettings, name), {.defStr = def}, {.minInt = count} }
#define X_BOOL(section, restart, name, def) \
	{ 'b', #section, restart, #name, offsetof(XSettings, name), {.defBool = def} }
#define X_ENUM(section, restart, name, def, names) \
	{ 'e', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {}, {}, names, nullptr, sizeof(names)/sizeof(names[0]) }
#define X_FLOAT(section, restart, name, def, vmin, vmax) \
	{ 'f', #section, restart, #name, offsetof(XSettings, name), {.defFloat = def}, {.minFloat = vmin}, {.maxFloat = vmax} }
#define X_INT(section, restart, name, def, vmin, vmax) \
	{ 'i', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {.minInt = vmin}, {.maxInt = vmax} }
#define X_INT2(section, restart, name, def, some) \
	{ 'i', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {}, {}, nullptr, some }
#define X_STRING(section, restart, name, def) \
	{ 's', #section, restart, #name, offsetof(XSettings, name), {.defStr = def} }

// CONFIG
/////////

const char* Config::GetArray(int index)
{
    assert(ptr && index >= 0 && index < minInt);
    return ((str2k*)ptr)[index];
}

void Config::SetArray(int index, const char* val)
{
    assert(ptr && index < minInt);
    CHECK_TYPE('a');
    if (index >= 0)
        strcpy(((str2k*)ptr)[index], val);
    else
    {
        for (int i = 0; i < minInt; ++i)
            strcpy(((str2k*)ptr)[i], val);
    }
}

bool Config::GetBool()
{
    assert(ptr);
    return !!*(int*)ptr;
}

void Config::SetBool(bool val)
{
    assert(ptr);
    CHECK_TYPE('b');
    *(int*)ptr = val;
}

const char* Config::GetEnum()
{
    assert(ptr && names);
    int val = *(int*)ptr;
    val     = std::clamp(val, 0, count - 1);
    return names[val];
}

void Config::SetEnum(int val)
{
    assert(ptr && names);
    CHECK_TYPE('e');
    val        = std::clamp(val, 0, count - 1);
    *(int*)ptr = val;
}

void Config::SetEnum(const char* val)
{
    assert(ptr && names);
    CHECK_TYPE('e');

    for (int i = 0; i < count; ++i)
    {
        if (!strcmp(val, names[i]))
            *(int*)ptr = i;
    }
}

float Config::GetFloat()
{
    assert(ptr);
    return *(float*)ptr;
}

void Config::SetFloat(float val)
{
    assert(ptr);
    CHECK_TYPE('f');
    if (minFloat < maxFloat)
        val = std::clamp(val, minFloat, maxFloat);
    *(float*)ptr = val;
}

int Config::GetInt()
{
    assert(ptr);
    return *(int*)ptr;
}

void Config::SetInt(int val)
{
    assert(ptr);
    CHECK_TYPE('i');
    if (someInts)
    {
        char text[16];
        sprintf(text, "|%d|", val);
        if (strstr(someInts, text))
            *(int*)ptr = val;
    }
    else
    {
        if (minInt < maxInt)
            val = std::clamp(val, minInt, maxInt);
        *(int*)ptr = val;
    }
}

const char* Config::GetString()
{
    assert(ptr);
    return (const char*)ptr;
}

void Config::SetString(const char* val)
{
    assert(ptr);
    CHECK_TYPE('s');
    strcpy((char*)ptr, val);
}

void Config::ResetDefault()
{
    switch (type)
    {
    case 'a': SetArray(-1, defStr); break;
    case 'b': SetBool(defInt); break;
    case 'e': SetEnum(defInt); break;
    case 'f': SetFloat(defFloat); break;
    case 'i': SetInt(defInt); break;
    case 's': SetString(defStr); break;
    }
}

// MAPPING
//////////

static std::vector<Config> configs = {
	// [cpu]

	// [gpu]
	X_INT2(gpu, 0, anisotropic, 0, "|0|1|2|4|8|16|"),
	X_ENUM(gpu, 0, aspect_ratio, ASPECT_RATIO_43, sAspectRatios),
	X_INT(gpu, 0, dither, 2, 0, 2),
	X_BOOL(gpu, 0, fbo_nearest, 0),
    X_ENUM(gpu, 0, frame_limit, FRAME_LIMIT_AUTO, sFrameLimits),
	X_BOOL(gpu, 0, graph_nearest, 0),
	X_BOOL(gpu, 0, integer_scaling, 0),
	X_INT(gpu, 0, line_smooth, 2, 0, 2),
	X_BOOL(gpu, 0, overlay_nearest, 0),
	X_INT(gpu, 0, polygon_smooth, 2, 0, 2),
	X_ENUM(gpu, 1, renderer, RENDERER_OPENGL, sRenderers),
	X_INT(gpu, 0, resolution_scale, 1, 1, 10),
	X_BOOL(gpu, 0, scale_nearest, 0),
	X_BOOL(gpu, 0, shader_hint, 0),
	X_BOOL(gpu, 0, shader_nearest, 0),
	X_BOOL(gpu, 0, stretch, 0),
	X_BOOL(gpu, 0, surface_part_nearest, 0),
	X_BOOL(gpu, 0, surface_texture_nearest, 0),

	// [input]
	X_ARRAY(input, 0, input_guid, "", 4),
	X_ARRAY(input, 0, input_keyb, DEFAULT_KEYB_MAPPING, 4),
	X_ARRAY(input, 0, input_pad, DEFAULT_PAD_MAPPING, 4),

	// [audio]
	X_BOOL(audio, 0, use_dsp, 1),

	// [system]
	X_STRING(system, 1, bootrom_path, ""),
	X_STRING(system, 1, dvd_path, ""),
	X_STRING(system, 1, eeprom_path, ""),
	X_STRING(system, 1, flash_path, ""),
	X_BOOL(system, 1, hard_fpu, 1),
	X_STRING(system, 1, hdd_path, ""),
	X_INT(system, 1, memory, 64, 64, 128),

	// [network]
	X_ENUM(network, 0, net_backend, NET_BACKEND_USER, sNetBackends),
	X_BOOL(network, 0, net_enabled, 0),
	X_STRING(network, 0, net_local_addr, "0.0.0.0:9368"),
	X_STRING(network, 0, net_pcap_iface, ""),
	X_STRING(network, 0, net_remote_addr, "1.2.3.4:9368"),

    // [advanced]
    X_INT(advanced, 0, vblank_frequency, 60, 0, 360),

	// [emulator]
	X_BOOL(emulator, 0, performance_overlay, 0),
	X_INT(emulator, 1, resize_height, 800, 480, 2160),
	X_BOOL(emulator, 1, resize_on_boot, 1),
	X_INT(emulator, 1, resize_width, 1280, 640, 5120),
	X_BOOL(emulator, 1, short_animation, 0),
	X_BOOL(emulator, 1, start_fullscreen, 0),
	X_BOOL(emulator, 1, startup_game, 0),
	X_STRING(emulator, 0, window_title, ""),

	// [gui]
	X_ENUM(gui, 0, font, FONT_ROBOTO_MEDIUM, sFonts),
	X_INT(gui, 0, guide, 1, 0, 2),
	X_INT(gui, 0, guide_hold, 2, 0, 2),
	X_INT(gui, 0, guide_hold_frames, 15, 1, 60),
	X_INT(gui, 0, row_height, 80, 24, 176),
	X_BOOL(gui, 0, run_no_ui, 1),
	X_STRING(gui, 0, shortcut_controls, "Ctrl+C"),
	X_STRING(gui, 0, shortcut_eject, "Ctrl+E"),
	X_STRING(gui, 0, shortcut_fullscreen, "Alt+Enter"),
	X_STRING(gui, 0, shortcut_games, "Esc"),
	X_STRING(gui, 0, shortcut_gpu, "F1"),
	X_STRING(gui, 0, shortcut_intercept, "Alt+I"),
	X_STRING(gui, 0, shortcut_log, "Ctrl+L"),
	X_STRING(gui, 0, shortcut_monitor, "`"),
	X_STRING(gui, 0, shortcut_open, "Ctrl+O"),
	X_STRING(gui, 0, shortcut_pads, "F2"),
	X_STRING(gui, 0, shortcut_pause, "Ctrl+P"),
	X_STRING(gui, 0, shortcut_reset, "Ctrl+R"),
	X_STRING(gui, 0, shortcut_screenshot, "Ctrl+S"),
	X_BOOL(gui, 0, text_button, 1),
	X_ENUM(gui, 0, theme, THEME_XEMU, sThemes),
	X_FLOAT(gui, 0, ui_scale, 1.0f, 1.0f, 4.0f),

	// [debug]
	X_STRING(debug, 0, intercept_filter, ""),

	// [misc]
	X_BOOL(misc, 1, check_for_update, 1),
	X_ARRAY(misc, 0, recent_files, "", 6),
	X_STRING(misc, 0, user_token, ""),
};

static std::map<std::string, Config*> configMap;
static std::filesystem::path          settingsDir;
static int                            failedLoad;

// global variable
XSettings xsettings;

// API
//////

Config* ConfigFind(std::string name)
{
	auto it = configMap.find(name);
    if (it == configMap.end())
    {
        ui::LogError("ConfigFind: unknown %s", name.c_str());
        return nullptr;
    }
    return it->second;
}

/**
 * Check if the settings have changed
 * @returns 0 if the same, &1 if settings have changed, &2 if a restart is needed
 */
int xsettingsCompare(XSettings* previous)
{
	if (!memcmp(&xsettings, previous, sizeof(XSettings)))
		return 0;

	int changed = 0;

	for (auto& config : configs)
	{
		void* other = (void*)((char*)previous + config.offset);
		if (memcmp(config.ptr, other, config.size))
		{
			changed |= 1;
			if (config.restart)
				changed |= 2;
		}
	}

	return changed;
}

/**
 * Reset default values for a section
 * @param section 0 for all sections
 */
void xsettingsDefaults(const char* section)
{
	memset(&xsettings, 0, sizeof(XSettings));
	for (auto& config : configs)
	{
		if (!section || (config.section && !strcmp(config.section, section)))
			config.ResetDefault();
	}
}

int xsettingsFailed()
{
	return failedLoad;
}

void* xsettingsFind(const char* name)
{
	auto* config = ConfigFind(name);
	return config ? config->ptr : nullptr;
}

/**
 * Get the settings folder (C++)
 */
std::filesystem::path xsettingsFolder()
{
    return settingsDir;
}

/**
 * Get + maybe set the settings folder (C)
 * @param newFolder 0 to get
 */
const char* xsettingsFolderC(const char* newFolder)
{
	if (newFolder)
		settingsDir = newFolder;
	return settingsDir.string().c_str();
}

/**
 * Populate the settingsMap + find the settings folder
 */
void xsettingsInit()
{
	Config* prev = &configs[0];
	for (auto& config : configs)
	{
		config.ptr             = (void*)((char*)&xsettings + config.offset);
		configMap[config.name] = &config;
		prev->size             = config.offset - prev->offset;
		prev                   = &config;
	}
	configs.back().size = sizeof(XSettings) - prev->offset;

	// for (auto& config : configs)
	// 	ui::Log("config %d %d %c %d %s", config.offset, config.size, config.type, config.count, config.name);

	// portable mode?
	bool isPortable = false;
	{
		char*                 baseDir = SDL_GetBasePath();
		std::filesystem::path path(baseDir);
		if (std::filesystem::exists(path / shurikenToml))
		{
			settingsDir = path.string();
			isPortable  = true;
		}
		SDL_free(baseDir);
	}

	// user dir
	if (!isPortable)
	{
		char* baseDir = SDL_GetPrefPath("shuriken", "shuriken");
		settingsDir   = baseDir;
		SDL_free(baseDir);
	}
}

/**
 * Load shuriken.toml
 */
void xsettingsLoad()
{
	memset(&xsettings, 0, sizeof(XSettings));
	xsettingsDefaults(0);

	auto        path = settingsDir / shurikenToml;
	toml::table doc;
	try
	{
		doc = toml::parse_file(path.string());
	}
	catch (const toml::parse_error& err)
	{
        ui::LogError(fmt::format("xsettingsLoad error: {} {}", path.string(), err.description()));
		failedLoad = 1;
		return;
	}

	// iterate & visit over the data
	for (const auto& [section, data] : doc)
	{
		data.visit([](auto& node) noexcept {
			if constexpr (toml::is_table<decltype(node)>)
			{
				for (const auto& [key, value] : node)
				{
					value.visit([&key](auto& item) noexcept {
						auto config = ConfigFind(key);
						if (!config)
							return;

						if constexpr (toml::is_array<decltype(item)>)
						{
							if (auto array = item.as_array())
							{
								int i = 0;
								for (auto& element : *array)
								{
									element.visit([&config, i](auto& part) noexcept {
										if constexpr (toml::is_string<decltype(part)>)
											config->SetArray(i, part.value_or(""s).c_str());
									});
									++i;
								}
							}
						}
						else if constexpr (toml::is_boolean<decltype(item)>)
							config->SetBool(item.value_or(false));
						else if constexpr (toml::is_floating_point<decltype(item)>)
							config->SetFloat(item.value_or(0.0f));
						else if constexpr (toml::is_integer<decltype(item)>)
							config->SetInt(item.value_or(0));
						else if constexpr (toml::is_string<decltype(item)>)
						{
							if (config->type == 'e')
								config->SetEnum(item.value_or(""s).c_str());
							else
								config->SetString(item.value_or(""s).c_str());
						}
					});
				}
			}
		});
	}

	failedLoad = 0;
}

/**
 * Save shuriken.toml
 */
int xsettingsSave()
{
	toml::table doc;
	toml::table section;
	std::string prev_section;

	for (auto& config : configs)
	{
		if (prev_section != config.section)
		{
			if (section.size())
				doc.insert_or_assign(prev_section, section);
			prev_section = config.section;
			section      = toml::table {};
		}

		switch (config.type)
		{
		case 'a':
		{
			toml::array array;
			for (int i = 0; i < config.minInt; ++i)
				array.push_back(config.GetArray(i));

			section.insert_or_assign(config.name, array);
			break;
		}
		case 'b': section.insert_or_assign(config.name, config.GetBool()); break;
		case 'e': section.insert_or_assign(config.name, config.GetEnum()); break;
		case 'f': section.insert_or_assign(config.name, config.GetFloat()); break;
		case 'i': section.insert_or_assign(config.name, config.GetInt()); break;
		case 's': section.insert_or_assign(config.name, config.GetString()); break;
		}
	}

	if (section.size())
		doc.insert_or_assign(prev_section, section);

    auto path = settingsDir / shurikenToml;
	std::ofstream out(path.string());
	out << doc << '\n';
	out.close();
	return 1;
}

// C HELPERS
////////////

void LogC(int color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
	ui::AddLogV(color, fmt, args);
    va_end(args);
}

void LogCV(int color, const char* fmt, va_list args)
{
	ui::AddLogV(color, fmt, args);
}

void ShutDownC()
{
	ui::LoadedGame("");
}
