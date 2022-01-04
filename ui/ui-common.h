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

std::string FileOpen(const char* filters, std::string current);
std::string FileOpenISO(std::string current);
bool        ImageTextButton(std::string name);
bool        IsRunning();
void        LoadDisc();
uint32_t    LoadTexture(std::filesystem::path path, std::string name);
bool        LoadTextures(std::string folder, std::vector<std::string> names);
const char* PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name);
void        TogglePause();

} // namespace ui
