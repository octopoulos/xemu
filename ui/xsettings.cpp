/*
 * xsettings.cpp
 *
 * Copyright (C) 2021 Octo Poulos
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

#include <algorithm>
#include <assert.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <SDL_filesystem.h>
#include <string>
#include <unordered_set>

#include "util/tomlplusplus/toml.hpp"
#include "xsettings.h"

using namespace std::string_literals;

struct EnumMap
{
	int         value;
	const char* text;
};

const EnumMap aspectRatioMaps[] = {
	{ ASPECT_RATIO_169, "16:9" },
	{ ASPECT_RATIO_43, "4:3" },
	{ ASPECT_RATIO_NATIVE, "native" },
	{ ASPECT_RATIO_WINDOW, "window" },
	{ -1, nullptr },
};

const EnumMap backendMaps[] = {
	{ NET_BACKEND_USER, "user" },
	{ NET_BACKEND_SOCKET_UDP, "udp" },
	{ NET_BACKEND_PCAP, "pcap" },
	{ -1, nullptr },
};

const EnumMap rendererMaps[] = {
	{ RENDERER_OPENGL, "opengl" },
	{ RENDERER_VULKAN, "vulkan" },
	{ RENDERER_NONE, "none" },
	{ -1, nullptr },
};

#define CHECK_TYPE(want) \
	if (type != want) \
	{ \
		std::cerr << "Wrong type for " << name << ", " << want << " instead of " << type << '\n'; \
		return; \
	}

#define X_ARRAY(section, restart, name, def, count) \
	{ 'a', #section, restart, #name, offsetof(XSettings, name), {.defStr = def}, {.minInt = count} }
#define X_BOOL(section, restart, name, def) \
	{ 'b', #section, restart, #name, offsetof(XSettings, name), {.defBool = def} }
#define X_ENUM(section, restart, name, def, enumMap) \
	{ 'e', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {}, {}, enumMap }
#define X_FLOAT(section, restart, name, def, vmin, vmax) \
	{ 'f', #section, restart, #name, offsetof(XSettings, name), {.defFloat = def}, {.minFloat = vmin}, {.maxFloat = vmax} }
#define X_INT(section, restart, name, def, vmin, vmax) \
	{ 'i', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {.minInt = vmin}, {.maxInt = vmax} }
#define X_INT2(section, restart, name, def, some) \
	{ 'i', #section, restart, #name, offsetof(XSettings, name), {.defInt = def}, {}, {}, nullptr, some }
#define X_STRING(section, restart, name, def) \
	{ 's', #section, restart, #name, offsetof(XSettings, name), {.defStr = def} }

struct Config
{
	char        type;
	const char* section;
	int         restart;
	const char* name;
	ptrdiff_t   offset;
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
	const EnumMap* enumMap;
	const char*    someInts;
	void*          ptr;
	int            size;

	const char* GetArray(int index)
	{
		assert(ptr && index >= 0 && index < minInt);
		return ((str2k*)ptr)[index];
	}

	void SetArray(int index, const char* val)
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

	bool GetBool()
	{
		assert(ptr);
		return !!*(int*)ptr;
	}

	void SetBool(bool val)
	{
		assert(ptr);
		CHECK_TYPE('b');
		*(int*)ptr = val;
	}

	const char* GetEnum()
	{
		assert(ptr && enumMap);
		int val   = *(int*)ptr;
		int count = 0;
		for (int i = 0; enumMap[i].text; ++i, ++count) {}
		val = std::clamp(val, 0, count - 1);
		return enumMap[val].text;
	}

	void SetEnum(int val)
	{
		assert(ptr && enumMap);
		CHECK_TYPE('e');
		int count = 0;
		for (int i = 0; enumMap[i].text; ++i, ++count) {}
		val        = std::clamp(val, 0, count - 1);
		*(int*)ptr = val;
	}

	void SetEnum(const char* val)
	{
		assert(ptr && enumMap);
		CHECK_TYPE('e');

		for (int i = 0; enumMap[i].text; ++i)
		{
			if (!strcmp(val, enumMap[i].text))
				*(int*)ptr = i;
		}
	}

	float GetFloat()
	{
		assert(ptr);
		return *(float*)ptr;
	}

	void SetFloat(float val)
	{
		assert(ptr);
		CHECK_TYPE('f');
		if (minFloat < maxFloat)
			val = std::clamp(val, minFloat, maxFloat);
		*(float*)ptr = val;
	}

	int GetInt()
	{
		assert(ptr);
		return *(int*)ptr;
	}

	void SetInt(int val)
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

	const char* GetString()
	{
		assert(ptr);
		return (const char*)ptr;
	}

	void SetString(const char* val)
	{
		assert(ptr);
		CHECK_TYPE('s');
		strcpy((char*)ptr, val);
	}

	void ResetDefault()
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
};

static std::vector<Config> configs = {
	// [cpu]

	// [gpu]
	X_INT2(gpu, 0, anisotropic, 0, "|0|1|2|4|8|16|"),
	X_ENUM(gpu, 0, aspect_ratio, ASPECT_RATIO_43, aspectRatioMaps),
	X_INT(gpu, 0, dither, 2, 0, 2),
	X_BOOL(gpu, 0, fbo_nearest, 0),
	X_BOOL(gpu, 0, graph_nearest, 0),
	X_BOOL(gpu, 0, integer_scaling, 0),
	X_INT(gpu, 0, line_smooth, 2, 0, 2),
	X_BOOL(gpu, 0, overlay_nearest, 0),
	X_INT(gpu, 0, polygon_smooth, 2, 0, 2),
	X_ENUM(gpu, 1, renderer, RENDERER_OPENGL, rendererMaps),
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
	X_BOOL(audio, 0, use_dsp, 0),

	// [system]
	X_STRING(system, 1, bootrom_path, ""),
	X_STRING(system, 1, dvd_path, ""),
	X_STRING(system, 1, eeprom_path, ""),
	X_STRING(system, 1, flash_path, ""),
	X_BOOL(system, 1, hard_fpu, 1),
	X_STRING(system, 1, hdd_path, ""),
	X_INT(system, 1, memory, 64, 64, 128),

	// [network]
	X_ENUM(network, 0, net_backend, NET_BACKEND_USER, backendMaps),
	X_BOOL(network, 0, net_enabled, 0),
	X_STRING(network, 0, net_local_addr, "0.0.0.0:9368"),
	X_STRING(network, 0, net_pcap_iface, ""),
	X_STRING(network, 0, net_remote_addr, "1.2.3.4:9368"),

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
	X_FLOAT(gui, 0, ui_scale, 1.0f, 1.0f, 4.0f),

	// [debug]
	X_STRING(debug, 0, intercept_filter, ""),

	// [misc]
	X_BOOL(misc, 1, check_for_update, 1),
	X_ARRAY(misc, 0, recent_files, "", 6),
	X_STRING(misc, 0, user_token, ""),
};

static std::map<std::string, Config*> configMap;

static std::string settingsDir;
static int         failedLoad;

// global variable
XSettings xsettings;

// API
//////

Config* configFind(std::string name)
{
	auto it = configMap.find(name);
	return (it != configMap.end()) ? it->second : nullptr;
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
	auto* config = configFind(name);
	return config ? config->ptr : nullptr;
}

/**
 * Get or set the settings folder
 * @param newFolder 0 to get
 */
