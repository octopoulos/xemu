// ui-file.cpp
// @2022 octopoulos

#include "ui-file.h"

namespace ui
{

void FileWindow::Draw()
{
	if (!isOpen)
		return;

	if (!ImGui::Begin(isFolder ? "Select a folder" : "Select a file", &isOpen))
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

		ImGui::TextUnformatted(filename.c_str());
	}

	ImGui::End();
}

static FileWindow fileWindow;

FileWindow& GetFileWindow() { return fileWindow; }

} // namespace ui
