// ui-common.h
// @2022 octopoulos

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

#include <fmt/core.h>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "xsettings.h"

namespace ui
{

class CommonWindow
{
public:
	float alpha      = 1.0f;
	int   drawn      = 0;
	int   focus      = 0;
	bool  isOpen     = false;
	bool  manualOpen = false;

	virtual ~CommonWindow() {}
	virtual void Draw() {}

	// 0:hide, 1:show, 2:restore
	bool Show(int show = 1)
	{
		bool prevOpen = isOpen;
		isOpen        = (show == 0) ? false : ((show == 1) ? true : manualOpen);
		return (isOpen != prevOpen);
	}

	void Toggle() { isOpen = manualOpen = !isOpen; }
};

uint32_t LoadTexture(std::filesystem::path path, std::string name);
bool     LoadTextures(std::string folder, std::vector<std::string> names);
int      RowButton(std::string name);

} // namespace ui
