// ui-common.cpp
// @2022 octopoulos

#include "ui-common.h"
#include "xemu-shaders.h" // load_texture_from_file

namespace ui
{

static std::map<std::string, uint32_t> textures;

uint32_t LoadTexture(std::filesystem::path path, std::string name)
{
	if (!std::filesystem::exists(path))
		return 0;

	auto texId = load_texture_from_file(path.string().c_str(), 0);
	if (texId)
		textures[name] = texId;
	return texId;
}

bool LoadTextures(std::string folder, std::vector<std::string> names)
{
	std::filesystem::path basePath = xsettingsFolder(nullptr);
	basePath /= folder;

	bool success = true;
	for (auto& name : names)
	{
		std::filesystem::path path = basePath / (name + ".png");
		if (!LoadTexture(path, name))
			success = false;
	}
	return success;
}

/**
 * Image text button aligned on a row
 */
int RowButton(std::string name)
{
	const ImVec2 buttonDims(32.0f, 32.0f);
	const ImVec2 childDims(64.0f, 64.0f);

	auto nameStr = name.c_str();

	ImGui::BeginChild(nameStr, childDims, true, ImGuiWindowFlags_NoScrollbar);
	if (textures.contains(name))
	{
		float x = ImGui::GetCursorPosX();
		ImGui::SetCursorPosX(x + 8.0f);
		ImGui::Image((void*)(intptr_t)textures[name], buttonDims);
		if (xsettings.text_button)
		{
			float offset = (childDims.x - ImGui::CalcTextSize(nameStr).x) / 2;
			ImGui::SetCursorPosX(x + std::max(offset, -8.0f));
			ImGui::TextUnformatted(nameStr);
		}
	}
	else
		ImGui::Button(nameStr, childDims);
	ImGui::EndChild();

	int flag = ImGui::IsItemClicked() ? 1 : 0;
	ImGui::SameLine();
	return flag;
}

} // namespace ui
