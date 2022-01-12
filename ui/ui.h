// ui.h
// @2022 octopoulos
//
// This file is part of Shuriken.
// Shuriken is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <unordered_map>

#include <fmt/core.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "extract-xiso.h"
#include "xsettings.h"

namespace ui
{

// COMMON
/////////

#define CHECK_DRAW() \
	if (!isOpen || (hidden & 1)) return

class CommonWindow
{
public:
	float       alpha  = 1.0f;
	int         drawn  = 0;
	int         focus  = 0;
	int         hidden = 0;     // automatic flag, 2=cannot be hidden
	bool        isOpen = false; // manual flag, the user opened/closed the window
	std::string name;

	virtual ~CommonWindow() {}
	virtual void Draw() {}
};

CommonWindow& GetCommonWindow();
bool          AddCombo(std::string name, const char* text);
bool          AddCombo(std::string name, const char* text, const char* texts[], const std::vector<int> values);
bool          AddSliderFloat(std::string name, const char* text, const char* format = "%.2f");
bool          AddSliderInt(std::string name, const char* text, const char* format = "%d", bool vertical = false, const ImVec2& size = { 30, 120 });
void          AddSpace(int height = -1);
uint32_t      LoadTexture(std::filesystem::path path, std::string name);
uint32_t      LoadTexture(const uint8_t* data, uint32_t size, std::string name);

// CONTROLS
///////////

CommonWindow& GetControlsWindow();
void          DrawWindows();
void          EjectDisc();
std::string   FileOpen(const char* filters, std::string current);
std::string   FileOpenISO(std::string current);
float         GetMenuHeight();
void          HomeGuide(bool value);
bool          IsRunning();
void          ListWindows();
bool          LoadDisc(std::string filename = "", bool saveSetting = true);
void          LoadingGame(std::string path);
const char*   PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name);
void          Reset();
void          ShowMainMenu(float alpha);
bool          ShowWindows(bool show, bool force);
void          ShutDown();
void          TogglePause(int status = 2);

// FILE
///////

CommonWindow& GetFileWindow();

// GAMES
////////

CommonWindow& GetGamesWindow();
void          CheckIcon(std::string uid);
void          LoadedGame(std::string uid);
void          OpenGamesList();
void          SaveGamesList();
void          ScanGamesFolder();
void          SetGamesGrid(bool grid);

// LOG
//////

CommonWindow& GetLogWindow();
void          AddLogV(int color, const char* fmt, va_list args);
// clang-format off
void          Log       (const char* fmt, ...);
void          LogError  (const char* fmt, ...);
void          LogInfo   (const char* fmt, ...);
void          LogWarning(const char* fmt, ...);
void          Log       (std::string text);
void          LogError  (std::string text);
void          LogInfo   (std::string text);
void          LogWarning(std::string text);
// clang-format on

// SETTINGS
///////////

CommonWindow& GetSettingsWindow();
void          OpenConfig(int tab);
void          ProcessSDL(void* event_);
void          ProcessShortcuts();
void          UpdateIO();

// THEME
////////

CommonWindow& GetThemeWindow();
ImFont*       FindFont(std::string name);
void          UpdateFonts();
void          UpdateTheme();

// XEMU-HUD
///////////

CommonWindow& GetAboutWindow();
CommonWindow& GetAudioWindow();
CommonWindow& GetMonitorWindow();
CommonWindow& GetVideoWindow();

} // namespace ui
