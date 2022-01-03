/*
 * xemu-hud.cpp
 *
 * Copyright (C) 2021 octopoulos
 * Copyright (C) 2021 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <deque>
#include <epoxy/gl.h>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "xemu-hud.h"
#include "xemu-input.h"
#include "xemu-notifications.h"
#include "xsettings.h"
#include "xemu-shaders.h"
#include "xemu-custom-widgets.h"
#include "xemu-monitor.h"
#include "xemu-version.h"
#include "xemu-net.h"
#include "xemu-os-utils.h"
#include "xemu-xbe.h"
#include "xemu-reporting.h"

#if defined(_WIN32)
#	include "xemu-update.h"
#endif

#include "data/roboto_medium.ttf.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "implot/implot.h"
#include "games.h"
#include "hw/xbox/nv2a/intercept.h"
#include "settings.h"

extern "C"
{
#include "qemui/noc_file_dialog.h"

// Include necessary QEMU headers
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hw/xbox/mcpx/apu_debug.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/nv2a.h"
#include "net/pcap.h"

#undef atomic_fetch_add
#undef atomic_fetch_and
#undef atomic_fetch_xor
#undef atomic_fetch_or
#undef atomic_fetch_sub
}

extern FBO* controller_fbo;
extern FBO* logo_fbo;
extern int  g_user_asked_for_intercept;
extern int  want_screenshot;

static ImFont* g_fixed_width_font;
float          g_main_menu_height;
bool           g_trigger_style_update = true;

struct AxisButtonPos
{
	int   id;
	float x;
	float y;
	int   type;  // 1:button, 2:axis
	int   align; // 0:center, 1:left, 2:right
	int   value;
};
const struct AxisButtonPos ab_buttons[] = {
	// buttons
	{ 0, 498, 240, 1, 1, CONTROLLER_BUTTON_A },
	{ 1, 498, 198, 1, 1, CONTROLLER_BUTTON_B },
	{ 2, 470, 223, 1, 2, CONTROLLER_BUTTON_X },
	{ 3, 470, 180, 1, 2, CONTROLLER_BUTTON_Y },
	{ 4, 0, 390, 1, 2, CONTROLLER_BUTTON_DPAD_LEFT },
	{ 5, 16, 350, 1, 0, CONTROLLER_BUTTON_DPAD_UP },
	{ 6, 28, 390, 1, 1, CONTROLLER_BUTTON_DPAD_RIGHT },
	{ 7, 16, 430, 1, 0, CONTROLLER_BUTTON_DPAD_DOWN },
	{ 8, 222, 470, 1, 2, CONTROLLER_BUTTON_BACK },
	{ 9, 270, 470, 1, 1, CONTROLLER_BUTTON_START },
	{ 10, 435, 70, 1, 1, CONTROLLER_BUTTON_WHITE },
	{ 11, 465, 110, 1, 1, CONTROLLER_BUTTON_BLACK },
	{ 12, 16, 190, 1, 0, CONTROLLER_BUTTON_LSTICK },
	{ 13, 468, 470, 1, 0, CONTROLLER_BUTTON_RSTICK },
	{ 14, 246, 240, 1, 0, CONTROLLER_BUTTON_GUIDE },
	// axes
	{ 22, 222, 30, 2, 2, CONTROLLER_AXIS_LTRIG },
	{ 23, 270, 30, 2, 1, CONTROLLER_AXIS_RTRIG },
	{ 24, 0, 110, 2, 2, CONTROLLER_AXIS_LSTICK_X },
	{ 25, 16, 70, 2, 0, CONTROLLER_AXIS_LSTICK_Y },
	{ 26, 28, 110, 2, 1, CONTROLLER_AXIS_LSTICK_X },
	{ 27, 16, 150, 2, 0, CONTROLLER_AXIS_LSTICK_Y },
	{ 28, 452, 390, 2, 2, CONTROLLER_AXIS_RSTICK_X },
	{ 29, 468, 350, 2, 0, CONTROLLER_AXIS_RSTICK_Y },
	{ 30, 480, 390, 2, 1, CONTROLLER_AXIS_RSTICK_X },
	{ 31, 468, 430, 2, 0, CONTROLLER_AXIS_RSTICK_Y },
};

static AxisButtonPos* selectedInput  = nullptr;
static int            selectedType   = -1;
static int            selectedButton = -1;
static int            selectedKey    = -1;
static int            last_button    = -1;
static int            last_key       = -1;

class NotificationManager
{
private:
	const int               kNotificationDuration = 4000;
	std::deque<const char*> notification_queue;
	bool                    active;
	uint32_t                notification_end_ts;
	const char*             msg;

public:
	NotificationManager() { active = false; }
	~NotificationManager() {}

	void QueueNotification(const char* msg_, bool instant)
	{
		if (instant && active)
		{
			free((void*)msg);
			active = false;
		}
		notification_queue.push_back(strdup(msg_));
	}

	void Draw()
	{
		uint32_t now = SDL_GetTicks();

		if (active)
		{
			// Currently displaying a notification
			float t = (notification_end_ts - now) / (float)kNotificationDuration;
			if (t > 1.0)
			{
				// Notification delivered, free it
				free((void*)msg);
				active = false;
			}
			else
			{
				// Notification should be displayed
				DrawNotification(t, msg);
			}
		}
		else
		{
			// Check to see if a notification is pending
			if (notification_queue.size() > 0)
			{
				msg                 = notification_queue[0];
				active              = true;
				notification_end_ts = now + kNotificationDuration;
				notification_queue.pop_front();
			}
		}
	}

private:
	void DrawNotification(float t, const char* msg)
	{
		const float DISTANCE = 10.0f;
		static int  corner   = 1;
		auto&       io       = ImGui::GetIO();
		if (corner != -1)
		{
			ImVec2 window_pos       = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
			window_pos.y            = g_main_menu_height + DISTANCE;
			ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
			ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		}

		const float fade_in  = 0.1;
		const float fade_out = 0.9;
		float       fade     = 0;

		if (t < fade_in)
		{
			// Linear fade in
			fade = t / fade_in;
		}
		else if (t >= fade_out)
		{
			// Linear fade out
			fade = 1 - (t - fade_out) / (1 - fade_out);
		}
		else
		{
			// Constant
			fade = 1.0;
		}

		ImVec4 color = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
		color.w *= fade;
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, fade * 0.9f));
		ImGui::PushStyleColor(ImGuiCol_Border, color);
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::SetNextWindowBgAlpha(0.90f * fade);
		if (ImGui::Begin("Notification", nullptr, ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
			ImGui::Text("%s", msg);

		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		ImGui::End();
	}
};

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

static void Hyperlink(const char* text, const char* url)
{
	// FIXME: Color text when hovered
	ImColor col;
	ImGui::Text("%s", text);
	if (ImGui::IsItemHovered())
	{
		col = IM_COL32_WHITE;
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	}
	else col = ImColor(127, 127, 127, 255);

	ImVec2 max = ImGui::GetItemRectMax();
	ImVec2 min = ImGui::GetItemRectMin();
	min.x -= 1 * xsettings.ui_scale;
	min.y = max.y;
	max.x -= 1 * xsettings.ui_scale;
	ImGui::GetWindowDrawList()->AddLine(min, max, col, 1.0 * xsettings.ui_scale);

	if (ImGui::IsItemClicked())
		xemu_open_web_browser(url);
}

static int PushWindowTransparencySettings(bool transparent, float alpha_transparent = 0.4, float alpha_opaque = 1.0)
{
	float alpha = transparent ? alpha_transparent : alpha_opaque;

	ImVec4 c;
	c = ImGui::GetStyle().Colors[transparent ? ImGuiCol_WindowBg : ImGuiCol_TitleBg];
	c.w *= alpha;
	ImGui::PushStyleColor(ImGuiCol_TitleBg, c);

	c = ImGui::GetStyle().Colors[transparent ? ImGuiCol_WindowBg : ImGuiCol_TitleBgActive];
	c.w *= alpha;
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, c);

	c = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
	c.w *= alpha;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, c);

	c = ImGui::GetStyle().Colors[ImGuiCol_Border];
	c.w *= alpha;
	ImGui::PushStyleColor(ImGuiCol_Border, c);

	c = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
	c.w *= alpha;
	ImGui::PushStyleColor(ImGuiCol_FrameBg, c);

	return 5;
}

class MonitorWindow
{
public:
	bool is_open = false;

private:
	char                  InputBuf[256];
	ImVector<char*>       Items;
	ImVector<const char*> Commands;
	ImVector<char*>       History;
	int                   HistoryPos; //-1: new line, 0..History.Size-1 browsing history.
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;

public:
	MonitorWindow()
	{
		memset(InputBuf, 0, sizeof(InputBuf));
		HistoryPos     = -1;
		AutoScroll     = true;
		ScrollToBottom = false;
	}
	~MonitorWindow() {}

	// Portable helpers
	static char* Strdup(const char* str)
	{
		size_t len = strlen(str) + 1;
		void*  buf = malloc(len);
		IM_ASSERT(buf);
		return (char*)memcpy(buf, (const void*)str, len);
	}
	static void Strtrim(char* str)
	{
		char* str_end = str + strlen(str);
		while (str_end > str && str_end[-1] == ' ') str_end--;
		*str_end = 0;
	}

	void Draw()
	{
		if (!is_open)
			return;
		int    style_pop_cnt = PushWindowTransparencySettings(true);
		auto&  io            = ImGui::GetIO();
		ImVec2 window_pos    = ImVec2(0, io.DisplaySize.y / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y / 2), ImGuiCond_Appearing);
		if (ImGui::Begin("Monitor", &is_open, ImGuiWindowFlags_NoCollapse))
		{
			const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();             // 1 separator, 1 input text
			ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 separator + 1 InputText

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
			ImGui::PushFont(g_fixed_width_font);
			ImGui::TextUnformatted(xemu_get_monitor_buffer());
			ImGui::PopFont();

			if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
				ImGui::SetScrollHereY(1.0f);
			ScrollToBottom = false;

			ImGui::PopStyleVar();
			ImGui::EndChild();
			ImGui::Separator();

			// Command-line
			bool reclaim_focus = ImGui::IsWindowAppearing();

			ImGui::SetNextItemWidth(-1);
			ImGui::PushFont(g_fixed_width_font);
			if (ImGui::InputText("##text", InputBuf, IM_ARRAYSIZE(InputBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory, &TextEditCallbackStub, (void*)this))
			{
				char* s = InputBuf;
				Strtrim(s);
				if (s[0]) ExecCommand(s);
				strcpy(s, "");
				reclaim_focus = true;
			}
			ImGui::PopFont();

			// Auto-focus on window apparition
			ImGui::SetItemDefaultFocus();
			if (reclaim_focus)
				ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
		}
		ImGui::End();
		ImGui::PopStyleColor(style_pop_cnt);
	}

	void toggle_open() { is_open = !is_open; }

private:
	void ExecCommand(const char* command_line)
	{
		xemu_run_monitor_command(command_line);

		// Insert into history. First find match and delete it so it can be pushed to the back. This isn't trying to be
		// smart or optimal.
		HistoryPos = -1;
		for (int i = History.Size - 1; i >= 0; i--)
			if (stricmp(History[i], command_line) == 0)
			{
				free(History[i]);
				History.erase(History.begin() + i);
				break;
			}
		History.push_back(Strdup(command_line));

		// On commad input, we scroll to bottom even if AutoScroll==false
		ScrollToBottom = true;
	}

	// In C++11 you are better off using lambdas for this sort of forwarding callbacks
	static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
	{
		MonitorWindow* console = (MonitorWindow*)data->UserData;
		return console->TextEditCallback(data);
	}

	int TextEditCallback(ImGuiInputTextCallbackData* data)
	{
		switch (data->EventFlag)
		{
		case ImGuiInputTextFlags_CallbackHistory:
		{
			// Example of HISTORY
			const int prev_history_pos = HistoryPos;
			if (data->EventKey == ImGuiKey_UpArrow)
			{
				if (HistoryPos == -1)
					HistoryPos = History.Size - 1;
				else if (HistoryPos > 0)
					HistoryPos--;
			}
			else if (data->EventKey == ImGuiKey_DownArrow)
			{
				if (HistoryPos != -1)
					if (++HistoryPos >= History.Size)
						HistoryPos = -1;
			}

			// A better implementation would preserve the data on the current input line along with cursor position.
			if (prev_history_pos != HistoryPos)
			{
				const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
				data->DeleteChars(0, data->BufTextLen);
				data->InsertChars(0, history_str);
			}
		}
		break;
		}
		return 0;
	}
};

class InputWindow
{
public:
	bool is_open = false;

	InputWindow() {}
	~InputWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		ImGui::SetNextWindowContentSize(ImVec2(600.0f * xsettings.ui_scale, 0.0f));
		// Remove window X padding for this window to easily center stuff
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ImGui::GetStyle().WindowPadding.y));
		if (!ImGui::Begin("Gamepad Settings", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			ImGui::PopStyleVar();
			return;
		}

		static int active = 0;

		// Output dimensions of texture
		const float t_w = 512, t_h = 512;
		// Dimensions of (port+label)s
		const float b_x = 0, b_x_stride = 100, b_y = 400;
		const float b_w = 68, b_h = 81;
		// Dimensions of controller (rendered at origin)
		const float controller_width  = 477.0f;
		const float controller_height = 395.0f;

		// Setup rendering to fbo for controller and port images
		ImTextureID id = (ImTextureID)(intptr_t)render_to_fbo(controller_fbo);

		//
		// Render buttons with icons of the Xbox style port sockets with
		// circular numbers above them. These buttons can be activated to
		// configure the associated port, like a tabbed interface.
		//
		ImVec4 color_active(0.50, 0.86, 0.54, 0.12);
		ImVec4 color_inactive(0, 0, 0, 0);

		// Begin a 4-column layout to render the ports
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12));
		ImGui::Columns(4, "mixed", false);

		const int port_padding = 8;
		for (int i = 0; i < 4; i++)
		{
			bool is_currently_selected = (i == active);
			bool port_is_bound         = (xemu_input_get_bound(i) != nullptr);

			// Set an X offset to center the image button within the column
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (int)((ImGui::GetColumnWidth() - b_w * xsettings.ui_scale - 2 * port_padding * xsettings.ui_scale) / 2));

			// We are using the same texture for all buttons, but ImageButton
			// uses the texture as a unique ID. Push a new ID now to resolve
			// the conflict.
			ImGui::PushID(i);
			float x = b_x + i * b_x_stride;
			ImGui::PushStyleColor(ImGuiCol_Button, is_currently_selected ? color_active : color_inactive);
			bool activated = ImGui::ImageButton(id, ImVec2(b_w * xsettings.ui_scale, b_h * xsettings.ui_scale), ImVec2(x / t_w, (b_y + b_h) / t_h), ImVec2((x + b_w) / t_w, b_y / t_h), port_padding);
			ImGui::PopStyleColor();

			if (activated) active = i;

			uint32_t port_color = 0xafafafff;
			bool     is_hovered = ImGui::IsItemHovered();
			if (is_currently_selected || port_is_bound)
				port_color = 0x81dc8a00;
			else if (is_hovered)
				port_color = 0x000000ff;

			render_controller_port(x, b_y, i, port_color);

			ImGui::PopID();
			ImGui::NextColumn();
		}
		ImGui::PopStyleVar(); // ItemSpacing
		ImGui::Columns(1);

		//
		// Render input device combo
		//
		const float windowWidth = ImGui::GetWindowWidth();
		const float cwidth      = controller_width * xsettings.ui_scale;
		const float cheight     = controller_height * xsettings.ui_scale;

		ImGui::SetCursorPosX(20.0f);
		ImGui::SetNextItemWidth(cwidth * 0.6);

		// List available input devices
		const char*      not_connected = "Not Connected";
		ControllerState* bound_state   = xemu_input_get_bound(active);

		// Get current controller name
		const char* name = bound_state ? bound_state->name : not_connected;

		if (ImGui::BeginCombo("Input Devices", name))
		{
			// Handle "Not connected"
			bool is_selected = false;
			bound_state      = nullptr;
			if (ImGui::Selectable(not_connected, is_selected))
			{
				xemu_input_bind(active, nullptr, 1);
				bound_state = nullptr;
			}
			if (is_selected)
				ImGui::SetItemDefaultFocus();

			// Handle all available input devices
			ControllerState* iter;
			QTAILQ_FOREACH(iter, &available_controllers, entry)
			{
				is_selected = bound_state == iter;
				ImGui::PushID(iter);
				const char* selectable_label = iter->name;
				char        buf[128];
				if (iter->bound >= 0)
				{
					snprintf(buf, sizeof(buf), "%s (Port %d)", iter->name, active + 1);
					selectable_label = buf;
				}
				if (ImGui::Selectable(selectable_label, is_selected))
				{
					xemu_input_bind(active, iter, 1);
					bound_state = iter;
				}
				if (is_selected)
					ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		// reset mapping
		if (bound_state)
		{
			ImGui::SameLine();
			const char* text = "Reset Mapping";
			ImVec2      dim  = ImGui::CalcTextSize(text);
			ImGui::SetCursorPosX(windowWidth - dim.x - 40.0f);
			if (ImGui::Button(text))
			{
				if (bound_state->type == INPUT_DEVICE_SDL_KEYBOARD)
				{
					strcpy(bound_state->key_smapping, "");
					ParseMappingString(bound_state->key_smapping, bound_state->key_mapping, DEFAULT_KEYB_MAPPING);
					strcpy(xsettings.input_keyb[bound_state->bound], bound_state->key_smapping);
				}
				else
				{
					strcpy(bound_state->pad_smapping, "");
					ParseMappingString(bound_state->pad_smapping, bound_state->pad_mapping, DEFAULT_PAD_MAPPING);
					strcpy(xsettings.input_pad[bound_state->bound], bound_state->pad_smapping);
				}
				xsettingsSave();
			}
		}

		ImGui::Columns(1);

		//
		// Add a separator between input selection and controller graphic
		//
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		//
		// Render controller image
		//
		static int prev_inputs[32] = { 0 };
		static int frame           = 0;

		bool device_selected = false;
		if (bound_state)
		{
			device_selected = true;
			render_controller(0, 0, 0x81dc8a00, 0x0f0f0f00, bound_state);
		}
		else
		{
			static ControllerState state = { 0 };
			render_controller(0, 0, 0x1f1f1f00, 0x0f0f0f00, &state);
		}

		ImVec2 cur  = ImGui::GetCursorPos();
		float  curx = cur.x + (int)((ImGui::GetColumnWidth() - cwidth) / 2.0);
		ImGui::SetCursorPosX(curx);
		ImGui::Image(id, ImVec2(cwidth, cheight), ImVec2(0, controller_height / t_h), ImVec2(controller_width / t_w, 0));

		if (!device_selected)
		{
			const char* msg = "Please select an available input device";
			ImVec2      dim = ImGui::CalcTextSize(msg);
			ImGui::SetCursorPosX((windowWidth - dim.x) / 2);
			ImGui::SetCursorPosY(cur.y + (cheight - dim.y) / 2);
			ImGui::Text("%s", msg);
		}
		else
		{
			// check if a button has been pressed
			int* raw_inputs = bound_state->raw_inputs;
			int  button_id  = -1;
			for (int i = 0; i < 21; ++i)
			{
				if (!prev_inputs[i] && raw_inputs[i])
				{
					button_id   = i;
					last_button = i;
					break;
				}
			}
			if (button_id == -1)
			{
				for (int i = 0; i < 6; ++i)
				{
					if (abs(prev_inputs[i + 22]) < 4000 && abs(raw_inputs[i + 22]) >= 4000)
					{
						button_id   = i + 32;
						last_button = i + 32;
						break;
					}
				}
			}

			// confirm mapping change?
			int* mapping = (bound_state->type == INPUT_DEVICE_SDL_KEYBOARD) ? bound_state->key_mapping : bound_state->pad_mapping;

			if (selectedInput)
			{
				if (selectedType == INPUT_DEVICE_SDL_KEYBOARD)
				{
					if (last_key >= 0)
					{
						mapping[selectedInput->id] = last_key;
						StringifyMapping(bound_state->key_mapping, bound_state->key_smapping, DEFAULT_KEYB_MAPPING);
						fprintf(stderr, "keyboard update: selected=%d last_key=%d : %s\n", selectedInput->id, last_key, bound_state->key_smapping);
						strcpy(xsettings.input_keyb[bound_state->bound], bound_state->key_smapping);
						selectedInput = nullptr;
						selectedKey   = last_key;
					}
				}
				else
				{
					if (last_button >= 0)
					{
						mapping[selectedInput->id] = last_button;
						StringifyMapping(bound_state->pad_mapping, bound_state->pad_smapping, DEFAULT_PAD_MAPPING);
						fprintf(stderr, "pad update: selected=%d last_button=%d : %s\n", selectedInput->id, last_button, bound_state->pad_smapping);
						strcpy(xsettings.input_pad[bound_state->bound], bound_state->pad_smapping);
						selectedInput  = nullptr;
						selectedButton = last_button;
					}
				}

				if (!selectedInput)
					xsettingsSave();
			}

			// show the mapping
			for (int i = 0, len = sizeof(ab_buttons) / sizeof(ab_buttons[0]); i < len; ++i)
			{
				const AxisButtonPos& button = ab_buttons[i];

				// get button name
				const char* inputName = 0;
				int         key       = mapping ? mapping[button.id] : -1;
				if (key >= 0)
				{
					if (bound_state->type == INPUT_DEVICE_SDL_KEYBOARD)
						inputName = SDL_GetScancodeName((SDL_Scancode)key);
					else
					{
						if (key >= 32) inputName = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)(key - 32));
						else inputName = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)key);
					}
				}
				char text[32];
				if (!inputName)
				{
					sprintf(text, "%d/%d", button.id, key);
					inputName = text;
				}

				// display button
				float  x   = curx + button.x * cwidth / t_w;
				float  y   = cur.y + (button.y) * cheight / t_h;
				ImVec2 dim = ImGui::CalcTextSize(inputName);

				if (button.align & 2) x -= dim.x;
				else if (!(button.align & 1)) x -= dim.x / 2;

				ImGui::SetCursorPosX(x);
				ImGui::SetCursorPosY(y - dim.y / 2);

				// select-deselect + blink every 8 frames
				ImGui::PushID(i);
				const bool selected = (selectedInput == &button);
				bool       blink    = (selected && !((frame >> 3) & 1));
				ImVec4     color(0.0f, 0.25f, 0.5f, 1.0f);
				if (!blink && key >= 0)
				{
					if (button.type == 2)
					{
						int delta = abs(bound_state->axis[button.value]);
						if (delta > 1200)
						{
							blink       = true;
							float ratio = delta / 32768.0f;
							color.x     = color.x * ratio + 0.36f * (1 - ratio);
							color.y     = color.y * ratio + 0.36f * (1 - ratio);
							color.z     = color.z * ratio + 0.36f * (1 - ratio);
						}
					}
					else blink = !!(bound_state->buttons & button.value);
				}
				if (blink)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, color);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
				}
				if (ImGui::Button(inputName))
				{
					if (selected)
						selectedInput = nullptr;
					else
					{
						selectedInput = (AxisButtonPos*)&button;
						selectedType  = bound_state->type;
						last_button   = -1;
						last_key      = -1;
					}
				}
				if (blink)
				{
					ImGui::PopStyleColor();
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}

			memcpy(prev_inputs, raw_inputs, sizeof(prev_inputs));
		}

		ImGui::End();
		ImGui::PopStyleVar(); // Window padding
		++frame;

		// Restore original framebuffer target
		render_to_default_fb();
	}
};

#define MAX_STRING_LEN \
	2048 // FIXME: Completely arbitrary and only used here
	     // to give a buffer to ImGui for each field

static InputWindow input_window;
static ui::GamesWindow games_window;

class AboutWindow
{
public:
	bool is_open;

private:
	char build_info_text[256];

public:
	AboutWindow()
	{
		snprintf(
		    build_info_text, sizeof(build_info_text),
		    "Version: %s\n"
		    "Branch:  %s\n"
		    "Commit:  %s\n"
		    "Date:    %s",
		    xemu_version, xemu_branch, xemu_commit, xemu_date);
		// FIXME: Show platform
		// FIXME: Show driver
		// FIXME: Show BIOS/BootROM hash
	}

	~AboutWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		ImGui::SetNextWindowContentSize(ImVec2(400.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("About", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		static uint32_t time_start = 0;
		if (ImGui::IsWindowAppearing())
			time_start = SDL_GetTicks();

		uint32_t now = SDL_GetTicks() - time_start;

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 50 * xsettings.ui_scale);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 256 * xsettings.ui_scale) / 2);

		ImTextureID id    = (ImTextureID)(intptr_t)render_to_fbo(logo_fbo);
		float       t_w   = 256.0;
		float       t_h   = 256.0;
		float       x_off = 0;
		ImGui::Image(id, ImVec2((t_w - x_off) * xsettings.ui_scale, t_h * xsettings.ui_scale), ImVec2(x_off / t_w, t_h / t_h), ImVec2(t_w / t_w, 0));
		if (ImGui::IsItemClicked()) time_start = SDL_GetTicks();

		render_logo(now, 0x42e335ff, 0x42e335ff, 0x00000000);
		render_to_default_fb();
		ImGui::SetCursorPosX(10 * xsettings.ui_scale);

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 100 * xsettings.ui_scale);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(xemu_version).x) / 2);
		ImGui::Text("%s", xemu_version);

		ImGui::SetCursorPosX(10 * xsettings.ui_scale);
		ImGui::Dummy(ImVec2(0, 20 * xsettings.ui_scale));

		const char* msg = "Visit https://xemu.app for more information";
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(msg).x) / 2);
		Hyperlink(msg, "https://xemu.app");

		ImGui::Dummy(ImVec2(0, 40 * xsettings.ui_scale));

		ImGui::PushFont(g_fixed_width_font);
		ImGui::InputTextMultiline("##build_info", build_info_text, sizeof(build_info_text), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6), ImGuiInputTextFlags_ReadOnly);
		ImGui::PopFont();

		ImGui::End();
	}
};

class NetworkInterface
{
public:
	std::string pcap_name;
	std::string description;
	std::string friendlyname;

	NetworkInterface(pcap_if_t* pcap_desc, char* _friendlyname = nullptr)
	{
		pcap_name   = pcap_desc->name;
		description = pcap_desc->description ?: pcap_desc->name;
		if (_friendlyname)
		{
			char* tmp    = g_strdup_printf("%s (%s)", _friendlyname, description.c_str());
			friendlyname = tmp;
			g_free((gpointer)tmp);
		}
		else friendlyname = description;
	}
};

class NetworkInterfaceManager
{
public:
	std::vector<std::unique_ptr<NetworkInterface>> ifaces;

	NetworkInterface* current_iface;
	const char*       current_iface_name;
	bool              failed_to_load_lib;

	NetworkInterfaceManager()
	{
		current_iface      = nullptr;
		current_iface_name = xsettings.net_pcap_iface;
		failed_to_load_lib = false;
	}

	void refresh()
	{
		pcap_if_t *alldevs, *iter;
		char       err[PCAP_ERRBUF_SIZE];

		if (xemu_net_is_enabled())
			return;

#if defined(_WIN32)
		if (pcap_load_library())
		{
			failed_to_load_lib = true;
			return;
		}
#endif

		ifaces.clear();
		current_iface = nullptr;

		if (pcap_findalldevs(&alldevs, err))
			return;

		for (iter = alldevs; iter != nullptr; iter = iter->next)
		{
#if defined(_WIN32)
			char* friendlyname = get_windows_interface_friendly_name(iter->name);
			ifaces.emplace_back(new NetworkInterface(iter, friendlyname));
			if (friendlyname) g_free((gpointer)friendlyname);
#else
			ifaces.emplace_back(new NetworkInterface(iter));
#endif
			if (!strcmp(current_iface_name, iter->name)) current_iface = ifaces.back().get();
		}

		pcap_freealldevs(alldevs);
	}

	void select(NetworkInterface& iface)
	{
		current_iface = &iface;
		strcpy(xsettings.net_pcap_iface, iface.pcap_name.c_str());
		current_iface_name = xsettings.net_pcap_iface;
	}

	bool is_current(NetworkInterface& iface) { return &iface == current_iface; }
};

class NetworkWindow
{
public:
	bool is_open = false;
	int  backend;
	char remote_addr[64];
	char local_addr[64];

	std::unique_ptr<NetworkInterfaceManager> iface_mgr;

	NetworkWindow() {}
	~NetworkWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		ImGui::SetNextWindowContentSize(ImVec2(500.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Network", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (ImGui::IsWindowAppearing())
		{
			strncpy(remote_addr, xsettings.net_remote_addr, sizeof(remote_addr) - 1);
			strncpy(local_addr, xsettings.net_local_addr, sizeof(local_addr) - 1);
			backend = xsettings.net_backend;
		}

		ImGuiInputTextFlags flg        = 0;
		bool                is_enabled = xemu_net_is_enabled();
		if (is_enabled)
			flg |= ImGuiInputTextFlags_ReadOnly;

		ImGui::Columns(2, "", false);
		ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.33);

		ImGui::Text("Attached To");
		ImGui::SameLine();
		HelpMarker("The network backend which the emulated NIC interacts with");
		ImGui::NextColumn();
		if (is_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
		int         temp_backend   = backend; // Temporary to make backend combo read-only (FIXME: surely there's a nicer way)
		const char* sNetBackends[] = { "NAT", "UDP Tunnel", "Bridged Adapter" };
		if (ImGui::Combo("##backend", is_enabled ? &temp_backend : &backend, sNetBackends, 3) && !is_enabled)
		{
			xsettings.net_backend = backend;
			xsettingsSave();
		}
		if (is_enabled) ImGui::PopStyleVar();
		ImGui::SameLine();
		if (backend == NET_BACKEND_USER) HelpMarker("User-mode TCP/IP stack with network address translation");
		else if (backend == NET_BACKEND_SOCKET_UDP) HelpMarker("Tunnels link-layer traffic to a remote host via UDP");
		else if (backend == NET_BACKEND_PCAP) HelpMarker("Bridges with a host network interface");
		ImGui::NextColumn();

		if (backend == NET_BACKEND_SOCKET_UDP)
		{
			ImGui::Text("Remote Host");
			ImGui::SameLine();
			HelpMarker("The remote <IP address>:<Port> to forward packets to (e.g. 1.2.3.4:9368)");
			ImGui::NextColumn();
			float w = ImGui::GetColumnWidth() - 10 * xsettings.ui_scale;
			ImGui::SetNextItemWidth(w);
			if (is_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
			ImGui::InputText("###remote_host", remote_addr, sizeof(remote_addr), flg);
			if (is_enabled) ImGui::PopStyleVar();
			ImGui::NextColumn();

			ImGui::Text("Local Host");
			ImGui::SameLine();
			HelpMarker("The local <IP address>:<Port> to receive packets on (e.g. 0.0.0.0:9368)");
			ImGui::NextColumn();
			ImGui::SetNextItemWidth(w);
			if (is_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
			ImGui::InputText("###local_host", local_addr, sizeof(local_addr), flg);
			if (is_enabled) ImGui::PopStyleVar();
			ImGui::NextColumn();
		}
		else if (backend == NET_BACKEND_PCAP)
		{
			static bool should_refresh = true;
			if (iface_mgr.get() == nullptr)
			{
				iface_mgr.reset(new NetworkInterfaceManager());
				iface_mgr->refresh();
			}

			if (iface_mgr->failed_to_load_lib)
			{
#if defined(_WIN32)
				ImGui::Columns(1);
				ImGui::Dummy(ImVec2(0, 20 * xsettings.ui_scale));
				const char* msg = "WinPcap/npcap library could not be loaded.\nTo use this attachment, please install npcap.";
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetColumnWidth() - xsettings.ui_scale * ImGui::CalcTextSize(msg).x) / 2);
				ImGui::Text("%s", msg);
				ImGui::Dummy(ImVec2(0, 10 * xsettings.ui_scale));
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120 * xsettings.ui_scale) / 2);
				if (ImGui::Button("Install npcap", ImVec2(120 * xsettings.ui_scale, 0)))
					xemu_open_web_browser("https://nmap.org/npcap/");
				ImGui::Dummy(ImVec2(0, 10 * xsettings.ui_scale));
#endif
			}
			else
			{
				ImGui::Text("Network Interface");
				ImGui::SameLine();
				HelpMarker("Host network interface to bridge with");
				ImGui::NextColumn();

				float w = ImGui::GetColumnWidth() - 10 * xsettings.ui_scale;
				ImGui::SetNextItemWidth(w);
				const char* selected_display_name = (iface_mgr->current_iface ? iface_mgr->current_iface->friendlyname.c_str() : iface_mgr->current_iface_name);

				if (is_enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
				if (ImGui::BeginCombo("###network_iface", selected_display_name))
				{
					if (should_refresh)
					{
						iface_mgr->refresh();
						should_refresh = false;
					}
					int i = 0;
					for (auto& iface : iface_mgr->ifaces)
					{
						bool is_selected = iface_mgr->is_current((*iface));
						ImGui::PushID(i++);
						if (ImGui::Selectable(iface->friendlyname.c_str(), is_selected))
						{
							if (!is_enabled)
								iface_mgr->select((*iface));
						}
						if (is_selected)
							ImGui::SetItemDefaultFocus();
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				else
					should_refresh = true;

				if (is_enabled) ImGui::PopStyleVar();

				ImGui::NextColumn();
			}
		}

		ImGui::Columns(1);

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		Hyperlink("Help", "https://xemu.app/docs/networking/");

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (120 + 10) * xsettings.ui_scale);
		ImGui::SetItemDefaultFocus();
		if (ImGui::Button(is_enabled ? "Disable" : "Enable", ImVec2(120 * xsettings.ui_scale, 0)))
		{
			if (!is_enabled)
			{
				strcpy(xsettings.net_remote_addr, remote_addr);
				strcpy(xsettings.net_local_addr, local_addr);
				xemu_net_enable();
			}
			else
				xemu_net_disable();

			xsettings.net_enabled = xemu_net_is_enabled();
			xsettingsSave();
		}

		ImGui::End();
	}
};

#ifdef CONFIG_CPUID_H
#	include <cpuid.h>
#endif

const char* get_cpu_info()
{
	const char* cpu_info = "";
#ifdef CONFIG_CPUID_H
	static uint32_t brand[12];
	if (__get_cpuid_max(0x80000004, nullptr))
	{
		__get_cpuid(0x80000002, brand + 0x0, brand + 0x1, brand + 0x2, brand + 0x3);
		__get_cpuid(0x80000003, brand + 0x4, brand + 0x5, brand + 0x6, brand + 0x7);
		__get_cpuid(0x80000004, brand + 0x8, brand + 0x9, brand + 0xa, brand + 0xb);
	}
	cpu_info = (const char*)brand;
#endif
	// FIXME: Support other architectures (e.g. ARM)
	return cpu_info;
}

class CompatibilityReporter
{
public:
	bool is_open = false;

	CompatibilityReport report;
	bool                dirty;
	bool                is_xbe_identified;
	bool                did_send, send_result;
	char                token_buf[512];
	int                 playability;
	char                description[1024];
	std::string         serialized_report;

	CompatibilityReporter()
	{
		report.token        = "";
		report.xemu_version = xemu_version;
		report.xemu_branch  = xemu_branch;
		report.xemu_commit  = xemu_commit;
		report.xemu_date    = xemu_date;
#if defined(__linux__)
		report.os_platform = "Linux";
#elif defined(_WIN32)
		report.os_platform = "Windows";
#elif defined(__APPLE__)
		report.os_platform = "macOS";
#else
		report.os_platform = "Unknown";
#endif
		report.os_version = xemu_get_os_info();
		report.cpu        = get_cpu_info();
		dirty             = true;
		is_xbe_identified = false;
		did_send = send_result = false;
	}

	~CompatibilityReporter() {}

	void Draw()
	{
		if (!is_open)
			return;

		const char* playability_names[] = { "Broken", "Intro", "Starts", "Playable", "Perfect" };

		const char* playability_descriptions[] = {
			"This title crashes very soon after launching, or displays nothing at all.",
			"This title displays an intro sequence, but fails to make it to gameplay.",
			"This title starts, but may crash or have significant issues.",
			"This title is playable, but may have minor issues.",
			"This title is playable from start to finish with no noticable issues.",
		};

		ImGui::SetNextWindowContentSize(ImVec2(550.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Report Compatibility", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (ImGui::IsWindowAppearing())
		{
			report.gl_vendor                   = (const char*)glGetString(GL_VENDOR);
			report.gl_renderer                 = (const char*)glGetString(GL_RENDERER);
			report.gl_version                  = (const char*)glGetString(GL_VERSION);
			report.gl_shading_language_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
			struct xbe* xbe                    = xemu_get_xbe_info();
			is_xbe_identified                  = xbe != nullptr;
			if (is_xbe_identified) report.SetXbeData(xbe);
			did_send = send_result = false;

			playability            = 3; // Playable
			report.compat_rating   = playability_names[playability];
			description[0]         = '\x00';
			report.compat_comments = description;

			strncpy(token_buf, xsettings.user_token, sizeof(token_buf));
			report.token = token_buf;

			dirty = true;
		}

		if (!is_xbe_identified)
		{
			ImGui::TextWrapped("An XBE could not be identified.\nPlease launch an official Xbox title to submit a compatibility report.");
			ImGui::End();
			return;
		}

		ImGui::TextWrapped(
		    "If you would like to help improve xemu by submitting a compatibility report for this "
		    "title, please select an appropriate playability level, enter a brief description, then click 'Send'."
		    "\n\n"
		    "Note: By submitting a report, you acknowledge and consent to "
		    "collection, archival, and publication of information as outlined "
		    "in 'Privacy Disclosure' below.");

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		ImGui::Columns(2, "", false);
		ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.25);

		ImGui::Text("User Token");
		ImGui::SameLine();
		HelpMarker("This is a unique access token used to authorize submission of the report. To request a token, click 'Get Token'.");
		ImGui::NextColumn();
		float item_width = ImGui::GetColumnWidth() * 0.75 - 20 * xsettings.ui_scale;
		ImGui::SetNextItemWidth(item_width);
		ImGui::PushFont(g_fixed_width_font);
		if (ImGui::InputText("###UserToken", token_buf, sizeof(token_buf), 0))
		{
			report.token = token_buf;
			dirty        = true;
		}
		ImGui::PopFont();
		ImGui::SameLine();
		if (ImGui::Button("Get Token"))
			xemu_open_web_browser("https://reports.xemu.app");
		ImGui::NextColumn();

		ImGui::Text("Playability");
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(item_width);
		const char* sPlayabilities[] = { "Broken", "Intro/Menus", "Starts", "Playable", "Perfect" };
		if (ImGui::Combo("###PlayabilityRating", &playability, sPlayabilities, 5))
		{
			report.compat_rating = playability_names[playability];
			dirty                = true;
		}
		ImGui::SameLine();
		HelpMarker(playability_descriptions[playability]);
		ImGui::NextColumn();

		ImGui::Columns(1);

		ImGui::Text("Description");
		if (ImGui::InputTextMultiline("###desc", description, sizeof(description), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 6), 0))
		{
			report.compat_comments = description;
			dirty                  = true;
		}

		if (ImGui::TreeNode("Report Details"))
		{
			ImGui::PushFont(g_fixed_width_font);
			if (dirty)
			{
				serialized_report = report.GetSerializedReport();
				dirty             = false;
			}
			ImGui::InputTextMultiline("##build_info", (char*)serialized_report.c_str(), strlen(serialized_report.c_str()) + 1, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 7), ImGuiInputTextFlags_ReadOnly);
			ImGui::PopFont();
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Privacy Disclosure (Please read before submission!)"))
		{
			ImGui::TextWrapped(
			    "By volunteering to submit a compatibility report, basic information about your "
			    "computer is collected, including: your operating system version, CPU model, "
			    "graphics card/driver information, and details about the title which are "
			    "extracted from the executable in memory. The contents of this report can be "
			    "seen before submission by expanding 'Report Details'."
			    "\n\n"
			    "Like many websites, upon submission, the public IP address of your computer is "
			    "also recorded with your report. If provided, the identity associated with your "
			    "token is also recorded."
			    "\n\n"
			    "This information will be archived and used to analyze, resolve problems with, "
			    "and improve the application. This information may be made publicly visible, "
			    "for example: to anyone who wishes to see the playability status of a title, as "
			    "indicated by your report.");
			ImGui::TreePop();
		}

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		if (did_send)
		{
			if (send_result)
				ImGui::Text("Sent! Thanks.");
			else
				ImGui::Text("Error: %s (%d)", report.GetResultMessage().c_str(), report.GetResultCode());
			ImGui::SameLine();
		}

		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (120 + 10) * xsettings.ui_scale);

		ImGui::SetItemDefaultFocus();
		if (ImGui::Button("Send", ImVec2(120 * xsettings.ui_scale, 0)))
		{
			did_send    = true;
			send_result = report.Send();
			if (send_result)
			{
				// Close window on success
				is_open = false;

				// Save user token if it was used
				strcpy(xsettings.user_token, token_buf);
				xsettingsSave();
			}
		}

		ImGui::End();
	}
};

#include <math.h>

float mix(float a, float b, float t)
{
	return a * (1.0 - t) + (b - a) * t;
}

class DebugApuWindow
{
public:
	bool is_open = false;

	DebugApuWindow() {}
	~DebugApuWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		ImGui::SetNextWindowContentSize(ImVec2(600.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Audio Debug", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		const struct McpxApuDebug* dbg = mcpx_apu_get_debug_info();

		ImGui::Columns(2, "", false);
		int   now        = SDL_GetTicks() % 1000;
		float t          = now / 1000.0f;
		float freq       = 1;
		float v          = fabs(sin(M_PI * t * freq));
		float c_active   = mix(0.4, 0.97, v);
		float c_inactive = 0.2f;

		int voice_monitor = -1;
		int voice_info    = -1;
		int voice_mute    = -1;

		// Color buttons, demonstrate using PushID() to add unique identifier in the ID stack, and changing style.
		ImGui::PushFont(g_fixed_width_font);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
		for (int i = 0; i < 256; i++)
		{
			if (i % 16)
				ImGui::SameLine();

			float c, s, h;
			h = 0.6;
			if (dbg->vp.v[i].active)
			{
				if (dbg->vp.v[i].paused)
				{
					c = c_inactive;
					s = 0.4;
				}
				else
				{
					c = c_active;
					s = 0.7;
				}
				if (mcpx_apu_debug_is_muted(i))
					h = 1.0;
			}
			else
			{
				c = c_inactive;
				s = 0;
			}

			ImGui::PushID(i);
			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(h, s, c));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(h, s, 0.8));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(h, 0.8f, 1.0));
			char buf[12];
			snprintf(buf, sizeof(buf), "%02x", i);
			ImGui::Button(buf);
			if (/*dbg->vp.v[i].active &&*/ ImGui::IsItemHovered())
			{
				voice_monitor = i;
				voice_info    = i;
			}
			if (ImGui::IsItemClicked(1))
				voice_mute = i;
			ImGui::PopStyleColor(3);
			ImGui::PopID();
		}
		ImGui::PopStyleVar(3);
		ImGui::PopFont();

		if (voice_info >= 0)
		{
			const struct McpxApuDebugVoice* voice = &dbg->vp.v[voice_info];
			ImGui::BeginTooltip();
			bool is_paused = voice->paused;
			ImGui::Text("Voice 0x%x/%d %s", voice_info, voice_info, is_paused ? "(Paused)" : "");
			ImGui::SameLine();
			ImGui::Text(voice->stereo ? "Stereo" : "Mono");

			ImGui::Separator();
			ImGui::PushFont(g_fixed_width_font);

			const char* noyes[2] = { "NO", "YES" };
			ImGui::Text(
			    "Stream: %-3s Loop: %-3s Persist: %-3s Multipass: %-3s Linked: %-3s",
			    noyes[voice->stream], noyes[voice->loop], noyes[voice->persist], noyes[voice->multipass], noyes[voice->linked]);

			const char* cs[4] = { "1 byte", "2 bytes", "ADPCM", "4 bytes" };
			const char* ss[4] = { "Unsigned 8b PCM", "Signed 16b PCM", "Signed 24b PCM", "Signed 32b PCM" };

			assert(voice->container_size < 4);
			assert(voice->sample_size < 4);
			ImGui::Text("Container Size: %s, Sample Size: %s, Samples per Block: %d", cs[voice->container_size], ss[voice->sample_size], voice->samples_per_block);
			ImGui::Text("Rate: %f (%d Hz)", voice->rate, (int)(48000.0 / voice->rate));
			ImGui::Text("EBO=%d CBO=%d LBO=%d BA=%x", voice->ebo, voice->cbo, voice->lbo, voice->ba);
			ImGui::Text("Mix: ");
			for (int i = 0; i < 8; i++)
			{
				if (i == 4)
					ImGui::Text("     ");
				ImGui::SameLine();
				char buf[64];
				if (voice->vol[i] == 0xFFF)
					snprintf(buf, sizeof(buf), "Bin %2d (MUTE) ", voice->bin[i]);
				else
					snprintf(buf, sizeof(buf), "Bin %2d (-%.3f) ", voice->bin[i], (float)((voice->vol[i] >> 6) & 0x3f) + (float)((voice->vol[i] >> 0) & 0x3f) / 64.0);

				ImGui::Text("%-17s", buf);
			}
			ImGui::PopFont();
			ImGui::EndTooltip();
		}

		if (voice_monitor >= 0)
			mcpx_apu_debug_isolate_voice(voice_monitor);
		else
			mcpx_apu_debug_clear_isolations();

		if (voice_mute >= 0)
			mcpx_apu_debug_toggle_mute(voice_mute);

		ImGui::SameLine();
		ImGui::SetColumnWidth(0, ImGui::GetCursorPosX());
		ImGui::NextColumn();

		ImGui::PushFont(g_fixed_width_font);
		ImGui::Text("Frames:      %04d", dbg->frames_processed);
		ImGui::Text("GP Cycles:   %04d", dbg->gp.cycles);
		ImGui::Text("EP Cycles:   %04d", dbg->ep.cycles);
		bool color = (dbg->utilization > 0.9);
		if (color)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
		ImGui::Text("Utilization: %.2f%%", (dbg->utilization * 100));
		if (color)
			ImGui::PopStyleColor();
		ImGui::PopFont();

		ImGui::Separator();

		static int  mon         = 0;
		const char* sMonitors[] = { "AC97", "VP Only", "GP Only", "EP Only", "GP/EP if enabled" };
		mon                     = mcpx_apu_debug_get_monitor();
		if (ImGui::Combo("Monitor", &mon, sMonitors, 5))
			mcpx_apu_debug_set_monitor(mon);

		static bool gp_realtime;
		gp_realtime = dbg->gp_realtime;
		if (ImGui::Checkbox("GP Realtime\n", &gp_realtime))
			mcpx_apu_debug_set_gp_realtime_enabled(gp_realtime);

		static bool ep_realtime;
		ep_realtime = dbg->ep_realtime;
		if (ImGui::Checkbox("EP Realtime\n", &ep_realtime))
			mcpx_apu_debug_set_ep_realtime_enabled(ep_realtime);

		ImGui::Columns(1);
		ImGui::End();
	}
};

