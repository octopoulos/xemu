// ui-controls.cpp: Controls + Main menu + Xbox functions + window Manager
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"
#include "shuriken.h"
#include "data/controls.png.h"

#include "implot/implot.h"
#include "imgui_extra/imgui_memory_editor.h"

extern "C" {
#include "qemui/noc_file_dialog.h"

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hw/xbox/mcpx/apu_debug.h"
#include "net/pcap.h"
}

#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/nv2a.h"
#include "hw/xbox/smbus.h"
#include "xemu-os-utils.h"
#include "qapi/qapi-commands-block.h"

extern int      askedIntercept;
extern bool     capture_renderdoc_frame;
extern uint64_t memoryData[4];
extern int      want_screenshot;

exiso::GameInfo gameInfo;

// xemu.cpp
int  xemu_is_fullscreen();
void xemu_toggle_fullscreen();

namespace ui
{

// FUNCTIONS
////////////

static bool AddMenu(const char* text, const char* shortcut, CommonWindow& window)
{
	bool clicked = ImGui::MenuItem(text, shortcut, &window.isOpen);
	if (clicked)
	{
		Log("text=%s clicked=%d hidden=%d isOpen=%d", text, clicked, window.hidden, window.isOpen);
		if (window.hidden & 1)
			window.isOpen = true;
		if (window.isOpen)
			window.hidden &= ~1;
	}
	return clicked;
}

void EjectDisc()
{
	strcpy(xsettings.dvd_path, "");
	xsettingsSave();

	xbox_smc_eject_button();
	LoadedGame("");

	// Xbox software may request that the drive open, but do it now anyway
	Error* err = NULL;
	qmp_eject(true, "ide0-cd1", false, NULL, true, false, &err);
	xbox_smc_update_tray_state();
}

std::string FileOpen(const char* filters, std::string current)
{
	const char* filename = PausedFileOpen(NOC_FILE_DIALOG_OPEN, filters, current.c_str(), nullptr);
	return filename ? filename : "";
}

std::string FileOpenISO(std::string current)
{
	static const char* filters = ".iso Files\0*.iso\0All Files\0*.*\0";
	return FileOpen(filters, current);
}

bool IsRunning()
{
	return runstate_is_running();
}

bool LoadDisc(std::string filename, bool saveSetting)
{
	auto path = filename.c_str();
	if (!*path)
		return false;

	str2k temp;
	strcpy(temp, path);
	LoadingGame(temp);

	// add to recent list
	auto files = xsettings.recent_files;
	int  id    = 0;
	while (id < 6 && strcmp(files[id], temp))
		++id;

	if (id > 0)
	{
		for (int i = std::min(5, id); i > 0; --i)
			strcpy(files[i], files[i - 1]);
		strcpy(files[0], temp);
	}

	strcpy(xsettings.dvd_path, temp);
	if (saveSetting)
		xsettingsSave();

	// Ensure an eject sequence is always triggered so Xbox software reloads
	xbox_smc_eject_button();

	Error* err = NULL;
	qmp_blockdev_change_medium(true, "ide0-cd1", false, NULL, temp, false, "", false, (BlockdevChangeReadOnlyMode)0, &err);
	xbox_smc_update_tray_state();
	return true;
}

void LoadingGame(std::string path)
{
	exiso::ExtractGameInfo(path, &gameInfo, true);
	ui::LoadedGame("");
}

const char* PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name)
{
	bool is_running = runstate_is_running();
	if (is_running)
		vm_stop(RUN_STATE_PAUSED);

	const char* r = noc_file_dialog_open(flags, filters, default_path, default_name);
	if (is_running)
		vm_start();

	return r;
}

void Reset()
{
	qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

void ShutDown()
{
	LoadedGame("");
	qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
}

/**
 * Pause/Start/Toggle
 * @param status 0:pause, 1:start, 2:toggle
 */
void TogglePause(int status)
{
	switch (status)
	{
	case 0:
		if (IsRunning()) vm_stop(RUN_STATE_PAUSED);
		break;
	case 1:
		if (!IsRunning()) vm_start();
		break;
	case 2:
		if (IsRunning()) vm_stop(RUN_STATE_PAUSED);
		else vm_start();
		break;
	}
}

// CLASS
////////

static bool SetAlpha(float alpha)
{
	if (alpha <= 0.0f)
		return false;

	ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_Text];
	color.w      = alpha;
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	ImGui::SetNextWindowBgAlpha(alpha);
	return true;
}

