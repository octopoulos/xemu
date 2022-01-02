// controls.cpp
// @2022 octopoulos

#include "common.h"
#include "controls.h"
#include "games.h"
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
	if (!is_open)
		return;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockSpaceOverViewport(viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    static int step = 0;
    if (!step)
    {
        auto& size = viewport->WorkSize;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + size.x * 0.1f, viewport->WorkPos.y + size.y * 0.1f));
        ImGui::SetNextWindowSize(ImVec2(size.x * 0.8f, size.y * 0.8f));
        ++step;
    }

	if (!ImGui::Begin("Game List", &is_open))
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
	if (ImageTextButton("List")) {}
	ImGui::SameLine();
	if (ImageTextButton("Grid")) {}
	ImGui::SameLine();
    ImGui::PushItemWidth(200);
	ImGui::SliderInt("Scale", &xsettings.row_height, 24, 176);
    ImGui::PopItemWidth();
	// ImGui::SameLine();
	// ImGui::InputText("Search", search, sizeof(str2k));

	float icon_height = xsettings.row_height * 1.0f;
	ImVec2 icon_dims = { icon_height * 16.0f / 9.0f, icon_height };

	// saved
	ImGui::End();
}

} // namespace ui