// utility structure for realtime plot
struct ScrollingBuffer
{
	int              MaxSize;
	int              Offset;
	ImVector<ImVec2> Data;
	ScrollingBuffer()
	{
		MaxSize = 2000;
		Offset  = 0;
		Data.reserve(MaxSize);
	}
	void AddPoint(float x, float y)
	{
		if (Data.size() < MaxSize)
			Data.push_back(ImVec2(x, y));
		else
		{
			Data[Offset] = ImVec2(x, y);
			Offset       = (Offset + 1) % MaxSize;
		}
	}
	void Erase()
	{
		if (Data.size() > 0)
		{
			Data.shrink(0);
			Offset = 0;
		}
	}
};

class DebugVideoWindow
{
public:
	bool is_open;
	bool transparent;

	DebugVideoWindow()
	{
		is_open     = false;
		transparent = false;
	}

	~DebugVideoWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		float alpha = transparent ? 0.2 : 1.0;
		PushWindowTransparencySettings(transparent, 0.2);
		ImGui::SetNextWindowSize(ImVec2(600.0f * xsettings.ui_scale, 150.0f * xsettings.ui_scale), ImGuiCond_Once);
		if (ImGui::Begin("Video Debug", &is_open))
		{
			double      x_start, x_end;
			static auto rt_axis = ImPlotAxisFlags_NoTickLabels;
			ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(5, 5));
			ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
			static ScrollingBuffer fps;
			static float           t = 0;
			if (runstate_is_running())
			{
				t += ImGui::GetIO().DeltaTime;
				fps.AddPoint(t, g_nv2a_stats.increment_fps);
			}
			x_start = t - 10.0;
			x_end   = t;