class ControlsWindow : public CommonWindow
{
private:
	uint32_t texId;

public:
	ControlsWindow()
	{
		name   = "Controls";
		isOpen = true;
	}

	void Draw()
	{
		CHECK_DRAW();
		if (!drawn)
		{
			texId = LoadTexture(controls_data, controls_size, "controls");
			++drawn;
		}

		if (!SetAlpha(alpha))
			return;

		if (ImGui::Begin("Controls", &isOpen))
		{
			auto& style = ImGui::GetStyle();
			auto color = style.Colors[ImGuiCol_Text];

			ImGui::PushFont(FindFont("RobotoCondensed"));
			if (DrawButton(color, "Open")) LoadDisc();
			if (DrawButton(color, "Reset")) Reset();
			if (DrawButton(color, "FullScr")) xemu_toggle_fullscreen();
			if (DrawButton(color, "Stop")) EjectDisc();
			if (DrawButton(color, IsRunning() ? "Pause" : "Start")) TogglePause();
			if (DrawButton(color, "Config")) OpenConfig(1);
			if (DrawButton(color, "Pads")) OpenConfig(3);
			if (DrawButton(color, "List")) SetGamesGrid(false);
			if (DrawButton(color, "Grid")) SetGamesGrid(true);
			ImGui::PopFont();

			if (GetGamesWindow().isOpen)
			{
				auto  regionMax = ImGui::GetWindowContentRegionMax();
				auto  cursorPos = ImGui::GetCursorPos();
				float size64    = 64.0f * xsettings.ui_scale;

				if (float spaceLeftX = regionMax.x - cursorPos.x - style.ItemSpacing.x / 2; spaceLeftX > size64 * 1.5f)
				{
					ImGui::PushItemWidth(std::min(spaceLeftX, size64 * 3));
					float offset = (size64 - 16.0f) / 2  - style.FramePadding.y;
					ImGui::SetCursorPosY(cursorPos.y + offset);
					AddSliderInt("row_height", "##Scale");
					ImGui::PopItemWidth();
				}
				else
				{
					float sizeY = size64;
					if (regionMax.x < size64 * 2)
					{
						float offset = (size64 - 30.0f) / 2;
						ImGui::SetCursorPosX(cursorPos.x + offset);
						float spaceLeftY = regionMax.y - cursorPos.y - style.ItemSpacing.y / 2;
						sizeY = std::clamp(spaceLeftY, size64, size64 * 2);
					}
					AddSliderInt("row_height", "##Scale", "%d", true, ImVec2(30.0f, sizeY));
				}
			}

			// saved
			ImGui::End();
		}

		ImGui::PopStyleColor();
	}

	/**
	 * Image text button aligned on a row
	 */
	int DrawButton(const ImVec4& color, std::string name)
	{
		static std::map<std::string, std::tuple<ImVec2, ImVec2>> buttonNames = {
			{"Config",    { { 0.0f, 0.0f }, { 0.25f, 0.25f } }},
			{ "FullScr",  { { 0.25f, 0.0f }, { 0.5f, 0.25f } }},
			{ "FullScr2", { { 0.5f, 0.0f }, { 0.75f, 0.25f } }},
			{ "Grid",     { { 0.75f, 0.0f }, { 1.0f, 0.25f } }},
			{ "List",     { { 0.0f, 0.25f }, { 0.25f, 0.5f } }},
			{ "Open",     { { 0.25f, 0.25f }, { 0.5f, 0.5f } }},
			{ "Pads",     { { 0.5f, 0.25f }, { 0.75f, 0.5f } }},
			{ "Pause",    { { 0.75f, 0.25f }, { 1.0f, 0.5f } }},
			{ "Reset",    { { 0.0f, 0.5f }, { 0.25f, 0.75f } }},
			{ "Start",    { { 0.25f, 0.5f }, { 0.5f, 0.75f } }},
			{ "Stop",     { { 0.5f, 0.5f }, { 0.75f, 0.75f } }},
		};

		const auto   scale = std::clamp(xsettings.ui_scale, 1.0f, 2.0f);
		const ImVec2 buttonDims(32.0f * scale, 32.0f * scale);
		const ImVec2 childDims(64.0f * scale, 64.0f * scale);
		const ImVec2 offset(16.0f * scale, 4.0f * scale);

		auto  nameStr    = name.c_str();
		auto& style      = ImGui::GetStyle();
		auto  padding    = style.WindowPadding;
		auto& [uv0, uv1] = buttonNames[name];

		ImGui::BeginChild(nameStr, childDims, true, ImGuiWindowFlags_NoScrollbar);
		auto pos = ImGui::GetCursorPos() - padding;
		ImGui::SetCursorPos(pos + offset);
		ImGui::Image((ImTextureID)(intptr_t)texId, buttonDims, uv0, uv1, color);
		if (xsettings.text_button)
		{
			float offset = (childDims.x - ImGui::CalcTextSize(nameStr).x) / 2;
			ImGui::SetCursorPosX(pos.x + offset);
			ImGui::TextUnformatted(nameStr);
		}
		ImGui::EndChild();

		int flag = ImGui::IsItemClicked() ? 1 : 0;

		float window_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		float last_x2 = ImGui::GetItemRectMax().x;
		float next_x2 = last_x2 + style.ItemSpacing.x / 2 + childDims.x;
		if (next_x2 < window_x2)
			ImGui::SameLine();

		return flag;
	}
};

