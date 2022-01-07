// ui-file.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"

namespace ui
{

class FileWindow : public CommonWindow
{
public:
	bool isFolder = false;

	void Draw()
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin(isFolder ? "Select a folder" : "Select a file", &isOpen, ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		auto basePath = std::filesystem::current_path();
		for (auto& dir_entry : std::filesystem::directory_iterator { basePath })
		{
			auto& path     = dir_entry.path();
			auto  filename = path.filename().string();
			auto  status   = std::filesystem::status(path);
			auto  type     = status.type();

			ImGui::Selectable(filename.c_str());
			// ImGui::TextUnformatted(filename.c_str());
		}

		ImGui::End();
	}
};

static FileWindow fileWindow;
CommonWindow&     GetFileWindow() { return fileWindow; }

} // namespace ui
