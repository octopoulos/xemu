// extract-xiso.h
// @2021 octopoulos

#pragma once

#include <string>

namespace extract_iso
{
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = __int32;
using s64 = __int64;

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
	std::string date;
	std::string id;
	std::string region;
	std::string title;
	char buffer[256];
};

int CreateXiso(std::string in_root_directory, std::string in_output_directory, std::string in_name);
int DecodeXiso(
	std::string in_xiso, std::string in_path, modes in_mode, std::string* out_iso_path, bool in_ll_compat,
	GameInfo* gameInfo);
bool ExtractGameInfo(std::string filename, GameInfo* gameInfo, bool log);
int ExtractMetadata(int in_xiso, GameInfo* gameInfo);
void PrintHexBytes(char* buffer, int count, size_t offset, bool showHeader);
int ScanFolder(std::string folder);
int VerifyXiso(int in_xiso, s32* out_root_dir_sector, s32* out_root_dir_size, std::string in_iso_name);
} // namespace extract_iso