static ControlsWindow controlsWindow;
CommonWindow&         GetControlsWindow() { return controlsWindow; }

// MAIN MENU
////////////

static MemoryEditor memoryEditor;
static bool         showImGuiDemo    = false;
static bool         showImPlotDemo   = false;
static bool         showMemoryEditor = false;

static float menuHeight = 0.0f;
float        GetMenuHeight() { return menuHeight; }

void ShowMainMenu(float alpha)
{
	if (!SetAlpha(alpha))
	{
		menuHeight = 0.0f;
		return;
	}

	static int dirty_menu = 0;
	int        update     = 0;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Eject Disc", xsettings.shortcut_eject)) EjectDisc();
			if (ImGui::MenuItem("Boot Disc", xsettings.shortcut_open)) LoadDisc();
			if (ImGui::BeginMenu("Boot Recent"))
			{
				bool first = true;
				for (int i = 0; i < 6; ++i)
				{
					const char* name = xsettings.recent_files[i];
					if (*name)
					{
						if (first)
						{
							if (ImGui::MenuItem("List Clear"))
							{
								memset(xsettings.recent_files, 0, sizeof(xsettings.recent_files));
								break;
							}
							ImGui::Separator();
							first = false;
						}
						std::filesystem::path path = name;
						if (ImGui::MenuItem(path.filename().string().c_str(), fmt::format("Ctrl+{}", i + 1).c_str()))
							LoadDisc(name, true);
					}
				}

				if (first)
					ImGui::MenuItem("Empty List", nullptr, false, false);

				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Scan Folder")) ScanGamesFolder();
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) ShutDown();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Emulation"))
		{
			if (ImGui::MenuItem(runstate_is_running() ? "Pause" : "Run", xsettings.shortcut_pause)) TogglePause();
			if (ImGui::MenuItem("Reset", xsettings.shortcut_reset)) Reset();
			ImGui::Separator();
			if (ImGui::MenuItem("Load State", xsettings.shortcut_loadstate)) LoadState();
			if (ImGui::MenuItem("Save State", xsettings.shortcut_savestate)) SaveState();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Configuration"))
		{
			if (ImGui::MenuItem("CPU")) OpenConfig(0);
			if (ImGui::MenuItem("GPU", xsettings.shortcut_gpu)) OpenConfig(1);
			if (ImGui::MenuItem("Audio")) OpenConfig(2);
			ImGui::Separator();
			if (ImGui::MenuItem("Pads", xsettings.shortcut_pads)) OpenConfig(3);
			if (ImGui::MenuItem("System")) OpenConfig(4);
			if (ImGui::MenuItem("Network")) OpenConfig(5);
			if (ImGui::MenuItem("Advanced")) OpenConfig(6);
			if (ImGui::MenuItem("Emulator")) OpenConfig(7);
			if (ImGui::MenuItem("GUI")) OpenConfig(8);
			if (ImGui::MenuItem("Debug")) OpenConfig(9);
			ImGui::Separator();
			if (ImGui::MenuItem("Shortcuts")) OpenConfig(10);
			AddMenu("Theme Editor", nullptr, GetThemeWindow());
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Controls", xsettings.shortcut_controls, &controlsWindow.isOpen);
			AddMenu("Game List", xsettings.shortcut_games, GetGamesWindow());
			AddMenu("Log", xsettings.shortcut_log, GetLogWindow());
			ImGui::Separator();
			ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo);
			ImGui::MenuItem("ImPlot Demo", nullptr, &showImPlotDemo);
			if (ImGui::MenuItem("Fullscreen", xsettings.shortcut_fullscreen, xemu_is_fullscreen(), true)) xemu_toggle_fullscreen();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Utilities"))
		{
			ImGui::MenuItem("Memory Editor", nullptr, &showMemoryEditor);
			AddMenu("Monitor", xsettings.shortcut_monitor, GetMonitorWindow());
			AddMenu("Audio", nullptr, GetAudioWindow());
			AddMenu("Video", nullptr, GetVideoWindow());

			ImGui::Separator();
			if (ImGui::MenuItem("Extract ISO")) exiso::DecodeXiso(FileOpenISO(""));
			if (ImGui::MenuItem("Create ISO")) exiso::CreateXiso(FileOpenISO(""));

			ImGui::Separator();
			if (ImGui::MenuItem("Screenshot", xsettings.shortcut_screenshot)) want_screenshot = (1 + 4) + 2; // force screenshot + maybe icon
			if (ImGui::MenuItem("Save Icon")) want_screenshot = 2 + 8;                                       // force icon
			if (ImGui::MenuItem("Intercept", xsettings.shortcut_intercept)) GetFileWindow().isOpen = true;
#if defined(DEBUG_NV2A_GL) && defined(CONFIG_RENDERDOC)
            if (nv2a_dbg_renderdoc_available())
                ImGui::MenuItem("RenderDoc: Capture", nullptr, &capture_renderdoc_frame);
#endif
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Help", nullptr)) xemu_open_web_browser("https://xemu.app/docs/getting-started/");

			// ImGui::MenuItem("Report Compatibility...", nullptr, &compatibility_reporter_window.isOpen);
			// ImGui::MenuItem("Check for Updates...", nullptr, &update_window.isOpen);

			ImGui::Separator();
			AddMenu("About", nullptr, GetAboutWindow());
			ImGui::EndMenu();
		}

		menuHeight = ImGui::GetWindowHeight();
		ImGui::EndMainMenuBar();

		// save directly if update = 1, or after update-1 frames delay
		if (update) dirty_menu = update;
		if (dirty_menu && !--dirty_menu) xsettingsSave();
	}

	ImGui::PopStyleColor();
}

