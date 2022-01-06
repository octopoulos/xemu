// ui-settings.cpp : Settings + Inputs
// @2022 octopoulos

#include "ui-controls.h"
#include "ui-games.h"
#include "ui-log.h"
#include "ui-settings.h"

#include "xemu-custom-widgets.h"
#include "xemu-input.h"
#include "xemu-notifications.h"

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

const char* sRenderers[] = { "DX9", "DX11", "OpenGL", "Vulkan", "Null" };

namespace ui
{

static SettingsWindow settingsWindow;
SettingsWindow&       GetSettingsWindow() { return settingsWindow; }

static XSettings prevSettings;

SettingsWindow::SettingsWindow()
{
	memcpy(&prevSettings, &xsettings, sizeof(XSettings));
}

void SettingsWindow::Load()
{
	memcpy(&prevSettings, &xsettings, sizeof(XSettings));
}

void SettingsWindow::Save()
{
	xsettingsSave();
	xemu_queue_notification("Settings saved!", false);

	if ((changed = xsettingsCompare(&prevSettings)))
		memcpy(&prevSettings, &xsettings, sizeof(XSettings));
}

void SettingsWindow::OpenTab(int tabMenu_)
{
	tabMenu    = tabMenu_;
	isOpen     = true;
	manualOpen = true;
}

void FilePicker(const char* name, char* buf, size_t len, const char* filters)
{
	ImGui::PushID(name);
	ImGui::InputText("##file", buf, len);

	ImGui::SameLine();
	if (ImGui::Button("..."))
	{
		auto selected = FileOpen(filters, buf);
		if (selected.size() && selected != buf)
			strcpy(buf, selected.c_str());
	}
	ImGui::PopID();
}

void SettingsWindow::Draw()
{
	if (!isOpen)
		return;

	ImGui::SetNextWindowContentSize(ImVec2(800.0f * xsettings.ui_scale, 600.0f * xsettings.ui_scale));
	if (!ImGui::Begin("Settings", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	if (ImGui::IsWindowAppearing()) Load();

	clickedNow = false;

	if (ImGui::BeginTabBar("Settings#tabs"))
	{
		const char* tabNames[] = {
			"CPU",
			"GPU",
			"Audio",
			"Pads",
			"System",
			"Network",
			"Advanced",
			"Emulator",
			"GUI",
			"Debug",
			nullptr,
		};

		for (int i = 0; tabNames[i]; ++i)
			if (ImGui::BeginTabItem(tabNames[i]))
			{
				if (tab != i)
				{
					clickedNow = true;
					tab        = i;
					tabMenu    = i;
				}
				ImGui::EndTabItem();
			}

		ImGui::EndTabBar();
	}

	ImGui::Dummy(ImVec2(0, 0));

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
	default: break;
	}

	ImGui::TextUnformatted("Description");

	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 60);

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

	ImGui::End();
}

void SettingsWindow::DrawCPU() { ImGui::Text("%s %d %d", "CPU", tab, tabMenu); }

void SettingsWindow::DrawGPU()
{
	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	ImGui::Combo("Renderer", &xsettings.renderer, sRenderers, IM_ARRAYSIZE(sRenderers));

	if (ImGui::SliderInt("Resolution Scale", &xsettings.resolution_scale, 1, 10, "%dx"))
		nv2a_set_surface_scale_factor(xsettings.resolution_scale);

	const char* sAspectRatios[] = { "16:9", "4:3", "Native", "Window" };
	ImGui::Combo("Aspect Ratio", &xsettings.aspect_ratio, sAspectRatios, IM_ARRAYSIZE(sAspectRatios));

	ImGui::Checkbox("Stretch to Display Area", (bool*)&xsettings.stretch);
	{
		if (!xsettings.stretch) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::Checkbox("Vertical Integer Scaling", (bool*)&xsettings.integer_scaling);
		if (!xsettings.stretch) ImGui::PopItemFlag();
	}
	{
		const char*              sAnisotropics[] = { "Auto", "1x", "2x", "4x", "8x", "16x" };
		const std::array<int, 6> anisotropics    = { 0, 1, 2, 4, 8, 16 };
		auto                     it              = std::find(anisotropics.begin(), anisotropics.end(), xsettings.anisotropic);
		int                      anisotropic     = it ? std::distance(anisotropics.begin(), it) : 0;

		if (ImGui::Combo("Anisotropic Filtering", &anisotropic, sAnisotropics, IM_ARRAYSIZE(sAnisotropics)))
			xsettings.anisotropic = anisotropics[anisotropic];
	}

	const char* off_on_auto[] = { "Off", "On", "Auto" };
	ImGui::Combo("Dither", &xsettings.dither, off_on_auto, IM_ARRAYSIZE(off_on_auto));
	ImGui::Combo("Line Smooth", &xsettings.line_smooth, off_on_auto, IM_ARRAYSIZE(off_on_auto));
	ImGui::Combo("Polygon Smooth", &xsettings.polygon_smooth, off_on_auto, IM_ARRAYSIZE(off_on_auto));
	ImGui::Checkbox("Show shader compilation hint", (bool*)&xsettings.shader_hint);

	// ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().WindowPadding.y));
	ImGui::NextColumn();
	ImGui::Checkbox("FBO Nearest", (bool*)&xsettings.fbo_nearest);
	ImGui::Checkbox("Graph Nearest", (bool*)&xsettings.graph_nearest);
	ImGui::Checkbox("Overlay Nearest", (bool*)&xsettings.overlay_nearest);
	ImGui::Checkbox("Scale Nearest", (bool*)&xsettings.scale_nearest);
	ImGui::Checkbox("Shader Nearest", (bool*)&xsettings.shader_nearest);
	ImGui::Checkbox("Surface part Nearest", (bool*)&xsettings.surface_part_nearest);
	ImGui::Checkbox("Surface texture Nearest", (bool*)&xsettings.surface_texture_nearest);

	ImGui::EndColumns();
}

void SettingsWindow::DrawAudio() { ImGui::Text("%s %d %d", "Audio", tab, tabMenu); }

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
	{0,   498, 240, 1, 1, CONTROLLER_BUTTON_A         },
	{ 1,  498, 198, 1, 1, CONTROLLER_BUTTON_B         },
	{ 2,  470, 223, 1, 2, CONTROLLER_BUTTON_X         },
	{ 3,  470, 180, 1, 2, CONTROLLER_BUTTON_Y         },
	{ 4,  0,   390, 1, 2, CONTROLLER_BUTTON_DPAD_LEFT },
	{ 5,  16,  350, 1, 0, CONTROLLER_BUTTON_DPAD_UP   },
	{ 6,  28,  390, 1, 1, CONTROLLER_BUTTON_DPAD_RIGHT},
	{ 7,  16,  430, 1, 0, CONTROLLER_BUTTON_DPAD_DOWN },
	{ 8,  222, 470, 1, 2, CONTROLLER_BUTTON_BACK      },
	{ 9,  270, 470, 1, 1, CONTROLLER_BUTTON_START     },
	{ 10, 435, 70,  1, 1, CONTROLLER_BUTTON_WHITE     },
	{ 11, 465, 110, 1, 1, CONTROLLER_BUTTON_BLACK     },
	{ 12, 16,  190, 1, 0, CONTROLLER_BUTTON_LSTICK    },
	{ 13, 468, 470, 1, 0, CONTROLLER_BUTTON_RSTICK    },
	{ 14, 246, 240, 1, 0, CONTROLLER_BUTTON_GUIDE     },
 // axes
	{ 22, 222, 30,  2, 2, CONTROLLER_AXIS_LTRIG       },
	{ 23, 270, 30,  2, 1, CONTROLLER_AXIS_RTRIG       },
	{ 24, 0,   110, 2, 2, CONTROLLER_AXIS_LSTICK_X    },
	{ 25, 16,  70,  2, 0, CONTROLLER_AXIS_LSTICK_Y    },
	{ 26, 28,  110, 2, 1, CONTROLLER_AXIS_LSTICK_X    },
	{ 27, 16,  150, 2, 0, CONTROLLER_AXIS_LSTICK_Y    },
	{ 28, 452, 390, 2, 2, CONTROLLER_AXIS_RSTICK_X    },
	{ 29, 468, 350, 2, 0, CONTROLLER_AXIS_RSTICK_Y    },
	{ 30, 480, 390, 2, 1, CONTROLLER_AXIS_RSTICK_X    },
	{ 31, 468, 430, 2, 0, CONTROLLER_AXIS_RSTICK_Y    },
};

