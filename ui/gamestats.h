// gamestats.h
// @2021 octopoulos

#pragma once

#include "extract-xiso.h"

namespace ui
{

class GamesWindow
{
public:
	bool is_open = true;

	void Draw();
};

void LoadedGame(std::string key);
void OpenGamesList();
void SaveGamesList();
void ScanGamesFolder();

} // namespace ui
