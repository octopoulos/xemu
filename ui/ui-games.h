// ui-games.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"
#include "extract-xiso.h"

namespace ui
{

class GamesWindow : public CommonWindow
{
public:
	bool isGrid = false;

	GamesWindow() { isOpen = manualOpen = true; }
	void Draw();
	void SetGrid(bool grid);
};

GamesWindow& GetGamesWindow();
void         CheckIcon(std::string uid);
void         LoadedGame(std::string uid);
void         OpenGamesList();
void         SaveGamesList();
void         ScanGamesFolder();

} // namespace ui
