// ui-file.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

namespace ui
{

class FileWindow
{
public:
    bool isOpen = true;
    bool isFolder = false;

    void Draw();
};

FileWindow& GetFileWindow();

}