			float plot_width = 0.5 * (ImGui::GetWindowSize().x - 2 * ImGui::GetStyle().WindowPadding.x - ImGui::GetStyle().ItemSpacing.x);

			ImGui::SetNextWindowBgAlpha(alpha);
			if (ImPlot::BeginPlot("##ScrollingFPS", nullptr, nullptr, ImVec2(plot_width, 75 * xsettings.ui_scale), 0, rt_axis, rt_axis | ImPlotAxisFlags_Lock))
			{
				ImPlot::SetupAxesLimits(x_start, x_end, 0, 65, ImGuiCond_Always);
				if (fps.Data.size() > 0)
				{
					ImPlot::PlotShaded("##fps", &fps.Data[0].x, &fps.Data[0].y, fps.Data.size(), 0, fps.Offset, 2 * sizeof(float));
					ImPlot::PlotLine("##fps", &fps.Data[0].x, &fps.Data[0].y, fps.Data.size(), fps.Offset, 2 * sizeof(float));
				}
				ImPlot::Annotation(x_start, 65, ImPlot::GetLastItemColor(), ImVec2(0, 0), true, "FPS: %d", g_nv2a_stats.increment_fps);
				ImPlot::EndPlot();
			}

			ImGui::SameLine();

			x_end   = g_nv2a_stats.frame_count;
			x_start = x_end - NV2A_PROF_NUM_FRAMES;

