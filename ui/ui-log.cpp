// ui-log.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"
#include "stb_sprintf.h"

namespace ui
{

const char* colorNames[] = { "All", "Text", "Error", "Info", "Warning" };

const ImVec4 colorValues[] = {
	{1.0f,  1.0f, 1.0f, 1.0f}, // text
	{ 1.0f, 0.5f, 0.5f, 1.0f}, // error
	{ 0.3f, 0.7f, 1.0f, 1.0f}, // info
	{ 1.0f, 0.8f, 0.5f, 1.0f}, // warning
};

struct LogEntry
{
	int         color;
	std::string date;
	std::string text;
};

class LogWindow : public CommonWindow
{
public:
	LogWindow()
	{
		name   = "Log";
		isOpen = true;
	}

	void AddLog(int color, std::string text)
	{
		colorLines[0].push_back({ color, "", text });
		colorLines[color + 1].push_back({ color, "", text });
	}

	void Draw()
	{
		CHECK_DRAW();
		if (!ImGui::Begin("Log", &isOpen, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginTabBar("Log#tabs"))
		{
			for (int i = 0; i < 5; ++i)
				if (ImGui::BeginTabItem(colorNames[i]))
				{
					active = i - 1;
					ImGui::EndTabItem();
				}
			ImGui::EndTabBar();
		}

		auto   lines  = colorLines[active + 1];
		auto   region = ImGui::GetContentRegionAvail();
		ImVec2 childDims(region.x, region.y);

		ImGui::BeginChild("Scroll", childDims, false);
		{
			ImGui::PushFont(FindFont("mono"));
			ImGuiListClipper clipper;
			clipper.Begin(lines.size());
			while (clipper.Step())
			{
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
				{
					auto color = lines[i].color;
					if (color) ImGui::PushStyleColor(ImGuiCol_Text, colorValues[color]);
					ImGui::TextUnformatted(lines[i].text.c_str());
					if (color) ImGui::PopStyleColor();
				}
			}
			clipper.End();
			ImGui::PopFont();

			// auto scroll
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

		}
		ImGui::EndChild();
		ImGui::End();
	}

private:
	int                   active = -1;
	std::vector<LogEntry> colorLines[5];
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

// clang-format off
void Log       (std::string text) { AddLog(0, text); }
void LogError  (std::string text) { AddLog(1, text); }
void LogInfo   (std::string text) { AddLog(2, text); }
void LogWarning(std::string text) { AddLog(3, text); }
// clang-format on

} // namespace ui
