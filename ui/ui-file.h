// ui-file.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

namespace ui
{

class FileWindow : public CommonWindow
{
public:
	bool isFolder = false;

	void Draw();
};

FileWindow& GetFileWindow();

} // namespace ui