			ImPlot::PushStyleColor(ImPlotCol_Line, ImPlot::GetColormapColor(1));
			ImGui::SetNextWindowBgAlpha(alpha);
			if (ImPlot::BeginPlot("##ScrollingMSPF", nullptr, nullptr, ImVec2(plot_width, 75 * xsettings.ui_scale), 0, rt_axis, rt_axis | ImPlotAxisFlags_Lock))
			{
				ImPlot::SetupAxesLimits(x_start, x_end, 0, 100, ImGuiCond_Always);
				ImPlot::PlotShaded("##mspf", &g_nv2a_stats.frame_history[0].mspf, NV2A_PROF_NUM_FRAMES, 0, 1, x_start, g_nv2a_stats.frame_ptr, sizeof(g_nv2a_stats.frame_working));
				ImPlot::PlotLine("##mspf", &g_nv2a_stats.frame_history[0].mspf, NV2A_PROF_NUM_FRAMES, 1, x_start, g_nv2a_stats.frame_ptr, sizeof(g_nv2a_stats.frame_working));
				ImPlot::Annotation(x_start, 100, ImPlot::GetLastItemColor(), ImVec2(0, 0), true, "MS: %d", g_nv2a_stats.frame_history[(g_nv2a_stats.frame_ptr - 1) % NV2A_PROF_NUM_FRAMES].mspf);
				ImPlot::EndPlot();
			}
			ImPlot::PopStyleColor();

