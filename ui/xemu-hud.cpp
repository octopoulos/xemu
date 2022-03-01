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

#include <cmath>
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

#include "xemu-hud.h"
#include "xemu-input.h"
#include "xemu-notifications.h"
#include "xsettings.h"
#include "xemu-shaders.h"
#include "xemu-custom-widgets.h"
#include "xemu-monitor.h"
#include "shuriken-version.h"
#include "xemu-net.h"
#include "xemu-os-utils.h"
#include "xemu-xbe.h"
#include "xemu-reporting.h"

#if defined(_WIN32)
#	include "xemu-update.h"
#endif

#include "imgui/backends/imgui_impl_sdl.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "implot/implot.h"

#include "hw/xbox/nv2a/intercept.h"
#include "ui.h"

extern "C" {
// #include "qemui/noc_file_dialog.h"

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

extern FBO* logo_fbo;

bool    g_trigger_style_update = true;

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
			window_pos.y            = ui::GetMenuHeight() + DISTANCE;
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

		ImGui::PopStyleColor(3);
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

class MonitorWindow : public ui::CommonWindow
{
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
		name           = "Monitor";
		hidden         = 2;
		HistoryPos     = -1;
		AutoScroll     = true;
		ScrollToBottom = false;
	}

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
		CHECK_DRAW();
		int    style_pop_cnt = PushWindowTransparencySettings(true);
		auto&  io            = ImGui::GetIO();
		ImVec2 window_pos    = ImVec2(0, io.DisplaySize.y / 2);

		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y / 2), ImGuiCond_Appearing);
		if (ImGui::Begin("Monitor", &isOpen, ImGuiWindowFlags_NoCollapse))
		{
			const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();             // 1 separator, 1 input text
			ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar); // Leave room for 1 separator + 1 InputText

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
			ImGui::PushFont(ui::FindFont("mono"));
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
			ImGui::PushFont(ui::FindFont("mono"));
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

	void toggle_open() { isOpen = !isOpen; }

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

class AboutWindow : public ui::CommonWindow
{
private:
	char build_info_text[256];

public:
	AboutWindow()
	{
		name   = "About";
		hidden = 2;

		snprintf(
		    build_info_text, sizeof(build_info_text),
		    "Version: %s\n"
		    "Branch:  %s\n"
		    "Commit:  %s\n"
		    "Date:    %s",
		    shuriken_version, shuriken_branch, shuriken_commit, shuriken_date);
		// FIXME: Show platform
		// FIXME: Show driver
		// FIXME: Show BIOS/BootROM hash
	}

	void Draw()
	{
		CHECK_DRAW();
		ImGui::SetNextWindowContentSize(ImVec2(400.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("About", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
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
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(shuriken_version).x) / 2);
		ImGui::Text("%s", shuriken_version);

		ImGui::SetCursorPosX(10 * xsettings.ui_scale);
		ui::AddSpace(20);

		const char* msg = "Visit https://xemu.app for more information";
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(msg).x) / 2);
		Hyperlink(msg, "https://xemu.app");

		ui::AddSpace(40);

		ImGui::PushFont(ui::FindFont("mono"));
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

class NetworkWindow : public ui::CommonWindow
{
public:
	int  backend;
	char remote_addr[64];
	char local_addr[64];

	std::unique_ptr<NetworkInterfaceManager> iface_mgr;

	NetworkWindow() { name = "Network"; }

	void Draw()
	{
		CHECK_DRAW();
		ImGui::SetNextWindowContentSize(ImVec2(500.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Network", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
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
		if (ImGui::Combo("##backend", is_enabled ? &temp_backend : &backend, sNetBackends, IM_ARRAYSIZE(sNetBackends)) && !is_enabled)
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
				ui::AddSpace(20);
				const char* msg = "WinPcap/npcap library could not be loaded.\nTo use this attachment, please install npcap.";
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetColumnWidth() - xsettings.ui_scale * ImGui::CalcTextSize(msg).x) / 2);
				ImGui::Text("%s", msg);
				ui::AddSpace(10);
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120 * xsettings.ui_scale) / 2);
				if (ImGui::Button("Install npcap", ImVec2(120 * xsettings.ui_scale, 0)))
					xemu_open_web_browser("https://nmap.org/npcap/");
				ui::AddSpace(10);
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

		ui::AddSpace(20);
		ui::AddSpace();
		ImGui::Separator();
		ui::AddSpace();

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

class DebugApuWindow : public ui::CommonWindow
{
public:
	DebugApuWindow()
	{
		name   = "Audio";
		hidden = 2;
	}

