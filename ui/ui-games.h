// ui-games.h
// @2021 octopoulos

#pragma once

#include "ui-common.h"
#include "extract-xiso.h"

namespace ui
{

class GamesWindow
{
public:
	bool isOpen = true;
	int  isGrid = false;

	void Initialize() {}
	void Draw();
};

GamesWindow& GetGamesWindow();
void         CheckIcon(std::string uid);
void         LoadedGame(std::string uid);
void         OpenGamesList();
void         SaveGamesList();
void         ScanGamesFolder();

} // namespace ui