			if (ImGui::TreeNode("Advanced"))
			{
				ImGui::SetNextWindowBgAlpha(alpha);
				if (ImPlot::BeginPlot("##ScrollingDraws", nullptr, nullptr, ImVec2(-1, 500 * xsettings.ui_scale), 0, rt_axis, rt_axis | ImPlotAxisFlags_Lock))
				{
					ImPlot::SetupAxesLimits(x_start, x_end, 0, 1500, ImGuiCond_Always);
					for (int i = 0; i < NV2A_PROF__COUNT; i++)
					{
						ImGui::PushID(i);
						char title[64];
						snprintf(title, sizeof(title), "%s: %d", nv2a_profile_get_counter_name(i), nv2a_profile_get_counter_value(i));
						ImPlot::PushStyleColor(ImPlotCol_Line, ImPlot::GetColormapColor(i));
						ImPlot::PushStyleColor(ImPlotCol_Fill, ImPlot::GetColormapColor(i));
						ImPlot::PlotLine(title, &g_nv2a_stats.frame_history[0].counters[i], NV2A_PROF_NUM_FRAMES, 1, x_start, g_nv2a_stats.frame_ptr, sizeof(g_nv2a_stats.frame_working));
						ImPlot::PopStyleColor(2);
						ImGui::PopID();
					}
					ImPlot::EndPlot();
				}
				ImGui::TreePop();
			}

			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(2))
				transparent = !transparent;

			ImPlot::PopStyleVar(2);
		}
		ImGui::End();
		ImGui::PopStyleColor(5);
	}
};

