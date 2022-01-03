// log.h
// @2022 octopoulos

#pragma once

#include "common.h"

namespace ui
{

class LogWindow
{
public:
	bool is_open = true;

    void Initialize();
	void Draw();
};

void AddLog(std::string text);

} // namespace ui
