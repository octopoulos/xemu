// ui-settings.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

#include <SDL.h>

namespace ui
{

class SettingsWindow : public CommonWindow
{
private:
	int  changed    = 0;
	bool clickedNow = 0;
	int  tab        = 0;
	int  tabMenu    = 0;

public:
	SettingsWindow();

	void Draw();
	void Load();
	void Save();
	void OpenTab(int tabMenu_);

private:
	void DrawCPU();
	void DrawGPU();
	void DrawAudio();
	void DrawPads();
	void DrawSystem();
	void DrawNetwork();
	void DrawAdvanced();
	void DrawEmulator();
	void DrawGUI();
	void DrawDebug();
};

SettingsWindow& GetSettingsWindow();
void            OpenConfig(int tab);
void            ProcessSDL(SDL_Event* event);
void            ProcessShortcuts();
void            UpdateIO();

} // namespace ui