#if defined(_WIN32)
class AutoUpdateWindow
{
protected:
	Updater updater;

public:
	bool is_open;
	bool should_prompt_auto_update_selection;

	AutoUpdateWindow()
	{
		is_open                             = false;
		should_prompt_auto_update_selection = false;
	}

	~AutoUpdateWindow() {}

	void save_auto_update_selection(bool preference)
	{
		xsettings.check_for_update = int(preference);
		xsettingsSave();
		should_prompt_auto_update_selection = false;
	}

	void prompt_auto_update_selection()
	{
		ImGui::Text("Would you like xemu to check for updates on startup?");
		ImGui::SetNextItemWidth(-1.0f);

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		float w  = (130) * xsettings.ui_scale;
		float bw = w + (10) * xsettings.ui_scale;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw);

		if (ImGui::Button("No", ImVec2(w, 0)))
		{
			save_auto_update_selection(false);
			is_open = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Yes", ImVec2(w, 0)))
		{
			save_auto_update_selection(true);
			check_for_updates_and_prompt_if_available();
		}
	}

	void check_for_updates_and_prompt_if_available()
	{
		updater.check_for_update([this]() { is_open |= updater.is_update_available(); });
	}

	void Draw()
	{
		if (!is_open)
			return;
		ImGui::SetNextWindowContentSize(ImVec2(550.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Update", &is_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		if (should_prompt_auto_update_selection)
		{
			prompt_auto_update_selection();
			ImGui::End();
			return;
		}

		if (ImGui::IsWindowAppearing() && !updater.is_update_available())
			updater.check_for_update();

		const char* status_msg[] = {
			"",
			"An error has occured. Try again.",
			"Checking for update...",
			"Downloading update...",
			"Update successful! Restart to launch updated version of xemu.",
		};
		const char* available_msg[] = {
			"Update availability unknown.",
			"This version of xemu is up to date.",
			"An updated version of xemu is available!",
		};

		if (updater.get_status() == UPDATER_IDLE)
			ImGui::Text("%s", available_msg[updater.get_update_availability()]);
		else
			ImGui::Text("%s", status_msg[updater.get_status()]);

		if (updater.is_updating())
			ImGui::ProgressBar(updater.get_update_progress_percentage() / 100.0f, ImVec2(-1.0f, 0.0f));

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));

		float w  = (130) * xsettings.ui_scale;
		float bw = w + (10) * xsettings.ui_scale;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw);

		if (updater.is_checking_for_update() || updater.is_updating())
		{
			if (ImGui::Button("Cancel", ImVec2(w, 0)))
				updater.cancel();
		}
		else
		{
			if (updater.is_pending_restart())
			{
				if (ImGui::Button("Restart", ImVec2(w, 0)))
					updater.restart_to_updated();
			}
			else if (updater.is_update_available())
			{
				if (ImGui::Button("Update", ImVec2(w, 0)))
					updater.update();
			}
			else
			{
				if (ImGui::Button("Check for Update", ImVec2(w, 0)))
					updater.check_for_update();
			}
		}

		ImGui::End();
	}
};
#endif

static AboutWindow           about_window;
static CompatibilityReporter compatibility_reporter_window;
static DebugApuWindow        apu_window;
static DebugVideoWindow      video_window;
static MonitorWindow         monitor_window;
static NetworkWindow         network_window;
static NotificationManager   notification_manager;
static ui::SettingsWindow    settings_window;
#if defined(_WIN32)
static AutoUpdateWindow update_window;
#endif
static std::deque<const char*> g_errors;

class FirstBootWindow
{
public:
	bool is_open = false;

	FirstBootWindow() {}
	~FirstBootWindow() {}

	void Draw()
	{
		if (!is_open)
			return;

		ImVec2 size(400 * xsettings.ui_scale, 300 * xsettings.ui_scale);
		auto&  io = ImGui::GetIO();

		ImVec2 window_pos = ImVec2((io.DisplaySize.x - size.x) / 2, (io.DisplaySize.y - size.y) / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

		ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
		if (!ImGui::Begin("First Boot", &is_open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration))
		{
			ImGui::End();
			return;
		}

		static uint32_t time_start = 0;
		if (ImGui::IsWindowAppearing())
			time_start = SDL_GetTicks();

		uint32_t now = SDL_GetTicks() - time_start;

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 50 * xsettings.ui_scale);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 256 * xsettings.ui_scale) / 2);

		ImTextureID id    = (ImTextureID)(intptr_t)render_to_fbo(logo_fbo);
		float       t_w   = 256.0;
		float       t_h   = 256.0;
		float       x_off = 0;
		ImGui::Image(id, ImVec2((t_w - x_off) * xsettings.ui_scale, t_h * xsettings.ui_scale), ImVec2(x_off / t_w, t_h / t_h), ImVec2(t_w / t_w, 0));

		if (ImGui::IsItemClicked()) time_start = SDL_GetTicks();

		render_logo(now, 0x42e335ff, 0x42e335ff, 0x00000000);
		render_to_default_fb();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 100 * xsettings.ui_scale);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(xemu_version).x) / 2);
		ImGui::Text("%s", xemu_version);

		ImGui::SetCursorPosX(10 * xsettings.ui_scale);
		ImGui::Dummy(ImVec2(0, 20 * xsettings.ui_scale));

		const char* msg = "To get started, please configure machine settings.";
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(msg).x) / 2);
		ImGui::Text("%s", msg);

		ImGui::Dummy(ImVec2(0, 20 * xsettings.ui_scale));
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120 * xsettings.ui_scale) / 2);
		if (ImGui::Button("Settings", ImVec2(120 * xsettings.ui_scale, 0)))
			settings_window.is_open = true; // FIXME
		ImGui::Dummy(ImVec2(0, 20 * xsettings.ui_scale));

		msg = "Visit https://xemu.app for more information";
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(msg).x) / 2);
		Hyperlink(msg, "https://xemu.app");

		ImGui::End();
	}
};

static bool is_shortcut_key_pressed(int scancode, bool isAlt)
{
	auto&      io     = ImGui::GetIO();
	const bool is_osx = io.ConfigMacOSXBehaviors;
	// OS X style: Shortcuts using Cmd/Super instead of Ctrl
	const bool is_shortcut_key = isAlt ? io.KeyAlt : ((is_osx ? (io.KeySuper && !io.KeyCtrl) : (io.KeyCtrl && !io.KeySuper)) && !io.KeyAlt && !io.KeyShift);
	return is_shortcut_key && io.KeysDown[scancode] && (io.KeysDownDuration[scancode] == 0.0);
}

static void action_eject_disc()
{
	strcpy(xsettings.dvd_path, "");
	xsettingsSave();
	xemu_eject_disc();
}

