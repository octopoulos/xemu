// ui-games.cpp
// @2022 octopoulos
//
// This file is part of Shuriken.
// Foobar is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
// Shuriken is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Shuriken. If not, see <https://www.gnu.org/licenses/>.

#include <unordered_set>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include "ui.h"
#include "xemu-notifications.h"

extern "C" {
#include "qemui/noc_file_dialog.h"
}

#define JSON_GET_INT(name) \
	if (obj.HasMember(#name)) name = obj[#name].GetInt()
#define JSON_GET_STRING(name) \
	if (obj.HasMember(#name)) name = obj[#name].GetString()
#define JSON_SAVE_INT(name) \
	writer->String(#name);  \
	writer->Int(name)
#define JSON_SAVE_STRING(name) \
	writer->String(#name);     \
	writer->String(name.c_str())

namespace ui
{

struct GameStats : public exiso::GameInfo
{
	int         compatibility = 0;
	int         countPlay     = 0;  // # of times the game was launched (counts at 600 frames)
	int         icon          = 0;  // &1: checked, &2: found
	uint32_t    iconTexture   = 0;  //
	std::string lastPlay      = ""; // YYYY-MM-DD hh:mm
	int         timePlay      = 0;  // seconds of play time

	GameStats() {}

	GameStats(const exiso::GameInfo& info)
	{
		date   = info.date;
		debug  = info.debug;
		id     = info.id;
		key    = info.key;
		path   = info.path;
		region = info.region;
		title  = info.title;
		uid    = info.uid;

		CreateBufferUID();
		CheckIcon();
	}

	void Deserialize(const rapidjson::Value& obj)
	{
		// info
		JSON_GET_INT(debug);
		JSON_GET_STRING(date);
		JSON_GET_STRING(id);
		JSON_GET_STRING(key);
		JSON_GET_STRING(path);
		JSON_GET_STRING(region);
		JSON_GET_STRING(title);
		// stats
		JSON_GET_INT(compatibility);
		JSON_GET_INT(countPlay);
		JSON_GET_INT(timePlay);
		JSON_GET_STRING(lastPlay);

		CreateBufferUID();
	}

	void Serialize(rapidjson::Writer<rapidjson::StringBuffer>* writer) const
	{
		writer->StartObject();
		// info
		JSON_SAVE_INT(debug);
		JSON_SAVE_STRING(date);
		JSON_SAVE_STRING(id);
		JSON_SAVE_STRING(key);
		JSON_SAVE_STRING(path);
		JSON_SAVE_STRING(region);
		JSON_SAVE_STRING(title);
		// stats
		JSON_SAVE_INT(compatibility);
		JSON_SAVE_INT(countPlay);
		JSON_SAVE_INT(timePlay);
		JSON_SAVE_STRING(lastPlay);
		writer->EndObject();
	}

	void CheckIcon()
	{
		if (icon & 1)
			return;

		auto iconPath = xsettingsFolder() / "icons" / (uid + ".png");
		iconTexture   = LoadTexture(iconPath, uid);
		icon          = iconTexture ? 3 : 1;
	}
};

std::map<std::string, GameStats> gameStats;

/**
 * Add a cell in columns mode
 */
static void AddCell(int col, float posy, std::string text)
{
	ImGui::TableSetColumnIndex(col);
	ImGui::SetCursorPosY(posy);
	ImGui::TextUnformatted((" " + text + "      ").c_str());
}

/**
 * Left/right click action
 */
static void CheckClicks(std::string key, GameStats& game)
{
	if (ImGui::IsItemClicked(0))
	{
		static auto lastTime = std::chrono::steady_clock::now();
		auto        nowTime  = std::chrono::steady_clock::now();
		auto        elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - lastTime).count();

		if (elapsed > 0)
		{
			lastTime = nowTime;
			if (elapsed < 300)
			{
				lastTime += std::chrono::milliseconds(700);
				if (LoadDisc(game.path, true))
				{
					if (xsettings.run_no_ui) ShowWindows(0);
					TogglePause(1);
				}
			}
		}
	}
	if (ImGui::IsItemClicked(1))
	{
		Log("right clicked on %s", key.c_str());
		if (ImGui::BeginPopupContextItem()) // <-- use last item id as popup id
		{
			ImGui::Text("This a popup for \"%s\"!", key);
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
}

/**
 * Selectable for grid/columns
 */
static float Selectable(bool isGrid, std::string key, GameStats& game, float height)
{
	static std::unordered_set<std::string> selection;

	ImGuiSelectableFlags flags = ImGuiSelectableFlags_None | ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_SelectOnClick;
	if (!isGrid) flags |= ImGuiSelectableFlags_SpanAllColumns;

	float y = ImGui::GetCursorPosY();
	if (ImGui::Selectable("", selection.contains(key), flags, ImVec2(0.0f, height)))
	{
		selection.clear();
		selection.insert(key);
	}
	CheckClicks(key, game);
	ImGui::SetCursorPosY(y);
	return y;
}

// CLASS
////////

class GamesWindow : public CommonWindow
{
public:
	bool isGrid = false;

	GamesWindow() { isOpen = manualOpen = true; }

	void Draw()
	{
		if (!isOpen)
			return;

		if (!drawn)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			auto&                size     = viewport->WorkSize;
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + size.x * 0.1f, viewport->WorkPos.y + size.y * 0.1f));
			ImGui::SetNextWindowSize(ImVec2(size.x * 0.8f, size.y * 0.8f));

			OpenGamesList();
			++drawn;
		}

		if (!ImGui::Begin("Game List", &isOpen))
		{
			ImGui::End();
			return;
		}

		float  iconHeight = xsettings.row_height * 1.0f;
		ImVec2 iconDims   = { iconHeight * 16.0f / 9.0f, iconHeight };
		float  textHeight = ImGui::GetFontSize();

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));

		// grid display
		if (isGrid)
		{
			ImVec2      childDims         = { iconDims.x, iconDims.y };
			ImGuiStyle& style             = ImGui::GetStyle();
			float       window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

			if (childDims.x > 128.0f)
				childDims.y += textHeight + style.ItemSpacing.y * 2;

			for (auto& [key, game] : gameStats)
			{
				ImGui::BeginChild(key.c_str(), childDims);
				ImGui::PushID(key.c_str());

				if (focus)
				{
					ImGui::SetKeyboardFocusHere();
					focus = 0;
				}

				// selectable + icon
				Selectable(true, key, game, childDims.y);
				game.CheckIcon();
				if (game.icon & 2)
					ImGui::Image((void*)(intptr_t)game.iconTexture, iconDims);
				else
					ImGui::TextUnformatted("ICON");

				// text
				if (childDims.x > 128.0f)
				{
					auto title = game.title.c_str();
					if (float offset = (childDims.x - ImGui::CalcTextSize(title).x) / 2; offset > 0)
						ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
					ImGui::TextUnformatted(title);
				}
				ImGui::PopID();
				ImGui::EndChild();

				float last_x2 = ImGui::GetItemRectMax().x;
				float next_x2 = last_x2 + style.ItemSpacing.x / 2 + childDims.x;
				if (next_x2 < window_visible_x2)
					ImGui::SameLine();
			}
		}
		// column display
		else
		{
			static ImGuiTableFlags tFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg
				| ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;

			if (ImGui::BeginTable("Table", 9, tFlags))
			{
				ImGui::TableSetupScrollFreeze(1, 1);

				ImGui::TableSetupColumn(" Icon      ", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, iconDims.x);
				ImGui::TableSetupColumn(" Name      ", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn(" Serial      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Region      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Release Date      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Count Played      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Last Played      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Time Played      ", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn(" Compatibility      ", ImGuiTableColumnFlags_WidthStretch);

				ImGui::TableHeadersRow();

				float offset = (iconDims.y - textHeight) / 2;

				for (auto& [key, game] : gameStats)
				{
					ImGui::PushID(key.c_str());
					ImGui::TableNextRow();

					// selectable + icon
					ImGui::TableSetColumnIndex(0);
					float posy = Selectable(false, key, game, iconDims.y) + offset;
					game.CheckIcon();
					if (game.icon & 2)
						ImGui::Image((void*)(intptr_t)game.iconTexture, iconDims);
					else
						ImGui::TextUnformatted("ICON");

					// all columns
					AddCell(1, posy, game.title);
					AddCell(2, posy, game.id);
					AddCell(3, posy, game.region);
					AddCell(4, posy, game.date);
					AddCell(5, posy, std::to_string(game.countPlay));
					AddCell(6, posy, game.lastPlay);

					if (int seconds = game.timePlay; seconds > 0)
					{
						int minutes = seconds / 60;
						int hours   = minutes / 60;
						AddCell(7, posy, fmt::format("{:02}:{:02}:{:02}", hours, minutes % 60, seconds % 60));
					}
					else
						AddCell(7, posy, "");

					AddCell(8, posy, game.compatibility ? "Playable" : "No results found");
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		ImGui::PopStyleVar();

		// saved
		ImGui::End();
	}

	/**
	 * Set the grid + activate/deactive window
	 */
	void SetGrid(bool grid)
	{
		isOpen = (isGrid == grid) ? !isOpen : true;
		isGrid = grid;
	}
};

static GamesWindow gamesWindow;
CommonWindow&      GetGamesWindow() { return gamesWindow; }

// API
//////

void CheckIcon(std::string uid)
{
	auto it = gameStats.find(uid);
	if (it != gameStats.end())
	{
		it->second.icon = 0;
		it->second.CheckIcon();
	}
}

/**
 * Signal that a game was loaded
 * @param key empty to signal the game was closed, must be called when closing the app
 */
void LoadedGame(std::string uid)
{
	static std::string gameUid;
	static auto        start = std::chrono::steady_clock::now();

	bool mustSave = false;

	// close game => count the play time
	if (gameUid.size())
	{
		auto finish  = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(finish - start).count();

		if (elapsed > 10 && gameStats.contains(gameUid))
		{
			gameStats[gameUid].timePlay += elapsed;
			mustSave = true;
		}
	}

	if (uid.size() && gameStats.contains(uid))
	{
		// new game => update count + last played
		if (uid != gameUid)
		{
			auto& gameStat = gameStats[uid];

			auto    now   = std::chrono::system_clock::now();
			auto    timer = std::chrono::system_clock::to_time_t(now);
			std::tm gmtime;
			gmtime_s(&gmtime, &timer);

			++gameStat.countPlay;
			gameStat.lastPlay = fmt::format("{}-{:02}-{:02} {:02}:{:02}", 1900 + gmtime.tm_year, 1 + gmtime.tm_mon, gmtime.tm_mday, gmtime.tm_hour, gmtime.tm_min);
		}

		mustSave = true;
		start    = std::chrono::steady_clock::now();
	}

	if (mustSave)
		SaveGamesList();

	gameUid = uid;
}

void OpenGamesList()
{
	std::ifstream             ifs((xsettingsFolder() / "games.json").string());
	rapidjson::IStreamWrapper isw(ifs);
	rapidjson::Document       doc;
	doc.ParseStream(isw);

	if (!doc.IsObject())
		doc.SetObject();

	for (auto& it : doc.GetObject())
	{
		GameStats gameStat;
		gameStat.Deserialize(it.value);
		gameStats[it.name.GetString()] = gameStat;
	}
}

void SaveGamesList()
{
	rapidjson::StringBuffer ss;
	rapidjson::PrettyWriter writer(ss);
	writer.SetIndent('\t', 1);

	writer.StartObject();
	for (const auto& [key, game] : gameStats)
	{
		writer.String(key.c_str());
		game.Serialize(&writer);
	}
	writer.EndObject();

	std::ofstream ofs((xsettingsFolder() / "games.json").string());
	ofs << ss.GetString();
	ofs.flush();
	ofs.close();
}

void ScanGamesFolder()
{
	const char* filters  = ".iso Files\0*.iso\0All Files\0*.*\0";
	// const char* current  = xsettings.dvd_path;
	const char* filename = PausedFileOpen(NOC_FILE_DIALOG_OPEN, filters, "", nullptr);

	if (!filename)
		return;

	std::string folder = filename;
	folder.erase(folder.find_last_of("/\\") + 1);
	xemu_queue_notification(fmt::format("Scanning {}", folder).c_str(), true);

	auto gameInfos = exiso::ScanFolder(folder, true);

	for (auto& gameInfo : gameInfos)
	{
		auto it = gameStats.find(gameInfo.uid);
		if (it == gameStats.end())
		{
			GameStats gameStat      = gameInfo;
			gameStats[gameInfo.uid] = gameStat;
		}
		else
		{
			GameStats& gameStat = it->second;
			gameStat.path       = gameInfo.path;
			gameStat.CheckIcon();
		}
	}

	SaveGamesList();
}

void SetGamesGrid(bool grid) { gamesWindow.SetGrid(grid); }

} // namespace ui