static AxisButtonPos* selectedInput  = nullptr;
static int            selectedType   = -1;
static int            selectedButton = -1;
static int            selectedKey    = -1;
static int            last_button    = -1;
static int            last_key       = -1;

void SettingsWindow::DrawPads()
{
	// ImGui::SetNextWindowContentSize(ImVec2(600.0f * xsettings.ui_scale, 0.0f));
	// Remove window X padding for this window to easily center stuff
	// ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, ImGui::GetStyle().WindowPadding.y));
	// if (!ImGui::Begin("Gamepad Settings", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	// {
	//     ImGui::End();
	//     ImGui::PopStyleVar();
	//     return;
	// }

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

	// ImGui::End();
	// ImGui::PopStyleVar(); // Window padding
	++frame;

	// Restore original framebuffer target
	render_to_default_fb();
}

void SettingsWindow::DrawSystem()
{
	const char* rom_file_filters  = ".bin Files\0*.bin\0.rom Files\0*.rom\0All Files\0*.*\0";
	const char* qcow_file_filters = ".qcow2 Files\0*.qcow2\0All Files\0*.*\0";

	ImGui::Columns(2, "", false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.25);

	ImGui::Text("Flash (BIOS) File");
	ImGui::NextColumn();
	float picker_width = ImGui::GetColumnWidth() - 120 * xsettings.ui_scale;
	ImGui::SetNextItemWidth(picker_width);
	FilePicker("###Flash", xsettings.flash_path, sizeof(xsettings.flash_path), rom_file_filters);
	ImGui::NextColumn();

	ImGui::Text("MCPX Boot ROM File");
	ImGui::NextColumn();
	ImGui::SetNextItemWidth(picker_width);
	FilePicker("###BootROM", xsettings.bootrom_path, sizeof(xsettings.bootrom_path), rom_file_filters);
	ImGui::NextColumn();

	ImGui::Text("Hard Disk Image File");
	ImGui::NextColumn();
	ImGui::SetNextItemWidth(picker_width);
	FilePicker("###HDD", xsettings.hdd_path, sizeof(xsettings.hdd_path), qcow_file_filters);
	ImGui::NextColumn();

	ImGui::Text("EEPROM File");
	ImGui::NextColumn();
	ImGui::SetNextItemWidth(picker_width);
	FilePicker("###EEPROM", xsettings.eeprom_path, sizeof(xsettings.eeprom_path), rom_file_filters);
	ImGui::NextColumn();

	ImGui::Text("System Memory");
	ImGui::NextColumn();
	ImGui::SetNextItemWidth(ImGui::GetColumnWidth() * 0.5);

	{
		const char*              sMemories[] = { "64 MiB", "128 MiB" };
		const std::array<int, 2> memories    = { 64, 128 };
		auto                     it          = std::find(memories.begin(), memories.end(), xsettings.memory);
		int                      memory      = it ? std::distance(memories.begin(), it) : 0;

		if (ImGui::Combo("###mem", &memory, sMemories, IM_ARRAYSIZE(sMemories)))
			xsettings.memory = memories[memory];
	}

	ImGui::EndColumns();
}