static void action_reset()
{
	qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

static void action_shutdown()
{
    fmt::print(stderr, "action_shutdown\n");
    ui::LoadedGame("");
	qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
}

static bool is_key_pressed(int scancode)
{
	auto& io = ImGui::GetIO();
	return io.KeysDown[scancode] && (io.KeysDownDuration[scancode] == 0.0);
}

static void process_keyboard_shortcuts()
{
	if (is_shortcut_key_pressed(SDL_SCANCODE_E, false)) action_eject_disc();
	if (is_shortcut_key_pressed(SDL_SCANCODE_G, true)) g_user_asked_for_intercept = 1;
	if (is_shortcut_key_pressed(SDL_SCANCODE_H, true)) g_user_asked_for_intercept = 2;
	if (is_shortcut_key_pressed(SDL_SCANCODE_O, false)) ui::LoadDisc();
	if (is_shortcut_key_pressed(SDL_SCANCODE_P, false)) ui::TogglePause();
	if (is_shortcut_key_pressed(SDL_SCANCODE_R, false)) action_reset();

	// if (is_shortcut_key_pressed(SDL_SCANCODE_Q, false))
	// 	action_shutdown();

	if (is_key_pressed(SDL_SCANCODE_GRAVE))
		monitor_window.toggle_open();
}

static bool showImGuiDemo  = false;
static bool showImPlotDemo = false;

static void ShowMainMenu()
{
	bool       running    = runstate_is_running();
	static int dirty_menu = 0;
	int        update     = 0;

	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Eject Disc", xsettings.shortcut_eject)) action_eject_disc();
			if (ImGui::MenuItem("Boot Disc", xsettings.shortcut_open)) ui::LoadDisc();
			if (ImGui::BeginMenu("Boot Recent"))
			{
				bool first = true;
				for (int i = 0; i < 6; ++i)
				{
					const char* name = xsettings.recent_files[i];
					if (*name)
					{
						if (first)
						{
							if (ImGui::MenuItem("List Clear"))
							{
								memset(xsettings.recent_files, 0, sizeof(xsettings.recent_files));
								break;
							}
							ImGui::Separator();
							first = false;
						}
                        std::filesystem::path path = name;
						if (ImGui::MenuItem(path.filename().string().c_str(), fmt::format("Ctrl+{}", i + 1).c_str()))
							xemu_load_disc(name, true);
					}
				}

				if (first)
					ImGui::MenuItem("Empty List", nullptr, false, false);

				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Scan Folder")) ui::ScanGamesFolder();
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) action_shutdown();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Emulation"))
		{
			if (ImGui::MenuItem(running ? "Pause" : "Run", xsettings.shortcut_pause)) ui::TogglePause();
			if (ImGui::MenuItem("Reset", xsettings.shortcut_reset)) action_reset();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Configuration"))
		{
			if (ImGui::MenuItem("CPU")) OpenConfig(0);
			if (ImGui::MenuItem("GPU", xsettings.shortcut_gpu)) OpenConfig(1);
			if (ImGui::MenuItem("Audio")) OpenConfig(2);
			ImGui::Separator();
			ImGui::MenuItem("Pads", xsettings.shortcut_pads, &input_window.is_open);
			if (ImGui::MenuItem("System")) OpenConfig(4);
			if (ImGui::MenuItem("Network")) OpenConfig(5);
			if (ImGui::MenuItem("Advanced")) OpenConfig(6);
			if (ImGui::MenuItem("Emulator")) OpenConfig(7);
			if (ImGui::MenuItem("GUI")) OpenConfig(8);
			if (ImGui::MenuItem("Debug")) OpenConfig(9);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Controls", xsettings.shortcut_controls, &games_window.is_open);
			ImGui::MenuItem("Game List", xsettings.shortcut_games, &games_window.is_open);
			ImGui::MenuItem("Log", xsettings.shortcut_log, &games_window.is_open);
			ImGui::Separator();
			ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo);
			ImGui::MenuItem("ImPlot Demo", nullptr, &showImPlotDemo);
			if (ImGui::MenuItem("Fullscreen", xsettings.shortcut_fullscreen, xemu_is_fullscreen(), true)) xemu_toggle_fullscreen();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Utilities"))
		{
			ImGui::MenuItem("Monitor", xsettings.shortcut_monitor, &monitor_window.is_open);
			ImGui::MenuItem("Audio", nullptr, &apu_window.is_open);
			ImGui::MenuItem("Video", nullptr, &video_window.is_open);

			ImGui::Separator();
			if (ImGui::MenuItem("Extract ISO")) exiso::DecodeXiso(ui::FileOpenISO(""));
			if (ImGui::MenuItem("Create ISO")) exiso::CreateXiso(ui::FileOpenISO(""));

			ImGui::Separator();
			if (ImGui::MenuItem("Screenshot", xsettings.shortcut_screenshot)) want_screenshot = (1 + 4) + 2; // force screenshot + maybe icon
			if (ImGui::MenuItem("Save Icon")) want_screenshot = 2 + 8;        // force icon
			ImGui::MenuItem("Intercept", xsettings.shortcut_intercept, &video_window.is_open);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("Help", nullptr)) xemu_open_web_browser("https://xemu.app/docs/getting-started/");

			ImGui::MenuItem("Report Compatibility...", nullptr, &compatibility_reporter_window.is_open);
			ImGui::MenuItem("Check for Updates...", nullptr, &update_window.is_open);

			ImGui::Separator();
			ImGui::MenuItem("About", nullptr, &about_window.is_open);
			ImGui::EndMenu();
		}

		g_main_menu_height = ImGui::GetWindowHeight();
		ImGui::EndMainMenuBar();

		// save directly if update = 1, or after update-1 frames delay
		if (update) dirty_menu = update;
		if (dirty_menu && !--dirty_menu) xsettingsSave();
	}
}

static void InitializeStyle()
{
	auto& io = ImGui::GetIO();

	io.Fonts->Clear();

	ImFontConfig roboto_font_cfg         = ImFontConfig();
	roboto_font_cfg.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF((void*)roboto_medium_data, roboto_medium_size, 16 * xsettings.ui_scale, &roboto_font_cfg);

	ImFontConfig font_cfg = ImFontConfig();
	font_cfg.OversampleH  = 1;
	font_cfg.OversampleV  = 1;
	font_cfg.PixelSnapH   = true;
	font_cfg.SizePixels   = 13.0f * xsettings.ui_scale;
	g_fixed_width_font    = io.Fonts->AddFontDefault(&font_cfg);

	ImGui_ImplOpenGL3_CreateFontsTexture();

	ImGuiStyle style;
	style.WindowRounding    = 8.0;
	style.FrameRounding     = 8.0;
	style.GrabRounding      = 12.0;
	style.PopupRounding     = 5.0;
	style.ScrollbarRounding = 12.0;
	style.FramePadding.x    = 10;
	style.FramePadding.y    = 4;
	style.WindowBorderSize  = 0;
	style.PopupBorderSize   = 0;
	style.FrameBorderSize   = 0;
	style.TabBorderSize     = 0;
	ImGui::GetStyle()       = style;
	ImGui::GetStyle().ScaleAllSizes(xsettings.ui_scale);

	// Set default theme, override
	ImGui::StyleColorsDark();

	ImVec4* colors                         = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Border]                = ImVec4(0.11f, 0.11f, 0.11f, 0.60f);
	colors[ImGuiCol_BorderShadow]          = ImVec4(0.16f, 0.16f, 0.16f, 0.00f);
	colors[ImGuiCol_Button]                = ImVec4(0.36f, 0.36f, 0.36f, 1.00f);
	colors[ImGuiCol_ButtonActive]          = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_ButtonHovered]         = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_CheckMark]             = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_ChildBg]               = ImVec4(0.16f, 0.16f, 0.16f, 0.58f);
	colors[ImGuiCol_DragDropTarget]        = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	colors[ImGuiCol_FrameBg]               = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_FrameBgActive]         = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.28f, 0.71f, 0.25f, 0.78f);
	colors[ImGuiCol_Header]                = ImVec4(0.28f, 0.71f, 0.25f, 0.76f);
	colors[ImGuiCol_HeaderActive]          = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.71f, 0.25f, 0.86f);
	colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 0.00f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.16f, 0.16f, 0.16f, 0.73f);
	colors[ImGuiCol_NavHighlight]          = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_PlotHistogram]         = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
	colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_PlotLines]             = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
	colors[ImGuiCol_PlotLinesHovered]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_PopupBg]               = ImVec4(0.16f, 0.16f, 0.16f, 0.90f);
	colors[ImGuiCol_ResizeGrip]            = ImVec4(0.47f, 0.83f, 0.49f, 0.04f);
	colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.28f, 0.71f, 0.25f, 0.78f);
	colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.20f, 0.51f, 0.18f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.28f, 0.71f, 0.25f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.71f, 0.25f, 0.78f);
	colors[ImGuiCol_Separator]             = ImVec4(0.11f, 0.11f, 0.11f, 0.60f);
	colors[ImGuiCol_SeparatorActive]       = ImVec4(0.25f, 0.75f, 0.10f, 1.00f);
	colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.13f, 0.87f, 0.16f, 0.78f);
	colors[ImGuiCol_SliderGrab]            = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_Tab]                   = ImVec4(0.26f, 0.67f, 0.23f, 0.95f);
	colors[ImGuiCol_TabActive]             = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_TabHovered]            = ImVec4(0.28f, 0.71f, 0.25f, 0.86f);
	colors[ImGuiCol_TabUnfocused]          = ImVec4(0.21f, 0.54f, 0.19f, 0.99f);
	colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.24f, 0.60f, 0.21f, 1.00f);
	colors[ImGuiCol_Text]                  = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
	colors[ImGuiCol_TextDisabled]          = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.28f, 0.71f, 0.25f, 0.43f);
	colors[ImGuiCol_TitleBg]               = ImVec4(0.20f, 0.51f, 0.18f, 1.00f);
	colors[ImGuiCol_TitleBgActive]         = ImVec4(0.26f, 0.66f, 0.23f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.16f, 0.16f, 0.16f, 0.75f);
	colors[ImGuiCol_WindowBg]              = ImVec4(0.06f, 0.06f, 0.06f, 0.98f);
}

/* External interface, called from ui/xemu.c which handles SDL main loop */
static FirstBootWindow first_boot_window;
static SDL_Window*     g_sdl_window;