// API
//////

static std::vector<CommonWindow*>                     windows;
static std::unordered_map<std::string, CommonWindow*> windowNames;

// Draw all UI
void DrawWindows()
{
	for (auto window : windows)
		window->Draw();

	if (showImGuiDemo) ImGui::ShowDemoWindow(&showImGuiDemo);
	if (showImPlotDemo) ImPlot::ShowDemoWindow(&showImPlotDemo);
	if (showMemoryEditor)
	{
		ImGui::PushFont(FindFont("mono"));
		memoryEditor.DrawWindow("Memory Editor", (uint8_t*)memoryData[0], memoryData[2]);
		ImGui::PopFont();
	}
}

/**
 * Home/Guide button was pushed
 */
void HomeGuide(bool hold)
{
	static bool lastChange = true;

	int value = hold ? xsettings.guide_hold : xsettings.guide;
	if (!value)
		return;

	// if holding and we already paused with the guide => don't unpause again ... except if the pause was in game 1 frame (no windows changes)
	if (!hold || !xsettings.guide || (!lastChange && IsRunning()))
		TogglePause();

	// if game is running => hide the windows otherwise show them
	bool running = IsRunning();
	if (value > 1 || (running && xsettings.run_no_ui))
		lastChange = ShowWindows(!running, false);
	else
		lastChange = false;
}

/**
 * Create a list of the windows
 */
void ListWindows()
{
	if (windows.size())
		return;

	windows.push_back(&GetAudioWindow());
	windows.push_back(&GetControlsWindow());
	windows.push_back(&GetFileWindow());
	windows.push_back(&GetGamesWindow());
	windows.push_back(&GetLogWindow());
	windows.push_back(&GetMonitorWindow());
	windows.push_back(&GetSettingsWindow());
	windows.push_back(&GetThemeWindow());
	windows.push_back(&GetVideoWindow());

	for (auto window : windows)
		windowNames[window->name] = window;
}

/**
 * Hide/unhide windows:
 * - only change hidden, not isOpen
 */
bool ShowWindows(bool show, bool force)
{
	bool changed = false;
	for (auto window : windows)
	{
		if (show)
		{
			if (window->hidden & 1)
			{
				changed = true;
				window->hidden &= ~1;
			}
		}
		else if (window->hidden == 0 || (window->hidden == 2 && force))
		{
			changed = true;
			window->hidden |= 1;
		}
	}
	return changed;
}

} // namespace ui
