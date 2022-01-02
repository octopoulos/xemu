// gamestats.cpp
// @2021 octopoulos

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

#include <fmt/core.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include "common.h"
#include "gamestats.h"
#include "xsettings.h"
#include "xemu-hud.h"
#include "xemu-notifications.h"
#include "xemu-shaders.h"

extern "C" {
#include "noc_file_dialog.h"
}

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
	int         countPlay     = 0;     // # of times the game was launched (counts at 600 frames)
	int         icon          = 0;     // &1: checked, &2: found
	GLuint      iconTexture   = 0;     //
	std::string lastPlay      = "";    // YYYY-MM-DD hh:mm
	int         timePlay      = 0;     // seconds of play time

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

        std::filesystem::path iconPath = xsettingsFolder(nullptr);
        iconPath /= (uid + ".png");
        if (std::filesystem::exists(iconPath))
        {
            iconTexture = load_texture_from_file(iconPath.string().c_str(), 0);
            icon = 3;
        }
        else
            icon = 1;
    }
};

std::map<std::string, GameStats> gameStats;
std::map<std::string, uint32_t>  textures;

static std::vector<std::string> barNames = {
	"Config",
	"FullScr",
	"FullScr2",
	"Grid",
	"List",
	"Open",
	"Pads",
	"Pause",
	"Refresh",
	"Restart",
	"Start",
	"Stop",
};

/**
 * Load the icons for the top bar
 */
void GamesWindow::Initialize()
{
	std::filesystem::path basePath = xsettingsFolder(nullptr);

	for (auto& barName : barNames)
	{
		std::filesystem::path path = basePath / "bar" / (barName + ".png");
		// path /= "bar";
		// path /= barName;
		// path += ".png";
		fmt::print(stderr, "barName={}\n", path.string().c_str());
		if (std::filesystem::exists(path))
		{
			auto texId        = load_texture_from_file(path.string().c_str(), 0);
			textures[barName] = texId;
		}
	}
}

bool ImageTextButton(std::string name)
{
	static ImVec2 buttonSize(32.0f, 32.0f);
	// static ImVec2 uv0(0.0f, 0.0f);
	// static ImVec2 uv1(1.0f, 1.0f);
    // static ImVec4 colorBack(1.0f, 1.0f, 1.0f, 1.0f);
    // static ImVec4 colorTex(0.0f, 0.0f, 0.0f, 1.0f);

	bool click;
    if (textures.contains(name))
        click = ImGui::ImageButton((void*)(intptr_t)textures[name], buttonSize);    //, uv0, uv1, -1, colorBack, colorTex);
    else
        click = ImGui::Button(name.c_str());

    return click;
}

void GamesWindow::Draw()
{
	if (!is_open)
		return;

	static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

	if (!ImGui::Begin("Games", &is_open, flags))
	{
		ImGui::End();
		return;
	}

	if (ImageTextButton("Open")) LoadDisc();
	ImGui::SameLine();
	if (ImageTextButton("Refresh")) ScanGamesFolder();
	ImGui::SameLine();
	if (ImageTextButton("FullScr")) {}
	ImGui::SameLine();
	if (ImageTextButton("Stop")) {}
	ImGui::SameLine();
	if (ImageTextButton(IsRunning() ? "Pause" : "Start")) TogglePause();
	ImGui::SameLine();
	if (ImageTextButton("Config")) OpenConfig(1);
	ImGui::SameLine();
	if (ImageTextButton("Pads")) OpenConfig(10);
	ImGui::SameLine();
	if (ImageTextButton("List")) {}
	ImGui::SameLine();
	if (ImageTextButton("Grid")) {}
	ImGui::SameLine();
	ImGui::SliderInt("Scale", &xsettings.row_height, 24, 176);
	// ImGui::SameLine();
	// ImGui::InputText("Search", search, sizeof(str2k));

	float icon_height = xsettings.row_height * 1.0f;
	ImVec2 icon_dims = { icon_height * 16.0f / 9.0f, icon_height };

	// recent
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 8.0f));

	if (ImGui::BeginTable("Table", 9, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable))
	{
		ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Serial", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Release Date", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Count Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Last Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Time Played", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("Compatibility", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (auto& [key, game] : gameStats)
		{
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
            game.CheckIcon();
			if (game.icon & 2)
				ImGui::Image((void*)(intptr_t)game.iconTexture, icon_dims);
			else
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
			ImGui::Text("%s", game.compatibility? "Playable": "No results found");
		}

		ImGui::EndTable();
	}

	ImGui::PopStyleVar();

	// saved
	ImGui::End();
}

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
	const char* filename = PausedFileOpen(NOC_FILE_DIALOG_OPEN, filters, current, nullptr);

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
        {
            GameStats& gameStat = it->second;
            gameStat.path = gameInfo.path;
            gameStat.CheckIcon();
        }
	}

	SaveGamesList();
}

} // namespace ui