void xemu_hud_init(SDL_Window* window, void* sdl_gl_context)
{
	xemu_monitor_init();

	initialize_custom_ui_rendering();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = nullptr;

	// Setup Platform/Renderer bindings
	ImGui_ImplSDL2_InitForOpenGL(window, sdl_gl_context);
	ImGui_ImplOpenGL3_Init("#version 150");

	first_boot_window.is_open = xsettingsFailed();
	g_sdl_window              = window;

	ImPlot::CreateContext();
	ui::OpenGamesList();
    games_window.Initialize();

#if defined(_WIN32)
	int should_check_for_update = xsettings.check_for_update;
	if (should_check_for_update == -1)
		update_window.should_prompt_auto_update_selection = update_window.is_open = !xsettingsFailed();
	else if (should_check_for_update)
		update_window.check_for_updates_and_prompt_if_available();
#endif
}

void xemu_hud_cleanup()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
}

void xemu_hud_process_sdl_events(SDL_Event* event)
{
	if (event->type == SDL_KEYDOWN)
		last_key = event->key.keysym.scancode;

	// waiting for a key to be pushed => don't let ImGUI process the event
	//+ ignore that key being released
	bool imgui_process = true;
	if (selectedType == INPUT_DEVICE_SDL_KEYBOARD && (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP))
	{
		if (selectedInput || selectedKey >= 0)
			imgui_process = false;
		if (event->type == SDL_KEYUP && event->key.keysym.scancode == selectedKey)
			selectedKey = -1;
	}
	if (imgui_process) ImGui_ImplSDL2_ProcessEvent(event);
}

void xemu_hud_should_capture_kbd_mouse(int* kbd, int* mouse)
{
	auto& io = ImGui::GetIO();
	if (kbd) *kbd = io.WantCaptureKeyboard;
	if (mouse) *mouse = io.WantCaptureMouse;
}

void xemu_hud_render()
{
	uint32_t now       = SDL_GetTicks();
	bool     ui_wakeup = false;

	// Combine all controller states to allow any controller to navigate
	uint32_t buttons                      = 0;
	int16_t  axis[CONTROLLER_AXIS__COUNT] = { 0 };

	ControllerState* iter;
	QTAILQ_FOREACH(iter, &available_controllers, entry)
	{
		if (iter->type != INPUT_DEVICE_SDL_GAMECONTROLLER)
			continue;
		buttons |= iter->buttons;
		// We simply take any axis that is >10 % activation
		for (int i = 0; i < CONTROLLER_AXIS__COUNT; i++)
		{
			if ((iter->axis[i] > 3276) || (iter->axis[i] < -3276))
				axis[i] = iter->axis[i];
		}
	}

	// If the guide button is pressed, wake the ui
	bool menu_button = false;
	if (buttons & CONTROLLER_BUTTON_GUIDE)
	{
		ui_wakeup   = true;
		menu_button = true;
	}

	// Allow controllers without a guide button to also work
	if ((buttons & CONTROLLER_BUTTON_BACK) && (buttons & CONTROLLER_BUTTON_START))
	{
		ui_wakeup   = true;
		menu_button = true;
	}

	// If the mouse is moved, wake the ui
	static ImVec2 last_mouse_pos    = ImVec2();
	ImVec2        current_mouse_pos = ImGui::GetMousePos();
	if ((current_mouse_pos.x != last_mouse_pos.x) || (current_mouse_pos.y != last_mouse_pos.y))
	{
		last_mouse_pos = current_mouse_pos;
		ui_wakeup      = true;
	}

	// If mouse capturing is enabled (we are in a dialog), ensure the UI is alive
	bool  controller_focus_capture = false;
	auto& io                       = ImGui::GetIO();
	if (io.NavActive)
	{
		ui_wakeup                = true;
		controller_focus_capture = true;
	}

	// Prevent controller events from going to the guest if they are being used
	// to navigate the HUD
	xemu_input_set_test_mode(controller_focus_capture);

	if (g_trigger_style_update)
	{
		InitializeStyle();
		g_trigger_style_update = false;
	}

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();

	// Override SDL2 implementation gamecontroller interface
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
	ImGui_ImplSDL2_NewFrame(g_sdl_window);

	// button config => don't let ImGui process the pad
	bool gamepad_control = true;
	if (selectedType == INPUT_DEVICE_SDL_GAMECONTROLLER)
	{
		if (selectedInput)
			gamepad_control = false;
		else if (selectedButton >= 0)
		{
			if (buttons & (1 << selectedButton))
				gamepad_control = false;
			else
				selectedButton = -1;
		}
	}

	if (gamepad_control)
	{
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

		// Update gamepad inputs (from imgui_impl_sdl.cpp)
		memset(io.NavInputs, 0, sizeof(io.NavInputs));
#define MAP_BUTTON(NAV_NO, BUTTON_NO) \
	{ \
		io.NavInputs[NAV_NO] = (buttons & BUTTON_NO) ? 1.0f : 0.0f; \
	}
#define MAP_ANALOG(NAV_NO, AXIS_NO, V0, V1) \
	{ \
		float vn = (float)(axis[AXIS_NO] - V0) / (float)(V1 - V0); \
		if (vn > 1.0f) vn = 1.0f; \
		if (vn > 0.0f && io.NavInputs[NAV_NO] < vn) io.NavInputs[NAV_NO] = vn; \
	}
		const int thumb_dead_zone = 8000;                                  // SDL_gamecontroller.h suggests using this value.
		MAP_BUTTON(ImGuiNavInput_Activate, CONTROLLER_BUTTON_A);           // Cross / A
		MAP_BUTTON(ImGuiNavInput_Cancel, CONTROLLER_BUTTON_B);             // Circle / B
		MAP_BUTTON(ImGuiNavInput_Input, CONTROLLER_BUTTON_Y);              // Triangle / Y
		MAP_BUTTON(ImGuiNavInput_DpadLeft, CONTROLLER_BUTTON_DPAD_LEFT);   // D-Pad Left
		MAP_BUTTON(ImGuiNavInput_DpadRight, CONTROLLER_BUTTON_DPAD_RIGHT); // D-Pad Right
		MAP_BUTTON(ImGuiNavInput_DpadUp, CONTROLLER_BUTTON_DPAD_UP);       // D-Pad Up
		MAP_BUTTON(ImGuiNavInput_DpadDown, CONTROLLER_BUTTON_DPAD_DOWN);   // D-Pad Down
		MAP_BUTTON(ImGuiNavInput_FocusPrev, CONTROLLER_BUTTON_WHITE);      // L1 / LB
		MAP_BUTTON(ImGuiNavInput_FocusNext, CONTROLLER_BUTTON_BLACK);      // R1 / RB
		MAP_BUTTON(ImGuiNavInput_TweakSlow, CONTROLLER_BUTTON_WHITE);      // L1 / LB
		MAP_BUTTON(ImGuiNavInput_TweakFast, CONTROLLER_BUTTON_BLACK);      // R1 / RB

		// Allow Guide and "Back+Start" buttons to act as Menu button
		if (menu_button) io.NavInputs[ImGuiNavInput_Menu] = 1.0;

		MAP_ANALOG(ImGuiNavInput_LStickLeft, CONTROLLER_AXIS_LSTICK_X, -thumb_dead_zone, -32768);
		MAP_ANALOG(ImGuiNavInput_LStickRight, CONTROLLER_AXIS_LSTICK_X, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickUp, CONTROLLER_AXIS_LSTICK_Y, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickDown, CONTROLLER_AXIS_LSTICK_Y, -thumb_dead_zone, -32767);
	}

	if (selectedInput && selectedType == INPUT_DEVICE_SDL_KEYBOARD) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	else io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::NewFrame();
	process_keyboard_shortcuts();

	if (!first_boot_window.is_open)
	{
		// Auto-hide main menu after 5s of inactivity
		static uint32_t last_check    = 0;
		float           alpha         = 1.0;
		const uint32_t  timeout       = 5000;
		const float     fade_duration = 1000.0;
		if (ui_wakeup) last_check = now;

		if ((now - last_check) > timeout)
		{
			float t = fmin((float)((now - last_check) - timeout) / fade_duration, 1.0);
			alpha   = 1.0 - t;
			if (t >= 1.0) alpha = 0.0;
		}
		if (alpha > 0.0)
		{
			ImVec4 tc = ImGui::GetStyle().Colors[ImGuiCol_Text];
			tc.w      = alpha;
			ImGui::PushStyleColor(ImGuiCol_Text, tc);
			ImGui::SetNextWindowBgAlpha(alpha);
			ShowMainMenu();
            games_window.is_open = true;
			ImGui::PopStyleColor();
		}
		else
        {
            g_main_menu_height = 0;
            games_window.is_open = false;
        }
	}

	first_boot_window.Draw();
	input_window.Draw();
	games_window.Draw();
	settings_window.Draw();
	monitor_window.Draw();
	apu_window.Draw();
	video_window.Draw();
	about_window.Draw();
	network_window.Draw();
	compatibility_reporter_window.Draw();
	notification_manager.Draw();
#if defined(_WIN32)
	update_window.Draw();
#endif

	if (showImGuiDemo) ImGui::ShowDemoWindow(&showImGuiDemo);
	if (showImPlotDemo) ImPlot::ShowDemoWindow(&showImPlotDemo);

	// Very rudimentary error notification API
	if (g_errors.size() > 0) ImGui::OpenPopup("Error");

	if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("%s", g_errors[0]);
		ImGui::Dummy(ImVec2(0, 16));
		ImGui::SetItemDefaultFocus();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (120 + 10));
		if (ImGui::Button("Ok", ImVec2(120, 0)))
		{
			ImGui::CloseCurrentPopup();
			free((void*)g_errors[0]);
			g_errors.pop_front();
		}
		ImGui::EndPopup();
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/* External interface, exposed via xemu-notifications.h */

void xemu_queue_notification(const char* msg, bool instant)
{
	notification_manager.QueueNotification(msg, instant);
}

void xemu_queue_error_message(const char* msg)
{
	g_errors.push_back(strdup(msg));
}

// API
//////

void OpenConfig(int tab)
{
    if (tab > 10)
        input_window.is_open = 1;
    else
        settings_window.OpenTab(tab);
}
