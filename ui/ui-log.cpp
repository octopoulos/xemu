// ui-log.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"
#include "stb_sprintf.h"

extern ImFont* fixedFont;

namespace ui
{

const ImVec4 colorValues[] = {
	{1.0f,  1.0f, 1.0f, 1.0f}, // text
	{ 1.0f, 0.5f, 0.5f, 1.0f}, // error
	{ 0.3f, 0.7f, 1.0f, 1.0f}, // info
	{ 1.0f, 0.8f, 0.5f, 1.0f}, // warning
};

class LogWindow : public CommonWindow
{
public:
	LogWindow() { isOpen = manualOpen = true; }

	void AddLog(int color, std::string text)
	{
		colors.push_back(color);
		lines.push_back(text);
	}

	void Draw()
	{
		if (!isOpen)
			return;

		if (!ImGui::Begin("Log", &isOpen))
		{
			ImGui::End();
			return;
		}

		ImGui::PushFont(fixedFont);
		ImGuiListClipper clipper;
		clipper.Begin(lines.size());
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				if (colors[i]) ImGui::PushStyleColor(ImGuiCol_Text, colorValues[colors[i]]);
				ImGui::TextUnformatted(lines[i].c_str());
				if (colors[i]) ImGui::PopStyleColor();
			}
		}
		clipper.End();
		ImGui::PopFont();

		// auto scroll
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			ImGui::SetScrollHereY(1.0f);

		ImGui::End();
	}

private:
	std::vector<int>         colors;
	std::vector<std::string> lines;
};

static LogWindow logWindow;
CommonWindow&    GetLogWindow() { return logWindow; }

// API
//////

/**
 * Add Log
 * @param color 0:log, 1:error, 2:info, 3:warning
 */
void AddLogV(int color, const char* fmt, va_list args)
{
	const int   bufSize = 2048;
	static char buf[bufSize];

	int w = stbsp_vsnprintf(buf, (int)bufSize, fmt, args);
	if (w == -1 || w >= (int)bufSize)
		w = (int)bufSize - 1;
	buf[w] = 0;

	fwrite(buf, 1, w, stderr);
	fputc('\n', stderr);
	logWindow.AddLog(color, buf);
}

void AddLog(int color, std::string text)
{
	fwrite(text.c_str(), 1, text.size(), stderr);
	fputc('\n', stderr);
	logWindow.AddLog(color, text);
}

void Log(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	AddLogV(0, fmt, args);
	va_end(args);
}

void LogError(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	AddLogV(1, fmt, args);
	va_end(args);
}

void LogInfo(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	AddLogV(2, fmt, args);
	va_end(args);
}

void LogWarning(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	AddLogV(3, fmt, args);
	va_end(args);
}

void Log       (std::string text) { AddLog(0, text); }
void LogError  (std::string text) { AddLog(1, text); }
void LogInfo   (std::string text) { AddLog(2, text); }
void LogWarning(std::string text) { AddLog(3, text); }

} // namespace ui
