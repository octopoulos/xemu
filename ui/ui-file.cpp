// ui-file.cpp
// @2022 octopoulos

#include "ui-file.h"

namespace ui
{

static FileWindow fileWindow;
FileWindow&       GetFileWindow() { return fileWindow; }

void FileWindow::Draw()
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

} // namespace ui
