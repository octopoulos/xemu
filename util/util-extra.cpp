// util-extra.cpp
// @2022 octopoulos

#include "../ui/ui-log.h"

// 0:log, 1:error, 2:info, 3:warning
extern "C" void LogExtra(int color, const char* fmt, va_list args)
{
	ui::AddLogV(color, fmt, args);
}
