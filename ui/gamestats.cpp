// gamestats.cpp
// @2021 octopoulos

#include <chrono>
#include <fstream>
#include <map>

#include <fmt/core.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include "gamestats.h"
#include "xsettings.h"
#include "xemu-notifications.h"

extern "C" {
#include "noc_file_dialog.h"
}

const char* paused_file_open(int flags, const char* filters, const char* default_path, const char* default_name);

#define JSON_GET_INT(name) \
	if (obj.HasMember(#name)) name = obj[#name].GetInt()
#define JSON_GET_STRING(name) \
	if (obj.HasMember(#name)) name = obj[#name].GetString()
#define JSON_SAVE_INT(name) \
	writer->String(#name); \
	writer->Int(name)
#define JSON_SAVE_STRING(name) \
	writer->String(#name); \
	writer->String(name.c_str())

namespace ui
{
struct GameStats : public exiso::GameInfo
{
	int         compatibility = 0;
	int         countPlay     = 0; // # of times the game was launched (counts at 600 frames)
	int         icon          = 0; // taken from a screenshot
	int         timePlay      = 0; // seconds of play time
	std::string lastPlay;          // YYYY-MM-DD hh:mm

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
		MakeBuffer();
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
		JSON_GET_INT(icon);
		JSON_GET_INT(timePlay);
		JSON_GET_STRING(lastPlay);

		MakeBuffer();
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
		JSON_SAVE_INT(icon);
		JSON_SAVE_INT(timePlay);
		JSON_SAVE_STRING(lastPlay);
		writer->EndObject();
	}
};

std::map<std::string, GameStats> gameStats;

void GamesWindow::Draw()
{
	if (!is_open)
		return;

	if (!ImGui::Begin("Games", &is_open))
	{
		ImGui::End();
		return;
	}

	// recent
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(16.0f, 16.0f));

	if (ImGui::BeginTable("Table", 9, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
	{
		ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Release Date", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Count Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Last Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Time Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Compatibility", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableHeadersRow();

		for (const auto& [key, game] : gameStats)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted("ICON");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(game.title.c_str());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(game.id.c_str());
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(game.region.c_str());
			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(game.date.c_str());
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%d", game.countPlay);
			ImGui::TableSetColumnIndex(6);
			ImGui::TextUnformatted(game.lastPlay.c_str());

			ImGui::TableSetColumnIndex(7);
            int seconds = game.timePlay;
            if (seconds > 0)
            {
                int minutes = seconds / 60;
                int hours = minutes / 60;
                ImGui::Text("%02d:%02d:%02d", hours, minutes % 60, seconds % 60);
            }
            else
                ImGui::TextUnformatted("");

			ImGui::TableSetColumnIndex(8);
			ImGui::Text("%d", game.compatibility);
		}

		ImGui::EndTable();
	}

	ImGui::PopStyleVar();

	// saved
	ImGui::End();
}

/**
 * Signal that a game was loaded
 * @param key empty to signal the game was closed, must be called when closing the app
 */
void LoadedGame(std::string key)
{
	static std::string gameUid;
	static auto        start = std::chrono::steady_clock::now();

	bool mustSave = false;
    fmt::print(stderr, "LoadedGame, key={} gameUid={}\n", key, gameUid);

	// close game => count the play time
	if (gameUid.size())
	{
		auto finish  = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(finish - start).count();
        fmt::print(stderr, "elapsed={} gameUid={}\n", elapsed, gameUid);

		if (elapsed > 10 && gameStats.contains(gameUid))
		{
			gameStats[gameUid].timePlay += elapsed;
			mustSave = true;
		}
	}

	if (key.size() && gameStats.contains(key))
	{
		auto& gameStat = gameStats[key];

		auto    now   = std::chrono::system_clock::now();
		auto    timer = std::chrono::system_clock::to_time_t(now);
		std::tm gmtime;
		gmtime_s(&gmtime, &timer);

		++gameStat.countPlay;
		gameStat.lastPlay = fmt::format("{}-{:02}-{:02} {:02}:{:02}", 1900 + gmtime.tm_year, 1 + gmtime.tm_mon, gmtime.tm_mday, gmtime.tm_hour, gmtime.tm_min);

		mustSave = true;
		start    = std::chrono::steady_clock::now();
	}

	if (mustSave)
		SaveGamesList();

    fmt::print(stderr, "~LoadedGame, key={} gameUid={} mustSave={}\n", key, gameUid, mustSave);
	gameUid = key;
}

void OpenGamesList()
{
	std::ifstream             ifs(fmt::format("{}/games.json", xsettingsFolder(0)));
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

/**
 * Save games list
 */
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

	std::ofstream ofs(fmt::format("{}games.json", xsettingsFolder(0)));
	ofs << ss.GetString();
	ofs.flush();
	ofs.close();
}

void ScanGamesFolder()
{
	const char* filters  = ""; // ".iso Files\0*.iso\0All Files\0*.*\0";
	const char* current  = xsettings.dvd_path;
	const char* filename = paused_file_open(NOC_FILE_DIALOG_OPEN, filters, current, nullptr);

	std::string folder = filename;
	folder.erase(folder.find_last_of("/\\") + 1);
	xemu_queue_notification(fmt::format("Scanning {}", folder).c_str(), true);

	auto gameInfos = exiso::ScanFolder(folder, true);

	for (auto& gameInfo : gameInfos)
	{
		auto it = gameStats.find(gameInfo.uid);
		if (it == gameStats.end())
		{
			GameStats gameStat = gameInfo;
			gameStats[gameInfo.uid] = gameStat;
		}
        else
            it->second.path = gameInfo.path;
	}

	SaveGamesList();
}

} // namespace ui
