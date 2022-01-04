// ui-controls.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

namespace ui
{

class ControlsWindow
{
public:
	bool isOpen = true;

    void Initialize();
	void Draw();
};

ControlsWindow& GetControlsWindow();

} // namespace ui
