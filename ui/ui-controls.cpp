// ui-controls.cpp
// @2022 octopoulos

#include "ui-controls.h"
#include "ui-games.h"
#include "ui-settings.h"
#include "xemu-hud.h"

namespace ui
{

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

/**
 * Load the icons for the top bar
 */
void ControlsWindow::Initialize()
{
    LoadTextures("buttons", buttonNames);
}

void ControlsWindow::Draw()
{
	if (!isOpen)
		return;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();

    static int step = 0;
    if (!step)
    {
        auto& size = viewport->WorkSize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(size.x, 64.0f));
        ++step;
    }

	if (!ImGui::Begin("Controls", &isOpen, ImGuiWindowFlags_NoTitleBar))
	{
		ImGui::End();
		return;
	}

	if (ImageTextButton("Open")) LoadDisc();
	ImGui::SameLine();
	if (ImageTextButton("Refresh")) ScanGamesFolder();
	ImGui::SameLine();
	if (ImageTextButton("FullScr")) {}
	ImGui::SameLine();
	if (ImageTextButton("Stop")) {}
	ImGui::SameLine();
	if (ImageTextButton(IsRunning() ? "Pause" : "Start")) TogglePause();
	ImGui::SameLine();
	if (ImageTextButton("Config")) OpenConfig(1);
	ImGui::SameLine();
	if (ImageTextButton("Pads")) OpenConfig(10);
	ImGui::SameLine();
	if (ImageTextButton("List")) GetGamesWindow().isGrid = 0;
	ImGui::SameLine();
	if (ImageTextButton("Grid")) GetGamesWindow().isGrid = 1;
	ImGui::SameLine();
    ImGui::PushItemWidth(200);
	ImGui::SliderInt("Scale", &xsettings.row_height, 24, 176);
    ImGui::PopItemWidth();
	// ImGui::SameLine();
	// ImGui::InputText("Search", search, sizeof(str2k));

	// saved
	ImGui::End();
}

static ControlsWindow controlsWindow;

ControlsWindow& GetControlsWindow() { return controlsWindow; }

} // namespace ui
