// settings.h
// @2022 octopoulos

#pragma once

#include "common.h"

namespace ui
{

class SettingsWindow
{
public:
	bool is_open = false;

private:
	int  changed    = 0;
	bool clickedNow = 0;
	int  tab        = 0;
	int  tabMenu    = 0;

public:
	SettingsWindow();

	void Load();
	void Save();
	void OpenTab(int tabMenu_);
	void Draw();

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

} // namespace ui
