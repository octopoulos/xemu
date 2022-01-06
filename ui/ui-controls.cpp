// ui-controls.cpp: Controls + Main menu + Xbox functions
// @2022 octopoulos

#include "ui-controls.h"
#include "ui-file.h"
#include "ui-games.h"
#include "ui-log.h"
#include "ui-settings.h"

#include "implot/implot.h"

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

extern ImFont* g_fixed_width_font;
extern int     g_user_asked_for_intercept;
extern int     want_screenshot;

exiso::GameInfo gameInfo;

// xemu.cpp
int  xemu_is_fullscreen();
void xemu_toggle_fullscreen();

namespace ui
{

static ControlsWindow controlsWindow;
ControlsWindow&       GetControlsWindow() { return controlsWindow; }

// FUNCTIONS
////////////

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
		if (IsRunning())
			vm_stop(RUN_STATE_PAUSED);
		break;
	case 1:
		if (!IsRunning())
			vm_start();
		break;
	case 2:
		if (IsRunning())
			vm_stop(RUN_STATE_PAUSED);
		else
			vm_start();
		break;
	}
}

// CONTROLS
///////////

static std::vector<std::string> buttonNames = {
	"Config",
	"FullScr",
	"FullScr2",
	"Grid",
	"List",
	"Open",
	"Pads",
	"Pause",
	"Refresh",
	"Restart",
	"Start",
	"Stop",
};

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

void ControlsWindow::Draw()
{
	if (!isOpen)
		return;

	if (!drawn)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		auto&                size     = viewport->WorkSize;
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(size.x, 64.0f));

		LoadTextures("buttons", buttonNames);
		++drawn;
	}

	if (!SetAlpha(alpha))
		return;

	if (!ImGui::Begin("Controls", &isOpen, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration))
	{
		ImGui::End();
		return;
	}

	ImGui::PushFont(g_fixed_width_font);
	if (RowButton("Open")) LoadDisc();
	if (RowButton("Refresh")) ScanGamesFolder();
	if (RowButton("FullScr")) xemu_toggle_fullscreen();
	if (RowButton("Stop")) EjectDisc();
	if (RowButton(IsRunning() ? "Pause" : "Start")) TogglePause();
	if (RowButton("Config")) OpenConfig(1);
	if (RowButton("Pads")) OpenConfig(10);
	if (RowButton("List")) GetGamesWindow().SetGrid(false);
	if (RowButton("Grid")) GetGamesWindow().SetGrid(true);
	ImGui::PopFont();

	if (GetGamesWindow().isOpen)
	{
		ImGui::PushItemWidth(200);
		float offset = (64.0f - ImGui::CalcTextSize("Scale").y) / 2;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
		ImGui::SliderInt("Scale", &xsettings.row_height, 24, 176);
		ImGui::PopItemWidth();
		// ImGui::SameLine();
		// ImGui::InputText("Search", search, sizeof(str2k));
	}

	// saved
	ImGui::End();
	ImGui::PopStyleColor();
}

// MAIN MENU
////////////

static bool showImGuiDemo  = false;
static bool showImPlotDemo = false;

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
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Controls", xsettings.shortcut_controls)) controlsWindow.Toggle();
			if (ImGui::MenuItem("Game List", xsettings.shortcut_games)) GetGamesWindow().Toggle();
			if (ImGui::MenuItem("Log", xsettings.shortcut_log)) GetLogWindow().Toggle();
			ImGui::Separator();
			ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo);
			ImGui::MenuItem("ImPlot Demo", nullptr, &showImPlotDemo);
			if (ImGui::MenuItem("Fullscreen", xsettings.shortcut_fullscreen, xemu_is_fullscreen(), true)) xemu_toggle_fullscreen();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Utilities"))
		{
			// ImGui::MenuItem("Monitor", xsettings.shortcut_monitor, &monitor_window.isOpen);
			// ImGui::MenuItem("Audio", nullptr, &apu_window.isOpen);
			// ImGui::MenuItem("Video", nullptr, &video_window.isOpen);

			ImGui::Separator();
			if (ImGui::MenuItem("Extract ISO")) exiso::DecodeXiso(FileOpenISO(""));
			if (ImGui::MenuItem("Create ISO")) exiso::CreateXiso(FileOpenISO(""));

			ImGui::Separator();
			if (ImGui::MenuItem("Screenshot", xsettings.shortcut_screenshot)) want_screenshot = (1 + 4) + 2; // force screenshot + maybe icon
			if (ImGui::MenuItem("Save Icon")) want_screenshot = 2 + 8;                                       // force icon
			if (ImGui::MenuItem("Intercept", xsettings.shortcut_intercept)) GetFileWindow().Show();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Help", nullptr)) xemu_open_web_browser("https://xemu.app/docs/getting-started/");

			// ImGui::MenuItem("Report Compatibility...", nullptr, &compatibility_reporter_window.isOpen);
			// ImGui::MenuItem("Check for Updates...", nullptr, &update_window.isOpen);

			// ImGui::Separator();
			// ImGui::MenuItem("About", nullptr, &about_window.isOpen);
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

// Draw all UI
void Draw()
{
	controlsWindow.Draw();
	GetFileWindow().Draw();
	GetGamesWindow().Draw();
	GetLogWindow().Draw();
	GetSettingsWindow().Draw();

	if (showImGuiDemo) ImGui::ShowDemoWindow(&showImGuiDemo);
	if (showImPlotDemo) ImPlot::ShowDemoWindow(&showImPlotDemo);
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
	bool prevRunning = IsRunning();
	if (!hold || !xsettings.guide || (!lastChange && IsRunning()))
		TogglePause();

	// if game is running => hide the windows otherwise show them
	bool running = IsRunning();
	if (value > 1 || (running && xsettings.run_no_ui))
		lastChange = ShowWindows(running ? 0 : 2);
	else
		lastChange = false;
}

bool ShowWindows(int show)
{
	bool changed = 0;
	changed |= controlsWindow.Show(show);
	changed |= GetFileWindow().Show(show);
	changed |= GetGamesWindow().Show(show);
	changed |= GetLogWindow().Show(show);
	changed |= GetSettingsWindow().Show(show);
	return changed;
}

} // namespace ui
