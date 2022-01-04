// ui-settings.cpp
// @2022 octopoulos

#include "ui-settings.h"
#include "xemu-notifications.h"

#include <SDL.h>

extern "C"
{
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "hw/xbox/nv2a/nv2a.h"
}

extern SDL_Window* m_window;
extern bool        g_trigger_style_update;

namespace ui
{
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
    tabMenu = tabMenu_;
    isOpen = true;
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

	const char* sRenderers[] = { "DX9", "DX11", "OpenGL", "Vulkan", "Null" };
	ImGui::Combo("Renderer", &xsettings.renderer, sRenderers, 5);

	if (ImGui::SliderInt("Resolution Scale", &xsettings.resolution_scale, 1, 10, "%dx"))
        nv2a_set_surface_scale_factor(xsettings.resolution_scale);

    const char* sAspectRatios[] = { "16:9", "4:3", "Native", "Window" };
	ImGui::Combo("Aspect Ratio", &xsettings.aspect_ratio, sAspectRatios, 4);

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

		if (ImGui::Combo("Anisotropic Filtering", &anisotropic, sAnisotropics, 6))
			xsettings.anisotropic = anisotropics[anisotropic];
	}

	const char* off_on_auto[] = { "Off", "On", "Auto" };
	ImGui::Combo("Dither", &xsettings.dither, off_on_auto, 3);
	ImGui::Combo("Line Smooth", &xsettings.line_smooth, off_on_auto, 3);
	ImGui::Combo("Polygon Smooth", &xsettings.polygon_smooth, off_on_auto, 3);
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

void SettingsWindow::DrawPads()
{
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

		if (ImGui::Combo("###mem", &memory, sMemories, 2))
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
    float prev_scale = xsettings.ui_scale;
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

	for (auto& [ name, buffer ] : names)
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

static SettingsWindow settingsWindow;

SettingsWindow& GetSettingsWindow() { return settingsWindow; }
void            OpenConfig(int tab) { settingsWindow.OpenTab(tab); }

} // namespace ui