void SettingsWindow::DrawNetwork() { ImGui::Text("%s %d %d", "Network", tab, tabMenu); }

void SettingsWindow::DrawAdvanced() { ImGui::Text("%s %d %d", "Advanced", tab, tabMenu); }

void SettingsWindow::DrawEmulator()
{
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

	static float prev_delta = 0;
	float        prev_scale = xsettings.ui_scale;
	if (ImGui::SliderFloat("UI Scale", &xsettings.ui_scale, 1.0f, 4.0f, "%.3f"))
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

	const char* homeActions[] = { "disable", "pause", "pause + windows" };
	ImGui::Combo("Guide", &xsettings.guide, homeActions, IM_ARRAYSIZE(homeActions));
	ImGui::Combo("Guide [Hold]", &xsettings.guide_hold, homeActions, IM_ARRAYSIZE(homeActions));
	ImGui::SliderInt("Hold after", &xsettings.guide_hold_frames, 1, 60, "%d frames");
	ImGui::Checkbox("Hide UI when running Game", (bool*)&xsettings.run_no_ui);

	// shortcuts
	ImGui::NextColumn();

	static std::vector<std::pair<std::string, char*>> names = {
		{"Actions:",    nullptr                      },
		{ "Boot Disc",  xsettings.shortcut_open      },
		{ "Eject Disc", xsettings.shortcut_eject     },
		{ "Fullscreen", xsettings.shortcut_fullscreen},
		{ "Intercept",  xsettings.shortcut_intercept },
		{ "Pause",      xsettings.shortcut_pause     },
		{ "Reset",      xsettings.shortcut_reset     },
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

	for (auto& [name, buffer] : names)
	{
		if (!buffer)
			ImGui::TextUnformatted(name.c_str());
		else
			ImGui::InputText(name.c_str(), buffer, sizeof(str32));
	}

	ImGui::EndColumns();
}

void SettingsWindow::DrawDebug()
{
	ImGui::Text("%s %d %d", "Debug", tab, tabMenu);

	ImGui::InputTextMultiline("Intercept Filter", xsettings.intercept_filter, 2048);
}

// API
//////

void OpenConfig(int tab) { settingsWindow.OpenTab(tab); }

void ProcessSDL(SDL_Event* event)
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

void ProcessShortcuts()
{
}

void UpdateIO()
{
	static int homeFrame                    = 0;
	uint32_t   buttons                      = 0;
	int16_t    axis[CONTROLLER_AXIS__COUNT] = { 0 };

	// Allow any controller to navigate
	ControllerState* iter;
	QTAILQ_FOREACH(iter, &available_controllers, entry)
	{
		if (iter->type != INPUT_DEVICE_SDL_GAMECONTROLLER)
			continue;
		buttons |= iter->buttons;
		for (int i = 0; i < CONTROLLER_AXIS__COUNT; i++)
		{
			if ((iter->axis[i] > 3276) || (iter->axis[i] < -3276))
				axis[i] = iter->axis[i];
		}
	}

	auto& io = ImGui::GetIO();

	// home button => toggle the game list + pause/unpause the game
	if ((buttons & CONTROLLER_BUTTON_GUIDE) || io.KeysDown[SDL_SCANCODE_ESCAPE])
	{
		if (!homeFrame)
			HomeGuide(false);
		else if (homeFrame == xsettings.guide_hold_frames)
			HomeGuide(true);
		// io.NavInputs[ImGuiNavInput_Menu] = 1.0f;
		++homeFrame;
	}
	else
		homeFrame = 0;

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

		MAP_ANALOG(ImGuiNavInput_LStickLeft, CONTROLLER_AXIS_LSTICK_X, -thumb_dead_zone, -32768);
		MAP_ANALOG(ImGuiNavInput_LStickRight, CONTROLLER_AXIS_LSTICK_X, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickUp, CONTROLLER_AXIS_LSTICK_Y, +thumb_dead_zone, +32767);
		MAP_ANALOG(ImGuiNavInput_LStickDown, CONTROLLER_AXIS_LSTICK_Y, -thumb_dead_zone, -32767);
	}

	if (selectedInput && selectedType == INPUT_DEVICE_SDL_KEYBOARD)
		io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	else
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ProcessShortcuts();
}

} // namespace ui
