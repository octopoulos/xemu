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

    void Initialize();
	void Draw();
};

void CheckIcon(std::string uid);
void LoadedGame(std::string uid);
void OpenGamesList();
void SaveGamesList();
void ScanGamesFolder();

} // namespace ui