const char* xsettingsFolder(const char* newFolder)
{
	if (newFolder)
		settingsDir = newFolder;
	return settingsDir.c_str();
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
	// 	std::cerr << "config " << config.offset << ' ' << config.size << ' ' << config.type << ' ' << config.name << '\n';

	// portable mode?
	bool isPortable = false;
	{
		char*                 baseDir = SDL_GetBasePath();
		std::filesystem::path path(baseDir);
		path += "xemu.toml";
		if (std::filesystem::exists(path))
		{
			settingsDir = path.string();
			isPortable  = true;
		}
		SDL_free(baseDir);
	}

	// user dir
	if (!isPortable)
	{
		char* baseDir = SDL_GetPrefPath("xemu", "xemu");
		settingsDir   = baseDir;
		SDL_free(baseDir);
	}
}

/**
 * Load xemu.toml
 */
void xsettingsLoad()
{
	memset(&xsettings, 0, sizeof(XSettings));
	xsettingsDefaults(0);

	toml::table doc;
	try
	{
		doc = toml::parse_file(settingsDir + "xemu.toml");
	}
	catch (const toml::parse_error& err)
	{
		std::cerr << err << '\n';
		failedLoad = 1;
		return;
	}

	// iterate & visit over the data
	for (const auto& [section, data] : doc)
	{
		data.visit([](auto& node) noexcept
		{
			if constexpr (toml::is_table<decltype(node)>)
			{
				for (const auto& [key, value] : node)
				{
					value.visit([&key](auto& item) noexcept
					{
						auto config = configFind(key);
						if (!config)
							return;

						if constexpr (toml::is_array<decltype(item)>)
						{
							if (auto array = item.as_array())
							{
								int i = 0;
								for (auto& element : *array)
								{
									element.visit([&config, i](auto& part) noexcept
									{
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
 * Save xemu.toml
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

	std::ofstream out(settingsDir + "xemu.toml");
	out << doc << '\n';
	out.close();
	return 1;
}
