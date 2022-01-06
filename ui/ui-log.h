// ui-log.h
// @2022 octopoulos

#pragma once

#include "ui-common.h"

namespace ui
{

class LogWindow : public CommonWindow
{
public:
	LogWindow() { isOpen = manualOpen = true; }
	void AddLog(int color, std::string text);
	void Draw();

private:
	std::vector<int>         colors;
	std::vector<std::string> lines;
};

LogWindow& GetLogWindow();
void       AddLogV(int color, const char* fmt, va_list args);
void       Log       (const char* fmt, ...);
void       LogError  (const char* fmt, ...);
void       LogInfo   (const char* fmt, ...);
void       LogWarning(const char* fmt, ...);
void       Log       (std::string text);
void       LogError  (std::string text);
void       LogInfo   (std::string text);
void       LogWarning(std::string text);

} // namespace ui
