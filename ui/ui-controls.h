// ui-controls.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

namespace ui
{

class ControlsWindow : public CommonWindow
{
public:
	ControlsWindow() { isOpen = manualOpen = true; }
	void Draw();
};

ControlsWindow& GetControlsWindow();
void            Draw();
void            EjectDisc();
std::string     FileOpen(const char* filters, std::string current);
std::string     FileOpenISO(std::string current);
float           GetMenuHeight();
void            HomeGuide(bool value);
bool            IsRunning();
bool            LoadDisc(std::string filename = "", bool saveSetting = true);
void            LoadingGame(std::string path);
const char*     PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name);
void            Reset();
void            ShowMainMenu(float alpha);
bool            ShowWindows(int show);
void            ShutDown();
void            TogglePause(int status = 2);

} // namespace ui
