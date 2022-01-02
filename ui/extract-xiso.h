// extract-xiso.h
// @2021 octopoulos
//
// Original extract-xiso.c @2003 in <in@fishtank.com>

#pragma once

#include <string>
#include <vector>

namespace exiso
{
enum class modes
{
	generate_avl,
	exe,
	extract,
	list,
	rewrite,
	title,
};

struct GameInfo
{
	char        buffer[256] = { 0 }; // cache data used in the window title
	int         extract     = 0;     // &1: get logo
	bool        debug;               // *
	std::string date;                // YYYY-MM-DD
	std::string id;                  // XX-123
	std::string key;                 // unique identifier: XX-123-AJ*
	std::string path;                // iso location
	std::string region;              // A,J,E combinations
	std::string title;               // Full Game Title
    std::string uid;                 // Full Game Title (XX-123-AJ*)

	void CreateBufferUID();
};

int  CreateXiso(std::string in_root_directory, std::string in_output_directory, std::string in_name, bool force);
int  DecodeXiso(std::string filename, std::string in_path, modes in_mode, std::string* out_iso_path, bool in_ll_compat, int extract, GameInfo* gameInfo);
bool ExtractGameInfo(std::string filename, GameInfo* gameInfo, bool log);
int  ExtractMetadata(int ifile, GameInfo* gameInfo);
void PrintHexBytes(char* buffer, int count, size_t offset, bool showHeader);
int  VerifyXiso(int ifile, int* out_root_dir_sector, int* out_root_dir_size, std::string in_iso_name);

std::vector<GameInfo> ScanFolder(std::string folder, bool log);
} // namespace exiso
