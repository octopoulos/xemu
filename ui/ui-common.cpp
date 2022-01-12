// ui-common.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"
#include "xemu-shaders.h" // load_texture_from_file

namespace ui
{

static std::unordered_map<std::string, uint32_t> textures;

// HELPERS
//////////

bool AddCombo(std::string name, const char* text)
{
	if (auto config = ConfigFind(name))
		return ImGui::Combo(text, (int*)config->ptr, config->names, config->count);
	return false;
}

bool AddCombo(std::string name, const char* text, const char* texts[], const std::vector<int> values)
{
	if (auto config = ConfigFind(name))
	{
		auto it    = std::find(values.begin(), values.end(), *(int*)config->ptr);
		int  index = (it != values.end()) ? std::distance(values.begin(), it) : 0;

		if (ImGui::Combo(text, &index, texts, values.size()))
		{
			*(int*)config->ptr = values[index];
			return true;
		}
	}
	return false;
}

bool AddSliderFloat(std::string name, const char* text, const char* format)
{
	if (auto config = ConfigFind(name))
		return ImGui::SliderFloat(text, (float*)config->ptr, config->minFloat, config->maxFloat, format);
	return false;
}

bool AddSliderInt(std::string name, const char* text, const char* format, bool vertical, const ImVec2& size)
{
	if (auto config = ConfigFind(name))
		return vertical? ImGui::VSliderInt(text, size, (int*)config->ptr, config->minInt, config->maxInt, format)
		: ImGui::SliderInt(text, (int*)config->ptr, config->minInt, config->maxInt, format);
	return false;
}

void AddSpace(int height)
{
	if (height < 0)
		height = -height * ImGui::GetStyle().WindowPadding.y;

	ImGui::Dummy(ImVec2(0, height * xsettings.ui_scale));
}

// IMAGES
/////////

uint32_t LoadTexture(std::filesystem::path path, std::string name)
{
	if (!std::filesystem::exists(path))
		return 0;

	auto texId = load_texture_from_file(path.string().c_str(), 0);
	if (texId)
		textures[name] = texId;
	return texId;
}

uint32_t LoadTexture(const uint8_t* data, uint32_t size, std::string name)
{
	auto texId = load_texture_from_memory(data, size, 0);
	if (texId)
		textures[name] = texId;
	return texId;
}

static CommonWindow commonWindow;
CommonWindow&       GetCommonWindow() { return commonWindow; }

} // namespace ui
