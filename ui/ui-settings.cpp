// ui-settings.cpp : Settings + Inputs
// @2022 octopoulos
// @2021 Matt Borgerson
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include "ui.h"
#include "xemu-custom-widgets.h"
#include "xemu-input.h"
#include "xemu-notifications.h"

#include <SDL.h>
#include "imgui/backends/imgui_impl_sdl.h"

extern "C" {
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "hw/xbox/nv2a/nv2a.h"
}

extern FBO*        controller_fbo;
extern SDL_Window* m_window;
extern bool        g_trigger_style_update;

namespace ui
{

static XSettings prevSettings;

class SettingsWindow : public CommonWindow
{
private:
	int  changed    = 0;
	bool clickedNow = 0;
	int  tab        = 0;
	int  tabMenu    = 0;

public:
	SettingsWindow()
	{
		name = "Settings";
		memcpy(&prevSettings, &xsettings, sizeof(XSettings));
	}

	void Load() { memcpy(&prevSettings, &xsettings, sizeof(XSettings)); }

	void Save()
	{
		xsettingsSave();
		xemu_queue_notification("Settings saved!", false);

		if ((changed = xsettingsCompare(&prevSettings)))
			memcpy(&prevSettings, &xsettings, sizeof(XSettings));
	}

	void OpenTab(int tabMenu_)
	{
		tab     = tabMenu_;
		tabMenu = tabMenu_;
		isOpen  = true;
	}

	/**
	 * Draw all tabs
	 */