	void Draw()
	{
		CHECK_DRAW();
		ImGui::SetNextWindowContentSize(ImVec2(600.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Audio Debug", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
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
		float c_active   = 0.4f * (1.0f - v) + (0.97f - 0.4f) * v;
		float c_inactive = 0.2f;

		int voice_monitor = -1;
		int voice_info    = -1;
		int voice_mute    = -1;

		// Color buttons, demonstrate using PushID() to add unique identifier in the ID stack, and changing style.
		ImGui::PushFont(ui::FindFont("mono"));
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
			ImGui::PushFont(ui::FindFont("mono"));

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

		ImGui::PushFont(ui::FindFont("mono"));
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
		if (ImGui::Combo("Monitor", &mon, sMonitors, IM_ARRAYSIZE(sMonitors)))
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

class DebugVideoWindow : public ui::CommonWindow
{
public:
	bool transparent = false;

	DebugVideoWindow()
	{
		name   = "Video";
		hidden = 2;
	}

	void Draw()
	{
		CHECK_DRAW();
		float alpha = transparent ? 0.2 : 1.0;
		PushWindowTransparencySettings(transparent, 0.2);
		ImGui::SetNextWindowSize(ImVec2(600.0f * xsettings.ui_scale, 150.0f * xsettings.ui_scale), ImGuiCond_Once);
		if (ImGui::Begin("Video Debug", &isOpen))
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
class AutoUpdateWindow : public ui::CommonWindow
{
protected:
	Updater updater;

public:
	bool should_prompt_auto_update_selection = false;

	AutoUpdateWindow() { name = "Update"; }

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

		ui::AddSpace();
		ImGui::Separator();
		ui::AddSpace();

		float w  = (130) * xsettings.ui_scale;
		float bw = w + (10) * xsettings.ui_scale;
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 2 * bw);

		if (ImGui::Button("No", ImVec2(w, 0)))
		{
			save_auto_update_selection(false);
			isOpen = false;
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
		updater.check_for_update([this]() { isOpen |= updater.is_update_available(); });
	}

	void Draw()
	{
		CHECK_DRAW();
		ImGui::SetNextWindowContentSize(ImVec2(550.0f * xsettings.ui_scale, 0.0f));
		if (!ImGui::Begin("Update", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
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

		ui::AddSpace();
		ImGui::Separator();
		ui::AddSpace();

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
static DebugApuWindow        apu_window;
static DebugVideoWindow      video_window;
static MonitorWindow         monitor_window;
static NetworkWindow         network_window;
static NotificationManager   notification_manager;
#if defined(_WIN32)
static AutoUpdateWindow update_window;
#endif

#ifdef CONFIG_RENDERDOC
static bool capture_renderdoc_frame = false;
#endif

static bool is_shortcut_key_pressed(int scancode, bool isAlt)
{
	auto&      io              = ImGui::GetIO();
	const bool is_osx          = io.ConfigMacOSXBehaviors;
	// OS X style: Shortcuts using Cmd/Super instead of Ctrl
	const bool is_shortcut_key = isAlt ? io.KeyAlt : ((is_osx ? (io.KeySuper && !io.KeyCtrl) : (io.KeyCtrl && !io.KeySuper)) && !io.KeyAlt && !io.KeyShift);
	return is_shortcut_key && io.KeysDown[scancode] && (io.KeysDownDuration[scancode] == 0.0);
}

static bool is_key_pressed(int scancode)
{
	auto& io = ImGui::GetIO();
	return io.KeysDown[scancode] && (io.KeysDownDuration[scancode] == 0.0);
}

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

	ImPlot::CreateContext();

#if defined(_WIN32)
	int should_check_for_update = xsettings.check_for_update;
	if (should_check_for_update == -1)
		update_window.should_prompt_auto_update_selection = update_window.isOpen = !xsettingsFailed();
	else if (should_check_for_update)
		update_window.check_for_updates_and_prompt_if_available();
#endif

	ui::ListWindows();
}

void xemu_hud_cleanup()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
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

	// Prevent controller events from going to the guest if they are being used to navigate the HUD
	xemu_input_set_test_mode(controller_focus_capture);

	if (g_trigger_style_update)
	{
		ui::UpdateFonts();
		ui::UpdateTheme();
		g_trigger_style_update = false;
	}

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();

	ui::UpdateIO();

	ImGui::NewFrame();

#if defined(DEBUG_NV2A_GL) && defined(CONFIG_RENDERDOC)
    if (capture_renderdoc_frame)
	{
        nv2a_dbg_renderdoc_capture_frames(1);
        capture_renderdoc_frame = false;
    }
#endif

	{
		// Auto-hide main menu after 3s of inactivity
		static uint32_t last_check    = 0;
		float           alpha         = 1.0f;
		const uint32_t  timeout       = 3000;
		const float     fade_duration = 1000.0f;
		if (ui_wakeup) last_check = now;

		if ((now - last_check) > timeout)
		{
			float t = fmin((float)((now - last_check) - timeout) / fade_duration, 1.0);
			alpha   = 1.0 - t;
			if (t >= 1.0) alpha = 0.0;
		}

		auto controlsWindow  = ui::GetControlsWindow();
		controlsWindow.alpha = alpha;
		// controlsWindow.isOpen = (alpha > 0.0f);

		ui::ShowMainMenu(alpha);
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockSpaceOverViewport(viewport, ImGuiDockNodeFlags_PassthruCentralNode);

	network_window.Draw();
	notification_manager.Draw();
#if defined(_WIN32)
	update_window.Draw();
#endif

	ui::DrawWindows();

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void xemu_queue_notification(const char* msg, bool instant)
{
	notification_manager.QueueNotification(msg, instant);
}

namespace ui
{

CommonWindow& GetAboutWindow() { return about_window; }
CommonWindow& GetAudioWindow() { return apu_window; }
CommonWindow& GetMonitorWindow() { return monitor_window; }
CommonWindow& GetVideoWindow() { return video_window; }

} // namespace ui
