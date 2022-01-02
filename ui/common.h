// common.h
// @2021 octopoulos

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

#include <fmt/core.h>
#include "imgui/imgui.h"

#include "xsettings.h"

namespace ui
{

bool        ImageTextButton(std::string name);
bool        IsRunning();
void        LoadDisc();
uint32_t    LoadTexture(std::filesystem::path path, std::string name);
bool        LoadTextures(std::string folder, std::vector<std::string> names);
const char* PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name);
void        TogglePause();

} // namespace ui