	void Draw()
	{
		CHECK_DRAW();
		ImGui::SetNextWindowSize(ImVec2(900.0f * xsettings.ui_scale, 600.0f * xsettings.ui_scale));
		if (!ImGui::Begin("Settings", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		if (ImGui::IsWindowAppearing()) Load();

		const char* tabNames[] = {
			"CPU",
			"GPU",
			"Audio",
			"",
			"Pads",
			"System",
			"Network",
			"Advanced",
			"Emulator",
			"GUI",
			"Debug",
			"",
			"Shortcuts",
			"Theme Editor",
			nullptr,
		};

		ImGui::BeginChild("main", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
		{
			clickedNow = false;

			// left
			{
				ImGui::BeginChild("left pane", ImVec2(150, 0), true);
				for (int i = 0, j = 0;; ++i)
				{
					auto text = tabNames[i];
					if (!text)
						break;
					else if (!*text)
						ImGui::Separator();
					else
					{
						if (ImGui::Selectable(text, tab == j))
						{
							clickedNow = true;
							tab        = j;
							tabMenu    = j;
						}
						++j;
					}
				}
				ImGui::EndChild();
			}
			ImGui::SameLine();

			// right
			{
				ImGui::BeginGroup();
				ImGui::BeginChild("item view");
				AddSpace(0);

				switch (tabMenu)
				{
				case 0: DrawCPU(); break;
				case 1: DrawGPU(); break;
				case 2: DrawAudio(); break;
				case 3: DrawPads(); break;
				case 4: DrawSystem(); break;
				case 5: DrawNetwork(); break;
				case 6: DrawAdvanced(); break;
				case 7: DrawEmulator(); break;
				case 8: DrawGUI(); break;
				case 9: DrawDebug(); break;
				case 10: DrawShortcuts(); break;
				default: break;
				}

				ImGui::EndChild();
				ImGui::EndGroup();
			}
		}
		ImGui::EndChild();

		// footer
		{
			ImGui::TextUnformatted("Description");

			auto style = ImGui::GetStyle();
			auto size  = ImGui::CalcTextSize("OK");
			// ImGui::SetCursorPosY(ImGui::GetWindowHeight() - size.y - style.FramePadding.y * 2 - style.WindowPadding.y);
			ImGui::SameLine();

			if (ImGui::Button("Restore Defaults"))
			{
				memcpy(&xsettings, &prevSettings, sizeof(XSettings));
				changed = 0;
			}
			ImGui::SameLine();
			ImGui::SetItemDefaultFocus();
			if (ImGui::Button("Save"))
			{
				Save();
				isOpen = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Close"))
			{
				if ((changed = xsettingsCompare(&prevSettings))) {}
				memcpy(&xsettings, &prevSettings, sizeof(XSettings));
				isOpen = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Apply")) {}

			if (changed & 2)
			{
				const char* msg = "Restart to apply changes";
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(msg).x) / 2.0);
				ImGui::TextUnformatted(msg);
				ImGui::SameLine();
			}
		}

		ImGui::End();
	}

private:
	void DrawCPU();
	void DrawGPU();
	void DrawAudio();
	void DrawPads();
	void DrawSystem();
	void DrawNetwork();
	void DrawAdvanced();
	void DrawEmulator();
	void DrawGUI();
	void DrawDebug();
	void DrawShortcuts();
};

static SettingsWindow settingsWindow;
CommonWindow&         GetSettingsWindow() { return settingsWindow; }

void SettingsWindow::DrawCPU()
{
	ImGui::Checkbox("Hard FPU", (bool*)&xsettings.hard_fpu);
}

void SettingsWindow::DrawGPU()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	AddCombo("renderer", "Renderer");
	if (AddSliderInt("resolution_scale", "Resolution Scale", "%dx")) nv2a_set_surface_scale_factor(xsettings.resolution_scale);
	AddCombo("aspect_ratio", "Aspect Ratio");

	ImGui::Checkbox("Stretch to Display Area", (bool*)&xsettings.stretch);
	{
		if (!xsettings.stretch) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::Checkbox("Vertical Integer Scaling", (bool*)&xsettings.integer_scaling);
		if (!xsettings.stretch) ImGui::PopItemFlag();
	}

	AddCombo("frame_limit", "Frame Limit");
	const char* sAnisotropics[] = { "Auto", "1x", "2x", "4x", "8x", "16x" };
	AddCombo("anisotropic", "Anisotropic Filtering", sAnisotropics, { 0, 1, 2, 4, 8, 16 });

	const char* off_on_auto[] = { "Off", "On", "Auto" };
	ImGui::Combo("Dither", &xsettings.dither, off_on_auto, 3);
	ImGui::Combo("Line Smooth", &xsettings.line_smooth, off_on_auto, 3);
	ImGui::Combo("Polygon Smooth", &xsettings.polygon_smooth, off_on_auto, 3);
	ImGui::Checkbox("Show shader compilation hint", (bool*)&xsettings.shader_hint);

	// AddSpace();
	ImGui::NextColumn();
	ImGui::Checkbox("Graph Nearest", (bool*)&xsettings.graph_nearest);
	ImGui::Checkbox("Overlay Nearest", (bool*)&xsettings.overlay_nearest);
	ImGui::Checkbox("Scale Nearest", (bool*)&xsettings.scale_nearest);
	ImGui::Checkbox("Surface part Nearest", (bool*)&xsettings.surface_part_nearest);
	ImGui::Checkbox("Surface texture Nearest", (bool*)&xsettings.surface_texture_nearest);
}

void SettingsWindow::DrawAudio()
{
	ImGui::Checkbox("Use DSP", (bool*)&xsettings.use_dsp);
}

// INPUT
////////

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
	{0,   498, 240, 1, 1, PAD_BUTTON_A         },
	{ 1,  498, 198, 1, 1, PAD_BUTTON_B         },
	{ 2,  470, 223, 1, 2, PAD_BUTTON_X         },
	{ 3,  470, 180, 1, 2, PAD_BUTTON_Y         },
	{ 4,  0,   390, 1, 2, PAD_BUTTON_DPAD_LEFT },
	{ 5,  16,  350, 1, 0, PAD_BUTTON_DPAD_UP   },
	{ 6,  28,  390, 1, 1, PAD_BUTTON_DPAD_RIGHT},
	{ 7,  16,  430, 1, 0, PAD_BUTTON_DPAD_DOWN },
	{ 8,  222, 470, 1, 2, PAD_BUTTON_BACK      },
	{ 9,  270, 470, 1, 1, PAD_BUTTON_START     },
	{ 10, 435, 70,  1, 1, PAD_BUTTON_WHITE     },
	{ 11, 465, 110, 1, 1, PAD_BUTTON_BLACK     },
	{ 12, 16,  190, 1, 0, PAD_BUTTON_LSTICK    },
	{ 13, 468, 470, 1, 0, PAD_BUTTON_RSTICK    },
	{ 14, 246, 240, 1, 0, PAD_BUTTON_GUIDE     },
 // axes
	{ 22, 222, 30,  2, 2, PAD_AXIS_LTRIG       },
	{ 23, 270, 30,  2, 1, PAD_AXIS_RTRIG       },
	{ 24, 0,   110, 2, 2, PAD_AXIS_LSTICK_X    },
	{ 25, 16,  70,  2, 0, PAD_AXIS_LSTICK_Y    },
	{ 26, 28,  110, 2, 1, PAD_AXIS_LSTICK_X    },
	{ 27, 16,  150, 2, 0, PAD_AXIS_LSTICK_Y    },
	{ 28, 452, 390, 2, 2, PAD_AXIS_RSTICK_X    },
	{ 29, 468, 350, 2, 0, PAD_AXIS_RSTICK_Y    },
	{ 30, 480, 390, 2, 1, PAD_AXIS_RSTICK_X    },
	{ 31, 468, 430, 2, 0, PAD_AXIS_RSTICK_Y    },
};

static AxisButtonPos* selectedInput  = nullptr;
static int            selectedType   = -1;
static int            selectedButton = -1;
static int            selectedKey    = -1;
static int            last_button    = -1;
static int            last_key       = -1;

void SettingsWindow::DrawPads()
{
	static int active = 0;

	// Output dimensions of texture
	const float t_w               = 512.0f;
	const float t_h               = 512.0f;
	const float controller_width  = 477.0f;
	const float controller_height = 395.0f;

	// Setup rendering to fbo for controller and port images
	ImTextureID id = (ImTextureID)(intptr_t)render_to_fbo(controller_fbo);

	if (ImGui::BeginTabBar("Pads#tabs"))
	{
		for (int i = 0; i < 4; ++i)
			if (ImGui::BeginTabItem(fmt::format("Player {}", i + 1).c_str()))
			{
				active = i;
				ImGui::EndTabItem();
			}

		if (ImGui::BeginTabItem("Advanced"))
		{
			const char* homeActions[] = { "disable", "pause", "pause + windows" };
			ImGui::Combo("Guide", &xsettings.guide, homeActions, IM_ARRAYSIZE(homeActions));
			ImGui::Combo("Guide [Hold]", &xsettings.guide_hold, homeActions, IM_ARRAYSIZE(homeActions));
			AddSliderInt("guide_hold_time", "Hold after", "%d ms");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

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

	AddSpace();
	ImGui::Separator();
	AddSpace();

	// Render controller image
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
					Log("keyboard update: selected=%d last_key=%d : %s", selectedInput->id, last_key, bound_state->key_smapping);
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
					Log("pad update: selected=%d last_button=%d : %s", selectedInput->id, last_button, bound_state->pad_smapping);
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

	++frame;

	// Restore original framebuffer target
	render_to_default_fb();
}

void SettingsWindow::DrawSystem()
{
	const char* rom_file_filters  = ".bin Files\0*.bin\0.rom Files\0*.rom\0All Files\0*.*\0";
	const char* qcow_file_filters = ".qcow2 Files\0*.qcow2\0All Files\0*.*\0";

	static ImGuiTableFlags tFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;

	static std::vector<std::tuple<const char*, str2k*, const char*>> paths = {
		{"Flash (BIOS) File",     &xsettings.flash_path,   rom_file_filters },
		{ "MCPX Boot ROM File",   &xsettings.bootrom_path, rom_file_filters },
		{ "Hard Disk Image File", &xsettings.hdd_path,     qcow_file_filters},
		{ "EEPROM File",          &xsettings.eeprom_path,  rom_file_filters },
	};

	// use a table
	if (ImGui::BeginTable("Table", 3, tFlags))
	{
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed);

		for (auto& [text, value, filters] : paths)
		{
			ImGui::PushID(text);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text(text);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(400.0f);
			ImGui::InputText("", *value, sizeof(str2k));
			ImGui::TableSetColumnIndex(2);
			ImGui::Button("...");
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	const char* sMemories[] = { "64 MB", "128 MB" };
	AddCombo("memory", "System Memory", sMemories, { 64, 128 });
}

void SettingsWindow::DrawNetwork() { ImGui::Text("%s %d %d", "Network", tab, tabMenu); }

void SettingsWindow::DrawAdvanced()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	AddSliderInt("vblank_frequency", "Vblank Frequency", "%dHz");
}

void SettingsWindow::DrawEmulator()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	ImGui::Checkbox("Skip startup animation", (bool*)&xsettings.short_animation);
	ImGui::Checkbox("Check for updates on startup", (bool*)&xsettings.check_for_update);
	ImGui::Checkbox("Boot game at startup", (bool*)&xsettings.startup_game);
	ImGui::Checkbox("Start in Fullscreen mode", (bool*)&xsettings.start_fullscreen);
	ImGui::Checkbox("Resize window on boot", (bool*)&xsettings.resize_on_boot);
	{
		if (!xsettings.resize_on_boot) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		int ww, wh;
		SDL_GetWindowSize(m_window, &ww, &wh);
		ImGui::InputInt(fmt::format("width ({})", ww).c_str(), &xsettings.resize_width);
		ImGui::InputInt(fmt::format("height ({})", wh).c_str(), &xsettings.resize_height);
		if (!xsettings.resize_on_boot) ImGui::PopItemFlag();
	}
	ImGui::InputText("Window Title", xsettings.window_title, sizeof(xsettings.window_title));
	ImGui::Checkbox("Enable performance overlay", (bool*)&xsettings.performance_overlay);
}

void SettingsWindow::DrawGUI()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	if (AddCombo("theme", "Theme")) UpdateTheme();
	// if (AddCombo("font", "Font")) UpdateFont();

	{
		static float prev_delta = 0;
		float        prev_scale = xsettings.ui_scale;

		if (AddSliderFloat("ui_scale", "UI Scale", "%.3f"))
		{
			float delta  = xsettings.ui_scale - prev_scale;
			bool  change = (delta * prev_delta >= 0);
			if (!change && fabsf(delta) > 0.2f)
			{
				xsettings.ui_scale = prev_scale * 0.9f + xsettings.ui_scale * 0.1f;
				change             = true;
			}
			if (change) g_trigger_style_update = true;
			prev_delta = delta;
		}
	}

	ImGui::Checkbox("Text under Buttons", (bool*)&xsettings.text_button);
	ImGui::Checkbox("Hide UI when Running Game", (bool*)&xsettings.run_no_ui);
}

void SettingsWindow::DrawDebug()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	ImGui::InputTextMultiline("Intercept Filter", xsettings.intercept_filter, 2048);
}

void SettingsWindow::DrawShortcuts()
{
	static std::vector<std::pair<std::string, char*>> names = {
		{"Actions:",    nullptr                      },
		{ "Boot Disc",  xsettings.shortcut_open      },
		{ "Eject Disc", xsettings.shortcut_eject     },
		{ "Fullscreen", xsettings.shortcut_fullscreen},
		{ "Intercept",  xsettings.shortcut_intercept },
		{ "Load State", xsettings.shortcut_loadstate },
		{ "Pause",      xsettings.shortcut_pause     },
		{ "Reset",      xsettings.shortcut_reset     },
		{ "Save State", xsettings.shortcut_savestate },
		{ "Screenshot", xsettings.shortcut_screenshot},
		{ "Windows:",   nullptr                      },
		{ "Controls",   xsettings.shortcut_controls  },
		{ "Games",      xsettings.shortcut_games     },
		{ "Log",        xsettings.shortcut_log       },
		{ "Monitor",    xsettings.shortcut_monitor   },
		{ "Config:",    nullptr                      },
		{ "GPU",        xsettings.shortcut_gpu       },
		{ "Pads",       xsettings.shortcut_pads      },
	};

	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	for (auto& [name, buffer] : names)
	{
		if (!buffer)
		{
			if (name == "Windows:")
				ImGui::NextColumn();
			ImGui::TextUnformatted(name.c_str());
		}
		else
			ImGui::InputText(name.c_str(), buffer, sizeof(str32));
	}
}

// API
//////

void OpenConfig(int tab)
{
	// if (IsRunning()) TogglePause();
	ShowWindows(true, false);
	settingsWindow.OpenTab(tab);
}

void ProcessSDL(void* event_)
{
	SDL_Event* event = (SDL_Event*)event_;

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

void ProcessShortcuts()
{
}

void UpdateFont()
{
}

void UpdateIO()
{
	int16_t     axis[PAD_AXIS__COUNT] = { 0 };
	uint32_t    buttons               = 0;
	static int  homeFrame             = 0;
	static auto homeStart             = std::chrono::steady_clock::now();

	// Allow any controller to navigate
	ControllerState* iter;
	QTAILQ_FOREACH(iter, &available_controllers, entry)
	{
		if (iter->type != INPUT_DEVICE_SDL_GAMECONTROLLER)
			continue;
		buttons |= iter->buttons;
		for (int i = 0; i < PAD_AXIS__COUNT; i++)
		{
			if ((iter->axis[i] > 3276) || (iter->axis[i] < -3276))
				axis[i] = iter->axis[i];
		}
	}

	auto& io = ImGui::GetIO();

	// home button => toggle the game list + pause/unpause the game
	auto now = std::chrono::steady_clock::now();
	if ((buttons & PAD_BUTTON_GUIDE) || io.KeysDown[SDL_SCANCODE_ESCAPE])
	{
		if (homeFrame >= 0)
		{
			++homeFrame;
			if (homeFrame == 1)
				HomeGuide(false);
			else
			{
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - homeStart).count();
				if (elapsed >= xsettings.guide_hold_time)
				{
					HomeGuide(true);
					homeFrame = -1; // special value to stop checking
				}
			}
		}
	}
	else
	{
		homeFrame = 0;
		homeStart = now;
	}

	// Override SDL2 implementation gamecontroller interface
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
	ImGui_ImplSDL2_NewFrame(m_window);

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
#define MAP_BUTTON(NAV_NO, BUTTON_NO) io.NavInputs[NAV_NO] = (buttons & BUTTON_NO) ? 1.0f : 0.0f;
#define MAP_ANALOG(NAV_NO, AXIS_NO, V0, V1)                                    \
	{                                                                          \
		float vn = (float)(axis[AXIS_NO] - V0) / (float)(V1 - V0);             \
		if (vn > 1.0f) vn = 1.0f;                                              \
		if (vn > 0.0f && io.NavInputs[NAV_NO] < vn) io.NavInputs[NAV_NO] = vn; \
	}
		const int thumb_dead_zone = 8000;                           // SDL_gamecontroller.h suggests using this value.
		MAP_BUTTON(ImGuiNavInput_Activate, PAD_BUTTON_A);           // Cross / A
		MAP_BUTTON(ImGuiNavInput_Cancel, PAD_BUTTON_B);             // Circle / B
		MAP_BUTTON(ImGuiNavInput_Input, PAD_BUTTON_Y);              // Triangle / Y
		MAP_BUTTON(ImGuiNavInput_DpadLeft, PAD_BUTTON_DPAD_LEFT);   // D-Pad Left
		MAP_BUTTON(ImGuiNavInput_DpadRight, PAD_BUTTON_DPAD_RIGHT); // D-Pad Right
		MAP_BUTTON(ImGuiNavInput_DpadUp, PAD_BUTTON_DPAD_UP);       // D-Pad Up
		MAP_BUTTON(ImGuiNavInput_DpadDown, PAD_BUTTON_DPAD_DOWN);   // D-Pad Down
		MAP_BUTTON(ImGuiNavInput_FocusPrev, PAD_BUTTON_WHITE);      // L1 / LB
		MAP_BUTTON(ImGuiNavInput_FocusNext, PAD_BUTTON_BLACK);      // R1 / RB
		MAP_BUTTON(ImGuiNavInput_TweakSlow, PAD_BUTTON_WHITE);      // L1 / LB
		MAP_BUTTON(ImGuiNavInput_TweakFast, PAD_BUTTON_BLACK);      // R1 / RB

		MAP_ANALOG(ImGuiNavInput_LStickLeft, PAD_AXIS_LSTICK_X, -thumb_dead_zone, -32768);
		MAP_ANALOG(ImGuiNavInput_LStickRight, PAD_AXIS_LSTICK_X, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickUp, PAD_AXIS_LSTICK_Y, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickDown, PAD_AXIS_LSTICK_Y, -thumb_dead_zone, -32767);
	}

	if (selectedInput && selectedType == INPUT_DEVICE_SDL_KEYBOARD)
		io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	else
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ProcessShortcuts();
}

} // namespace ui
