/*
    extract-xiso.cpp
    v1.1

    C++ rewrite WIP @2022 by octopoulos:
        - faster
        - less OS specific code (more portable)
        - has additional features

    TODO:
        - redo path/name extractions with filesystem::path instead
        - add a GUI

    Original extract-xiso.c
    An xdvdfs .iso (xbox iso) file extraction and creation tool by in <in@fishtank.com>
        written March 10, 2003
*/

#if defined(__LINUX__)
#	define _LARGEFILE64_SOURCE
#endif
#if defined(__GNUC__) && !defined(_GNU_SOURCE)
#	define _GNU_SOURCE
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <codecvt>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <source_location>
#include <sys/stat.h>
#include <sys/types.h>

#include <fmt/core.h>
#include "extract-xiso.h"

#define HAS_UI
#ifdef HAS_UI
#	include "ui.h"
#endif

//#define SAVE_PNG
#ifdef SAVE_PNG
#	define STB_IMAGE_WRITE_IMPLEMENTATION
#	include <stb_image_write.h>
#endif

#if defined(__FREEBSD__) || defined(__OPENBSD__)
#	include <machine/limits.h>
#endif

#if defined(_WIN32)
#	include <direct.h>
#else
#	include <limits.h>
#	include <unistd.h>
#endif

#if defined(__DARWIN__)
#	define PATH_CHAR     '/'
#	define PATH_CHAR_STR "/"

#	define FORCE_ASCII    1
#	define READFLAGS      O_RDONLY
#	define WRITEFLAGS     O_WRONLY | O_CREAT | O_TRUNC
#	define READWRITEFLAGS O_RDWR

#elif defined(__FREEBSD__)
#	define PATH_CHAR     '/'
#	define PATH_CHAR_STR "/"

#	define FORCE_ASCII    1
#	define READFLAGS      O_RDONLY
#	define WRITEFLAGS     O_WRONLY | O_CREAT | O_TRUNC
#	define READWRITEFLAGS O_RDWR

#elif defined(__LINUX__)
#	define PATH_CHAR     '/'
#	define PATH_CHAR_STR "/"

#	define FORCE_ASCII    0
#	define READFLAGS      O_RDONLY | O_LARGEFILE
#	define WRITEFLAGS     O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE
#	define READWRITEFLAGS O_RDWR | O_LARGEFILE

#	define lseek lseek64
#	define stat  stat64

#elif defined(_WIN32)
#	define PATH_CHAR     '\\'
#	define PATH_CHAR_STR "\\"

#	define FORCE_ASCII    0
#	define READFLAGS      O_RDONLY | O_BINARY
#	define WRITEFLAGS     O_WRONLY | O_CREAT | O_TRUNC | O_BINARY
#	define READWRITEFLAGS O_RDWR | O_BINARY

#	ifndef S_ISDIR
#		define S_ISDIR(x) ((x)&_S_IFDIR)
#	endif
#	ifndef S_ISREG
#		define S_ISREG(x) ((x)&_S_IFREG)
#	endif

#	define lseek _lseeki64
#endif

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s32 = __int32;
using s64 = __int64;

namespace exiso
{
#define swap16(n) ((n) = ((n) << 8) | ((n) >> 8))
#define swap32(n) ((n) = ((n) << 24) | ((n) << 8 & 0xff0000) | ((n) >> 8 & 0xff00) | ((n) >> 24))

#ifdef USE_BIG_ENDIAN
#	define big16(n)
#	define big32(n)
#	define little16(n) swap16(n)
#	define little32(n) swap32(n)
#else
#	define big16(n) swap16(n)
#	define big32(n) swap32(n)
#	define little16(n)
#	define little32(n)
#endif

#define exiso_version  "1.1 (2022-01-01)"
#define VERSION_LENGTH 16

#ifdef HAS_UI
#	define FMT_ERROR(...) ui::LogError(fmt::format(__VA_ARGS__))
#	define FMT_ONE(...)   ui::Log(fmt::format(__VA_ARGS__))
#	define FMT_PRINT(...) ui::Log(fmt::format(__VA_ARGS__))
#else
#	define FMT_ERROR(...)                   \
		do                                   \
		{                                    \
			fmt::print(stderr, __VA_ARGS__); \
			fmt::print(stderr, "\n");        \
		}                                    \
		while (0)
#	define FMT_ONE(...) fmt::print(__VA_ARGS__);
#	define FMT_PRINT(...)           \
		do                           \
		{                            \
			fmt::print(__VA_ARGS__); \
			fmt::print("\n");        \
		}                            \
		while (0)
#endif

#define XLOG(format, ...)                   \
	do                                      \
	{                                       \
		if (!s_quiet)                       \
			FMT_PRINT(format, __VA_ARGS__); \
	}                                       \
	while (0)

#define XONE(format, ...)                 \
	do                                    \
	{                                     \
		if (!s_quiet)                     \
			FMT_ONE(format, __VA_ARGS__); \
	}                                     \
	while (0)

#define XERROR(format, ...)                 \
	do                                      \
	{                                       \
		if (!s_quieter)                     \
		{                                   \
			FMT_ERROR("{} ", __LINE__);     \
			FMT_ERROR(format, __VA_ARGS__); \
		}                                   \
		err = 1;                            \
	}                                       \
	while (0)

#define GLOBAL_LSEEK_OFFSET 0x0FD90000ul
#define XGD3_LSEEK_OFFSET   0x02080000ul
#define XGD1_LSEEK_OFFSET   0x18300000ul

#define n_sectors(size) ((size) / XISO_SECTOR_SIZE + ((size) % XISO_SECTOR_SIZE ? 1 : 0))

#define XISO_HEADER_DATA        "MICROSOFT*XBOX*MEDIA"
#define XISO_HEADER_DATA_LENGTH 20
constexpr u64 XISO_HEADER_OFFSET = 0x10000;

#define XISO_FILE_MODULUS 0x10000

#define XISO_ROOT_DIRECTORY_SECTOR 0x108

#define XISO_OPTIMIZED_TAG_OFFSET     31337
#define XISO_OPTIMIZED_TAG            "in!xiso!" exiso_version
#define XISO_OPTIMIZED_TAG_LENGTH     (8 + VERSION_LENGTH)
#define XISO_OPTIMIZED_TAG_LENGTH_MIN 7

#define XISO_ATTRIBUTES_SIZE      1
#define XISO_FILENAME_LENGTH_SIZE 1
#define XISO_TABLE_OFFSET_SIZE    2
#define XISO_SECTOR_OFFSET_SIZE   4
#define XISO_DIRTABLE_SIZE        4
#define XISO_FILESIZE_SIZE        4
#define XISO_DWORD_SIZE           4
#define XISO_FILETIME_SIZE        8

constexpr u64 XISO_SECTOR_SIZE = 2048;
#define XISO_UNUSED_SIZE 0x7c8

#define XISO_FILENAME_OFFSET        14
#define XISO_FILENAME_LENGTH_OFFSET (XISO_FILENAME_OFFSET - 1)
#define XISO_FILENAME_MAX_CHARS     255

#define XISO_ATTRIBUTE_RO  0x01
#define XISO_ATTRIBUTE_HID 0x02
#define XISO_ATTRIBUTE_SYS 0x04
#define XISO_ATTRIBUTE_DIR 0x10
#define XISO_ATTRIBUTE_ARC 0x20
#define XISO_ATTRIBUTE_NOR 0x80

#define XISO_PAD_BYTE  0xff
#define XISO_PAD_SHORT 0xffff

#define XISO_MEDIA_ENABLE          "\xe8\xca\xfd\xff\xff\x85\xc0\x7d"
#define XISO_MEDIA_ENABLE_BYTE     '\xeb'
#define XISO_MEDIA_ENABLE_LENGTH   8
#define XISO_MEDIA_ENABLE_BYTE_POS 7

#define EMPTY_SUBDIRECTORY ((dir_node_avl*)1)

constexpr u64 READWRITE_BUFFER_SIZE = 0x00200000;

constexpr char DEBUG_DUMP_DIRECTORY[] = "/Volumes/c/xbox/iso/exiso";
constexpr char DEFAULT_XBE[]          = "default.xbe";

enum class avl_skew
{
	no_skew,
	left_skew,
	right_skew,
};
enum class avl_result
{
	no_err,
	avl_error,
	avl_balanced,
};
enum class avl_traversal_method
{
	prefix,
	infix,
	postfix,
};

enum bm_constants
{
	k_default_alphabet_size = 256,
};

enum errors
{
	err_end_of_sector = -5001,
	err_iso_rewritten = -5002,
	err_iso_no_files  = -5003,
};

typedef void (*progress_callback)(s64 in_current_value, s64 in_final_value);
typedef int (*traversal_callback)(void* in_node, void* in_context, long in_depth);

struct dir_node_avl
{
	size_t        offset           = 0;
	s64           dir_start        = 0;
	u64           file_size        = 0;
	u64           start_sector     = 0;
	dir_node_avl* subdirectory     = nullptr;
	u64           old_start_sector = 0;
	avl_skew      skew             = avl_skew::no_skew;
	dir_node_avl* left             = nullptr;
	dir_node_avl* right            = nullptr;
	std::string   filename;
};

struct dir_node
{
	dir_node*     left;
	dir_node*     parent;
	dir_node_avl* avl_node;
	char          filename[2048];
	u64           r_offset;
	u8            attributes;
	u8            filename_length;
	u64           file_size;
	u32           start_sector;
};

struct FILE_TIME
{
	u32 l;
	u32 h;
};

struct wdsafp_context
{
	s64  dir_start;
	u64* current_sector;
};

struct write_tree_context
{
	int               xiso;
	std::string       path;
	int               from;
	progress_callback progress;
	s64               final_bytes;
};

// TODO: clean this -> move to a struct
static long  s_pat_len;
static bool  s_quiet                = false;
static bool  s_quieter              = false;
static char* s_pattern              = nullptr;
static long* s_gs_table             = nullptr;
static long* s_bc_table             = nullptr;
static s64   s_total_bytes          = 0;
static int   s_total_files          = 0;
static char* s_copy_buffer          = nullptr;
static bool  s_media_enable         = true;
static s64   s_total_bytes_all_isos = 0;
static int   s_total_files_all_isos = 0;
static char  errorBuffer[512]       = { 0 };
static int   max_filename_length    = 0;
static u64   max_filesize           = 0;
static int   max_filesize_length    = 0;

static bool        s_remove_systemupdate = false;
static const char* s_systemupdate        = "$SystemUpdate";

static s64 s_xbox_disc_lseek = 0;

// HELPERS
//////////

#define MEMORY_CHECK(pointer) \
	if (pointer == nullptr) XERROR("{} out of memory error", __LINE__)

std::string PadLeft(std::string str, size_t length, char pad = ' ')
{
	if (length > str.size())
		str.insert(str.begin(), length - str.size(), pad);
	return str;
}

std::string PadRight(std::string str, size_t length, char pad = ' ')
{
	if (length > str.size())
		str.insert(str.end(), length - str.size(), pad);
	return str;
}

bool TryChangeDir(std::string path, int& err, const std::source_location location = std::source_location::current())
{
	if (!err && _chdir(path.c_str()) == -1)
	{
		FMT_ERROR("{}:{}:{} - cannot chdir {}: {}", location.function_name(), location.line(), location.column(), path, _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}
	return !err;
}

bool TryMakeDir(std::string path, int& err, const std::source_location location = std::source_location::current())
{
	if (!err && !std::filesystem::create_directory(path))
	{
		FMT_ERROR("{}:{}:{} - cannot create dir {}: {}", location.function_name(), location.line(), location.column(), path, _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}
	return !err;
}

[[nodiscard]] int TryOpen(std::string path, int flags, int permission, int& err, const std::source_location location = std::source_location::current())
{
	if (err)
		return -1;

	int file = _open(path.c_str(), flags, permission);
	if (file == -1)
	{
		FMT_ERROR("{}:{}:{} - open error {}: {}", location.function_name(), location.line(), location.column(), path, _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}

	return file;
}

inline bool TryRead(int file, char* buffer, int length, int& err, int* counter = nullptr, const std::source_location location = std::source_location::current())
{
	if (err)
		return false;

	int count = _read(file, buffer, (u32)length);

	if (counter)
	{
		*counter = count;
		if (count == -1)
			err = 1;
	}
	else if (count != length)
	{
		FMT_ERROR("{}:{}:{} - read error: {}", location.function_name(), location.line(), location.column(), _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}
	return !err;
}

inline bool TrySeek(int file, s64 pos, int mode, int& err, s64* newPos = nullptr, const std::source_location location = std::source_location::current())
{
	if (err)
		return false;

	s64 result = lseek(file, pos, mode);
	if (newPos)
		*newPos = result;
	if (result == -1)
	{
		FMT_ERROR("{}:{}:{} - seek error: {}", location.function_name(), location.line(), location.column(), _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}
	return !err;
}

inline bool TryWrite(int file, char* buffer, int length, int& err, int* counter = nullptr, const std::source_location location = std::source_location::current())
{
	if (err)
		return false;

	int count = _write(file, buffer, (u32)length);

	if (counter)
	{
		*counter = count;
		if (count == -1)
			err = 1;
	}
	else if (count != length)
	{
		FMT_ERROR("{}:{}:{} - write error: {}", location.function_name(), location.line(), location.column(), _strerror_s(errorBuffer, 512, nullptr));
		err = 1;
	}
	return !err;
}

#define CHANGE_DIR(path)                          TryChangeDir(path, err)
#define MAKE_DIR(path)                            TryMakeDir(path, err)
#define OPEN_FILE(path, flags, permission)        TryOpen(path, flags, permission, err)
#define READ_EXACT(file, buffer, length)          TryRead(file, (char*)(buffer), length, err)
#define READ_SOME(file, buffer, length, counter)  TryRead(file, (char*)(buffer), length, err, counter)
#define SEEK_ABSOLUTE(file, pos)                  TrySeek(file, (s64)(pos), SEEK_SET, err)
#define SEEK_ABSOLUTE_GET(file, pos, newPos)      TrySeek(file, (s64)(pos), SEEK_SET, err, newPos)
#define SEEK_END_GET(file, pos, newPos)           TrySeek(file, (s64)(pos), SEEK_END, err, newPos)
#define SEEK_RELATIVE(file, pos)                  TrySeek(file, (s64)(pos), SEEK_CUR, err)
#define SEEK_RELATIVE_GET(file, pos, newPos)      TrySeek(file, (s64)(pos), SEEK_CUR, err, newPos)
#define WRITE_EXACT(file, buffer, length)         TryWrite(file, (char*)(buffer), length, err)
#define WRITE_SOME(file, buffer, length, counter) TryWrite(file, (char*)(buffer), length, err, counter)

// COMMON
/////////

int avl_compare_key(std::string in_lhs, std::string in_rhs)
{
	auto cin_lhs = (char*)in_lhs.c_str();
	auto cin_rhs = (char*)in_rhs.c_str();

	for (;;)
	{
		char a = *cin_lhs++;
		char b = *cin_rhs++;

		if (a >= 'a' && a <= 'z') a -= 32;
		if (b >= 'a' && b <= 'z') b -= 32;

		if (a)
		{
			if (b)
			{
				if (a < b) return -1;
				if (a > b) return 1;
			}
			else
				return 1;
		}
		else
			return b ? -1 : 0;
	}
}

dir_node_avl* avl_fetch(dir_node_avl* in_root, std::string in_filename)
{
	int result;

	for (;;)
	{
		if (in_root == nullptr)
			return nullptr;

		result = avl_compare_key(in_filename, in_root->filename);

		if (result < 0)
			in_root = in_root->left;
		else if (result > 0)
			in_root = in_root->right;
		else
			return in_root;
	}
}

void avl_rotate_left(dir_node_avl** in_root)
{
	dir_node_avl* tmp = *in_root;

	*in_root         = (*in_root)->right;
	tmp->right       = (*in_root)->left;
	(*in_root)->left = tmp;
}

void avl_rotate_right(dir_node_avl** in_root)
{
	dir_node_avl* tmp = *in_root;

	*in_root          = (*in_root)->left;
	tmp->left         = (*in_root)->right;
	(*in_root)->right = tmp;
}

avl_result avl_left_grown(dir_node_avl** in_root)
{
	switch ((*in_root)->skew)
	{
	case avl_skew::left_skew:
		if ((*in_root)->left->skew == avl_skew::left_skew)
		{
			(*in_root)->skew = (*in_root)->left->skew = avl_skew::no_skew;
			avl_rotate_right(in_root);
		}
		else
		{
			switch ((*in_root)->left->right->skew)
			{
			case avl_skew::left_skew:
			{
				(*in_root)->skew       = avl_skew::right_skew;
				(*in_root)->left->skew = avl_skew::no_skew;
			}
			break;

			case avl_skew::right_skew:
			{
				(*in_root)->skew       = avl_skew::no_skew;
				(*in_root)->left->skew = avl_skew::left_skew;
			}
			break;

			default:
			{
				(*in_root)->skew       = avl_skew::no_skew;
				(*in_root)->left->skew = avl_skew::no_skew;
			}
			break;
			}
			(*in_root)->left->right->skew = avl_skew::no_skew;
			avl_rotate_left(&(*in_root)->left);
			avl_rotate_right(in_root);
		}
		return avl_result::no_err;

	case avl_skew::right_skew:
		(*in_root)->skew = avl_skew::no_skew;
		return avl_result::no_err;

	default:
		(*in_root)->skew = avl_skew::left_skew;
		return avl_result::avl_balanced;
	}
}

avl_result avl_right_grown(dir_node_avl** in_root)
{
	switch ((*in_root)->skew)
	{
	case avl_skew::left_skew:
		(*in_root)->skew = avl_skew::no_skew;
		return avl_result::no_err;

	case avl_skew::right_skew:
		if ((*in_root)->right->skew == avl_skew::right_skew)
		{
			(*in_root)->skew = (*in_root)->right->skew = avl_skew::no_skew;
			avl_rotate_left(in_root);
		}
		else
		{
			switch ((*in_root)->right->left->skew)
			{
			case avl_skew::left_skew:
			{
				(*in_root)->skew        = avl_skew::no_skew;
				(*in_root)->right->skew = avl_skew::right_skew;
			}
			break;

			case avl_skew::right_skew:
			{
				(*in_root)->skew        = avl_skew::left_skew;
				(*in_root)->right->skew = avl_skew::no_skew;
			}
			break;

			default:
			{
				(*in_root)->skew        = avl_skew::no_skew;
				(*in_root)->right->skew = avl_skew::no_skew;
			}
			break;
			}
			(*in_root)->right->left->skew = avl_skew::no_skew;
			avl_rotate_right(&(*in_root)->right);
			avl_rotate_left(in_root);
		}
		return avl_result::no_err;

	default:
		(*in_root)->skew = avl_skew::right_skew;
		return avl_result::avl_balanced;
	}
}

avl_result avl_insert(dir_node_avl** in_root, dir_node_avl* in_node)
{
	if (*in_root == nullptr)
	{
		*in_root = in_node;
		return avl_result::avl_balanced;
	}

	int result = avl_compare_key(in_node->filename, (*in_root)->filename);
	if (result < 0)
	{
		auto tmp = avl_insert(&(*in_root)->left, in_node);
		return (tmp == avl_result::avl_balanced) ? avl_left_grown(in_root) : tmp;
	}
	else if (result > 0)
	{
		auto tmp = avl_insert(&(*in_root)->right, in_node);
		return (tmp == avl_result::avl_balanced) ? avl_right_grown(in_root) : tmp;
	}

	return avl_result::avl_error;
}

int avl_traverse_depth_first(dir_node_avl* in_root, traversal_callback in_callback, void* in_context, avl_traversal_method in_method, long in_depth)
{
	int err;

	if (in_root == nullptr)
		return 0;

	switch (in_method)
	{
	case avl_traversal_method::prefix:
		err = (*in_callback)(in_root, in_context, in_depth);
		if (!err) err = avl_traverse_depth_first(in_root->left, in_callback, in_context, in_method, in_depth + 1);
		if (!err) err = avl_traverse_depth_first(in_root->right, in_callback, in_context, in_method, in_depth + 1);
		break;

	case avl_traversal_method::infix:
		err = avl_traverse_depth_first(in_root->left, in_callback, in_context, in_method, in_depth + 1);
		if (!err) err = (*in_callback)(in_root, in_context, in_depth);
		if (!err) err = avl_traverse_depth_first(in_root->right, in_callback, in_context, in_method, in_depth + 1);
		break;

	case avl_traversal_method::postfix:
		err = avl_traverse_depth_first(in_root->left, in_callback, in_context, in_method, in_depth + 1);
		if (!err) err = avl_traverse_depth_first(in_root->right, in_callback, in_context, in_method, in_depth + 1);
		if (!err) err = (*in_callback)(in_root, in_context, in_depth);
		break;

	default:
		err = 0;
		break;
	}

	return err;
}

int boyer_moore_init(char* in_pattern, long in_pat_len, long in_alphabet_size)
{
	long i, j, k, *backup, err = 0;

	s_pattern = in_pattern;
	s_pat_len = in_pat_len;

	s_bc_table = new long[in_alphabet_size];
	MEMORY_CHECK(s_bc_table);

	if (!err)
	{
		for (i = 0; i < in_alphabet_size; ++i)
			s_bc_table[i] = in_pat_len;
		for (i = 0; i < in_pat_len - 1; ++i)
			s_bc_table[(u8)in_pattern[i]] = in_pat_len - i - 1;

		s_gs_table = new long[2 * (in_pat_len + 1)];
		MEMORY_CHECK(s_gs_table);
	}

	if (!err)
	{
		backup = s_gs_table + in_pat_len + 1;

		for (i = 1; i <= in_pat_len; ++i)
			s_gs_table[i] = 2 * in_pat_len - i;
		for (i = in_pat_len, j = in_pat_len + 1; i; --i, --j)
		{
			backup[i] = j;

			while (j <= in_pat_len && in_pattern[i - 1] != in_pattern[j - 1])
			{
				if (s_gs_table[j] > in_pat_len - i)
					s_gs_table[j] = in_pat_len - i;
				j = backup[j];
			}
		}
		for (i = 1; i <= j; ++i)
			if (s_gs_table[i] > in_pat_len + j - i)
				s_gs_table[i] = in_pat_len + j - i;

		k = backup[j];

		for (; j <= in_pat_len; k = backup[k])
		{
			for (; j <= k; ++j)
				if (s_gs_table[j] >= k - j + in_pat_len)
					s_gs_table[j] = k - j + in_pat_len;
		}
	}

	return err;
}

void boyer_moore_done()
{
	if (s_bc_table)
	{
		delete[] s_bc_table;
		s_bc_table = nullptr;
	}
	if (s_gs_table)
	{
		delete[] s_gs_table;
		s_gs_table = nullptr;
	}
}

char* boyer_moore_search(char* in_text, long in_text_len)
{
	long i, j, k, l;

	for (i = j = s_pat_len - 1; j < in_text_len && i >= 0;)
	{
		if (in_text[j] == s_pattern[i])
		{
			--i;
			--j;
		}
		else
		{
			k = s_gs_table[i + 1];
			l = s_bc_table[(u8)in_text[j]];
			j += std::max(k, l);
			i = s_pat_len - 1;
		}
	}

	return i < 0 ? in_text + j + 1 : nullptr;
}

int extract_file(int ifile, dir_node* in_file, modes in_mode, std::string path, GameInfo* gameInfo)
{
	int    err          = 0;
	size_t totalsize    = 0;
	double totalpercent = 0.0;
	int    out;

	if (s_remove_systemupdate && path.find(s_systemupdate) != std::string::npos)
		SEEK_ABSOLUTE(ifile, in_file->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek);
	else
	{
		if (in_mode == modes::extract)
			out = OPEN_FILE(in_file->filename, WRITEFLAGS, 0644);
		else if (in_mode != modes::title)
			err = 1;

		if (SEEK_ABSOLUTE(ifile, in_file->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek))
		{
			if (in_file->file_size == 0)
				XONE("{}{}{} (0) [100%]{}\r", in_mode == modes::extract ? "extracting " : "", path, in_file->filename, "");
			if (in_mode == modes::extract)
			{
				for (u64 i = 0, size = std::min(in_file->file_size, READWRITE_BUFFER_SIZE); i < in_file->file_size; i += size, size = std::min(in_file->file_size - i, READWRITE_BUFFER_SIZE))
				{
					if (!READ_EXACT(ifile, s_copy_buffer, size) || !WRITE_EXACT(out, s_copy_buffer, size))
						break;

					totalsize += size;
					totalpercent = (totalsize * 100.0) / in_file->file_size;
					XONE("{}{}{} ({}) [{:.0f}%]{}\r", in_mode == modes::extract ? "extracting " : "", path, in_file->filename, in_file->file_size, totalpercent, "");
				}

				_close(out);
			}
			else if (in_mode == modes::title)
			{
				if (gameInfo)
					err = ExtractMetadata(ifile, gameInfo);
			}
			else if (in_mode != modes::exe)
			{
				for (u64 i = 0, size = std::min(in_file->file_size, READWRITE_BUFFER_SIZE); i < in_file->file_size; i += size, size = std::min(in_file->file_size - i, READWRITE_BUFFER_SIZE))
				{
					if (!READ_EXACT(ifile, s_copy_buffer, size))
						break;

					totalsize += size;
					totalpercent = (totalsize * 100.0) / in_file->file_size;
					XONE("{}{}{} ({}) [{}%]{}\r", in_mode == modes::extract ? "extracting " : "", path, in_file->filename, in_file->file_size, totalpercent, "");
				}
			}
		}
	}

	if (!err)
		XLOG("", "");

	return err;
}

int free_dir_node_avl(void* in_dir_node_avl, void* in_context, long in_depth)
{
	auto avl = (dir_node_avl*)in_dir_node_avl;

	if (avl->subdirectory && avl->subdirectory != EMPTY_SUBDIRECTORY)
		avl_traverse_depth_first(avl->subdirectory, free_dir_node_avl, nullptr, avl_traversal_method::postfix, 0);

	delete avl;
	return 0;
}

int write_file(dir_node_avl* in_avl, write_tree_context* in_context, int in_depth)
{
	if (in_avl->subdirectory)
		return 0;

	int err = 0;
	int fd  = -1;

	if (!SEEK_ABSOLUTE(in_context->xiso, in_avl->start_sector * XISO_SECTOR_SIZE))
		return err;

	const s64 size = std::max(XISO_SECTOR_SIZE, READWRITE_BUFFER_SIZE);
	auto      buf  = new char[size_t(size) + 1];
	MEMORY_CHECK(buf);

	if (!err)
	{
		if (in_context->from == -1)
			fd = OPEN_FILE(in_avl->filename, READFLAGS, 0);
		else
		{
			fd = in_context->from;
			SEEK_ABSOLUTE(fd, in_avl->old_start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek);
		}
	}

	if (!err)
	{
		int n = 0;

		if (s_media_enable && in_avl->filename.ends_with(".xbe"))
		{
			int i = 0;
			for (s64 bytes = in_avl->file_size; !err && bytes;)
			{
				if (READ_SOME(fd, buf + i, std::min(bytes, size - i), &n))
				{
					bytes -= n;
					buf[n += i] = 0;
					for (char* p = buf; (p = boyer_moore_search(p, n - (p - buf))) != nullptr; p += XISO_MEDIA_ENABLE_LENGTH)
						p[XISO_MEDIA_ENABLE_BYTE_POS] = XISO_MEDIA_ENABLE_BYTE;

					if (bytes)
					{
						i = XISO_MEDIA_ENABLE_LENGTH - 1;
						if (WRITE_EXACT(in_context->xiso, buf, n - i))
							memcpy(buf, &buf[n - (XISO_MEDIA_ENABLE_LENGTH - 1)], XISO_MEDIA_ENABLE_LENGTH - 1);
					}
					else
						WRITE_EXACT(in_context->xiso, buf, n + i);
				}
			}
		}
		else
		{
			for (s64 bytes = in_avl->file_size; !err && bytes; bytes -= n)
			{
				if (READ_SOME(fd, buf, std::min(bytes, size), &n))
					WRITE_EXACT(in_context->xiso, buf, n);
			}
		}

		if (!err)
		{
			if (s64 bytes = (XISO_SECTOR_SIZE - (in_avl->file_size % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE)
			{
				memset(buf, XISO_PAD_BYTE, bytes);
				WRITE_EXACT(in_context->xiso, buf, bytes);
			}
		}

		XLOG("adding {} ({}) {}", PadRight(in_context->path + in_avl->filename, max_filename_length + 1), PadLeft(std::to_string(in_avl->file_size), max_filesize_length), err ? "failed" : "[OK]");

		if (!err)
		{
			++s_total_files;
			s_total_bytes += in_avl->file_size;

			if (in_context->progress)
				(*in_context->progress)(s_total_bytes, in_context->final_bytes);
		}
	}

	if (in_context->from == -1 && fd != -1)
		_close(fd);
	if (buf)
		delete[] buf;

	return err;
}

int write_directory(dir_node_avl* in_avl, int ifile, int in_depth)
{
	int  err        = 0;
	u64  file_size  = in_avl->file_size + (in_avl->subdirectory ? (XISO_SECTOR_SIZE - (in_avl->file_size % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE : 0);
	char length     = (char)in_avl->filename.size();
	char attributes = in_avl->subdirectory ? XISO_ATTRIBUTE_DIR : XISO_ATTRIBUTE_ARC, sector[XISO_SECTOR_SIZE];

	little32(in_avl->file_size);
	little32(in_avl->start_sector);

	u16 l_offset = (u16)(in_avl->left ? in_avl->left->offset / XISO_DWORD_SIZE : 0);
	u16 r_offset = (u16)(in_avl->right ? in_avl->right->offset / XISO_DWORD_SIZE : 0);

	little16(l_offset);
	little16(r_offset);

	memset(sector, XISO_PAD_BYTE, XISO_SECTOR_SIZE);

	s64 pos;
	SEEK_RELATIVE_GET(ifile, 0, &pos);
	int pad = (int)((s64)in_avl->offset + in_avl->dir_start - pos);

	WRITE_EXACT(ifile, sector, pad);
	WRITE_EXACT(ifile, &l_offset, XISO_TABLE_OFFSET_SIZE);
	WRITE_EXACT(ifile, &r_offset, XISO_TABLE_OFFSET_SIZE);
	WRITE_EXACT(ifile, &in_avl->start_sector, XISO_SECTOR_OFFSET_SIZE);
	WRITE_EXACT(ifile, &file_size, XISO_FILESIZE_SIZE);
	WRITE_EXACT(ifile, &attributes, XISO_ATTRIBUTES_SIZE);
	WRITE_EXACT(ifile, &length, XISO_FILENAME_LENGTH_SIZE);
	WRITE_EXACT(ifile, in_avl->filename.c_str(), length);

	little32(in_avl->start_sector);
	little32(in_avl->file_size);

	return err;
}

int write_tree(dir_node_avl* in_avl, write_tree_context* in_context, int in_depth)
{
	s64                pos;
	write_tree_context context;
	int                err = 0, pad;
	char               sector[XISO_SECTOR_SIZE];

	if (in_avl->subdirectory)
	{
		if (in_context->path.size())
			context.path = fmt::format("{}{}{}", in_context->path, in_avl->filename, PATH_CHAR);
		else
			context.path += PATH_CHAR;

		if (!err)
		{
			XLOG("adding {} ({}) [OK]", PadRight(context.path, max_filename_length + 1), PadLeft("0", max_filesize_length));

			if (in_avl->subdirectory != EMPTY_SUBDIRECTORY)
			{
				context.xiso        = in_context->xiso;
				context.from        = in_context->from;
				context.progress    = in_context->progress;
				context.final_bytes = in_context->final_bytes;

				if (in_context->from == -1)
					CHANGE_DIR(in_avl->filename);

				if (SEEK_ABSOLUTE(in_context->xiso, (s64)in_avl->start_sector * XISO_SECTOR_SIZE))
					err = avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)write_directory, (void*)in_context->xiso, avl_traversal_method::prefix, 0);
				SEEK_RELATIVE_GET(in_context->xiso, 0, &pos);
				if (!err && (pad = (int)((XISO_SECTOR_SIZE - (pos % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE)))
				{
					memset(sector, XISO_PAD_BYTE, pad);
					WRITE_EXACT(in_context->xiso, sector, pad);
				}
				if (!err) err = avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)write_file, &context, avl_traversal_method::prefix, 0);
				if (!err) err = avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)write_tree, &context, avl_traversal_method::prefix, 0);

				if (!err && in_context->from == -1)
					CHANGE_DIR("..");
			}
			else
			{
				memset(sector, XISO_PAD_BYTE, XISO_SECTOR_SIZE);
				SEEK_ABSOLUTE_GET(in_context->xiso, in_avl->start_sector * XISO_SECTOR_SIZE, &pos);
				WRITE_EXACT(in_context->xiso, sector, XISO_SECTOR_SIZE);
			}
		}
	}

	return err;
}

int write_dir_start_and_file_positions(dir_node_avl* in_avl, wdsafp_context* io_context, int in_depth)
{
	in_avl->dir_start = io_context->dir_start;

	if (!in_avl->subdirectory)
	{
		in_avl->start_sector = *io_context->current_sector;
		*io_context->current_sector += n_sectors(in_avl->file_size);
	}

	return 0;
}

int calculate_directory_offsets(dir_node_avl* in_avl, u64* io_current_sector, int in_depth)
{
	wdsafp_context context;

	if (in_avl->subdirectory)
	{
		if (in_avl->subdirectory == EMPTY_SUBDIRECTORY)
		{
			in_avl->start_sector = *io_current_sector;
			*io_current_sector += 1;
		}
		else
		{
			context.current_sector = io_current_sector;
			context.dir_start      = (s64)(in_avl->start_sector = *io_current_sector) * XISO_SECTOR_SIZE;

			*io_current_sector += n_sectors(in_avl->file_size);

			avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)write_dir_start_and_file_positions, &context, avl_traversal_method::prefix, 0);
			avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)calculate_directory_offsets, io_current_sector, avl_traversal_method::prefix, 0);
		}
	}

	return 0;
}

int calculate_total_files_and_bytes(dir_node_avl* in_avl, void* in_context, int in_depth)
{
	if (in_avl->subdirectory && in_avl->subdirectory != EMPTY_SUBDIRECTORY)
		avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)calculate_total_files_and_bytes, nullptr, avl_traversal_method::prefix, 0);
	else
	{
		++s_total_files;
		s_total_bytes += in_avl->file_size;
	}

	return 0;
}

int calculate_directory_size(dir_node_avl* in_avl, size_t* out_size, long in_depth)
{
	size_t length;

	if (in_depth == 0)
		*out_size = 0;

	length = XISO_FILENAME_OFFSET + in_avl->filename.size();
	length += (XISO_DWORD_SIZE - (length % XISO_DWORD_SIZE)) % XISO_DWORD_SIZE;

	if (n_sectors(*out_size + length) > n_sectors(*out_size))
		*out_size += (XISO_SECTOR_SIZE - (*out_size % XISO_SECTOR_SIZE)) % XISO_SECTOR_SIZE;

	in_avl->offset = *out_size;
	*out_size += length;

	return 0;
}

int calculate_directory_requirements(dir_node_avl* in_avl, void* in_context, int in_depth)
{
	if (in_avl->subdirectory)
	{
		if (in_avl->subdirectory != EMPTY_SUBDIRECTORY)
		{
			avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)calculate_directory_size, &in_avl->file_size, avl_traversal_method::prefix, 0);
			avl_traverse_depth_first(in_avl->subdirectory, (traversal_callback)calculate_directory_requirements, in_context, avl_traversal_method::prefix, 0);
		}
		else
			in_avl->file_size = XISO_SECTOR_SIZE;
	}

	return 0;
}

int GenerateAvlTreeLocal(std::filesystem::path basePath, dir_node_avl** out_root, int depth)
{
	if (!depth)
	{
		XLOG("generating avl tree from filesystem: ", "");
		max_filename_length = 0;
		max_filesize        = 0;
		max_filesize_length = 0;
	}

	int  err       = 0;
	bool empty_dir = true;

	if (!std::filesystem::is_directory(basePath))
	{
		FMT_ERROR("{} not a dir: {}", __LINE__, basePath.string());
		return 1;
	}

	for (auto& dir_entry : std::filesystem::directory_iterator { basePath })
	{
		auto& path     = dir_entry.path();
		auto  filename = path.filename().string();
		auto  status   = std::filesystem::status(path);
		auto  type     = status.type();

		dir_node_avl* avl = nullptr;

		if (type == std::filesystem::file_type::regular)
		{
			empty_dir      = false;
			avl            = new dir_node_avl();
			avl->filename  = filename;
			avl->file_size = std::filesystem::file_size(path);
			max_filesize   = std::max(max_filesize, avl->file_size);

			s_total_bytes += avl->file_size;
			++s_total_files;
		}
		else if (type == std::filesystem::file_type::directory)
		{
			empty_dir     = false;
			avl           = new dir_node_avl();
			avl->filename = filename;
			err           = GenerateAvlTreeLocal(path, &avl->subdirectory, depth + 1);
		}

		// used to format the console output
		max_filename_length = std::max(max_filename_length, (int)path.string().size());

		if (!err)
		{
			if (avl)
			{
				if (avl_insert(out_root, avl) == avl_result::avl_error)
					XERROR("error inserting file {} into tree (duplicate filename?)", avl->filename);
			}
		}
		else
		{
			if (avl)
				delete avl;
			break;
		}
	}

	if (empty_dir)
		*out_root = EMPTY_SUBDIRECTORY;

	return err;
}

FILE_TIME* alloc_filetime_now()
{
	time_t now;
	int    err = 0;

	auto ft = new FILE_TIME();
	MEMORY_CHECK(ft);
	if (!err && (now = time(nullptr)) == -1)
		XERROR("an unrecoverable error has occurred", "");
	if (!err)
	{
		double tmp = ((double)now + (369.0 * 365.25 * 24 * 60 * 60 - (3.0 * 24 * 60 * 60 + 6.0 * 60 * 60))) * 1.0e7;

		ft->h = (u32)(tmp * (1.0 / (4.0 * (double)(1 << 30))));
		ft->l = (u32)(tmp - ((double)ft->h) * 4.0 * (double)(1 << 30));

		little32(ft->h); // convert to little endian here because this is a PC only struct and we won't read it anyway
		little32(ft->l);
	}
	else if (ft)
	{
		delete ft;
		ft = nullptr;
	}

	return ft;
}

// Found the CD-ROM layout in ECMA-119.  Now burning software should correctly
// detect the format of the xiso and burn it correctly without the user having
// to specify sector sizes and so on.	in 10.29.04

#define ECMA_119_DATA_AREA_START       0x8000
#define ECMA_119_VOLUME_SPACE_SIZE     (ECMA_119_DATA_AREA_START + 80)
#define ECMA_119_VOLUME_SET_SIZE       (ECMA_119_DATA_AREA_START + 120)
#define ECMA_119_VOLUME_SET_IDENTIFIER (ECMA_119_DATA_AREA_START + 190)
#define ECMA_119_VOLUME_CREATION_DATE  (ECMA_119_DATA_AREA_START + 813)

// write_volume_descriptors() assumes that the iso file block from offset
// 0x8000 to 0x8808 has been zeroed prior to entry.

int write_volume_descriptors(int ifile, u64 in_total_sectors)
{
	int  err = 0;
	u64  big, little;
	char date[] = "0000000000000000";
	char spaces[ECMA_119_VOLUME_CREATION_DATE - ECMA_119_VOLUME_SET_IDENTIFIER];

	big = little = in_total_sectors;

	big32(big);
	little32(little);

	memset(spaces, 0x20, sizeof(spaces));

	SEEK_ABSOLUTE(ifile, ECMA_119_DATA_AREA_START);
	WRITE_EXACT(ifile, "\x01" "CD001\x01", 7);
	SEEK_ABSOLUTE(ifile, ECMA_119_VOLUME_SPACE_SIZE);
	WRITE_EXACT(ifile, &little, 4);
	WRITE_EXACT(ifile, &big, 4);
	SEEK_ABSOLUTE(ifile, ECMA_119_VOLUME_SET_SIZE);
	WRITE_EXACT(ifile, "\x01\x00\x00\x01\x01\x00\x00\x01\x00\x08\x08\x00", 12);
	SEEK_ABSOLUTE(ifile, ECMA_119_VOLUME_SET_IDENTIFIER);
	WRITE_EXACT(ifile, spaces, sizeof(spaces));
	WRITE_EXACT(ifile, date, sizeof(date));
	WRITE_EXACT(ifile, date, sizeof(date));
	WRITE_EXACT(ifile, date, sizeof(date));
	WRITE_EXACT(ifile, "\x01", 1);
	SEEK_ABSOLUTE(ifile, ECMA_119_DATA_AREA_START + XISO_SECTOR_SIZE);
	WRITE_EXACT(ifile, "\xff" "CD001\x01", 7);

	return err;
}

// INTERMEDIATE
///////////////

int CreateXiso(std::string in_root_directory, std::string in_output_directory, dir_node_avl* in_root, int ifile, std::string* out_iso_path, std::string in_name, progress_callback in_progress_callback, bool force)
{
	s64                pos;
	dir_node_avl       root;
	FILE_TIME*         ft = nullptr;
	write_tree_context wt_context;
	u64                start_sector;
	u64                n;
	int                i           = 0;
	int                xiso        = -1;
	int                err         = 0;
	char*              buf         = nullptr;
	auto               currentPath = std::filesystem::current_path();
	std::string        iso_dir;
	std::string        iso_name;
	std::string        xiso_path;

	s_total_bytes = s_total_files = 0;

	if (!err)
	{
		if (!in_root)
		{
			CHANGE_DIR(in_root_directory);
			if (!err)
			{
				in_root_directory.erase(in_root_directory.find_last_not_of("/\\") + 1);
				auto pos = in_root_directory.find_last_of("/\\");
				iso_dir  = in_root_directory.substr(pos + 1);
				iso_name = in_name.size() ? in_name : iso_dir;
			}
		}
		else
		{
			iso_dir  = in_root_directory;
			iso_name = in_root_directory;
		}
	}
	if (!err)
	{
		if (!iso_dir.size())
			iso_dir = PATH_CHAR_STR;
		in_output_directory.erase(in_output_directory.find_last_not_of("/\\") + 1);
		if (!iso_name.size())
			iso_name = "root";
		else if (iso_name[1] == ':')
		{
			iso_name[1] = iso_name[0];
			iso_name    = iso_name.substr(1);
		}
#if defined(_WIN32)
		xiso_path = fmt::format("{}{}{}{}", in_output_directory.size() ? in_output_directory : currentPath.string(), PATH_CHAR, iso_name, in_name.size() ? "" : ".iso");
#else
		xiso_path = fmt::format("{}{}{}{}{}{}", *in_output_directory == PATH_CHAR ? "" : currentPath.string(), *in_output_directory == PATH_CHAR ? "" : PATH_CHAR_STR, in_output_directory, PATH_CHAR, iso_name, in_name ? "" : ".iso");
#endif
	}
	if (!err)
	{
		XLOG("{} {}{}:\n", in_root ? "rewriting" : "\ncreating", iso_name, in_name.size() ? "" : ".iso");

		root.start_sector = XISO_ROOT_DIRECTORY_SECTOR;

		s_total_bytes = s_total_files = 0;

		if (in_root)
		{
			root.subdirectory = in_root;
			avl_traverse_depth_first(in_root, (traversal_callback)calculate_total_files_and_bytes, nullptr, avl_traversal_method::prefix, 0);
		}
		else
		{
			err                 = GenerateAvlTreeLocal(".", &root.subdirectory, 0);
			max_filesize_length = (max_filesize > 0) ? (int)ceil(log10(max_filesize)) : 1;
			XLOG("{}\n", err ? "failed!" : "[OK]");
		}
	}
	if (!err && in_progress_callback)
		(*in_progress_callback)(0, s_total_bytes);
	if (!err)
	{
		wt_context.final_bytes = s_total_bytes;
		s_total_bytes = s_total_files = 0;

		if (root.subdirectory == EMPTY_SUBDIRECTORY)
			root.start_sector = root.file_size = 0;

		start_sector = root.start_sector;

		avl_traverse_depth_first(&root, (traversal_callback)calculate_directory_requirements, nullptr, avl_traversal_method::prefix, 0);
		avl_traverse_depth_first(&root, (traversal_callback)calculate_directory_offsets, &start_sector, avl_traversal_method::prefix, 0);
	}
	if (!err)
	{
		n   = std::max(READWRITE_BUFFER_SIZE, XISO_HEADER_OFFSET);
		buf = new char[n];
		MEMORY_CHECK(buf);
	}
	if (!err)
	{
		if (!force && std::filesystem::exists(xiso_path))
			XERROR("{} already exists, use -f to force overwrite.", xiso_path);
		else
		{
			xiso = OPEN_FILE(xiso_path.c_str(), WRITEFLAGS, 0644);
			if (out_iso_path)
				*out_iso_path = xiso_path;
		}
	}
	if (!err)
	{
		memset(buf, 0, n);
		WRITE_EXACT(xiso, buf, XISO_HEADER_OFFSET);
	}
	WRITE_EXACT(xiso, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH);
	if (!err)
	{
		little32(root.start_sector);
		WRITE_EXACT(xiso, &root.start_sector, XISO_SECTOR_OFFSET_SIZE);
		little32(root.start_sector);
	}
	if (!err)
	{
		little32(root.file_size);
		WRITE_EXACT(xiso, &root.file_size, XISO_DIRTABLE_SIZE);
		little32(root.file_size);
	}
	if (!err)
	{
		if (in_root)
		{
			SEEK_ABSOLUTE(ifile, (s64)XISO_HEADER_OFFSET + XISO_HEADER_DATA_LENGTH + XISO_SECTOR_OFFSET_SIZE + XISO_DIRTABLE_SIZE + s_xbox_disc_lseek);
			READ_EXACT(ifile, buf, XISO_FILETIME_SIZE);
			WRITE_EXACT(xiso, buf, XISO_FILETIME_SIZE);
			memset(buf, 0, XISO_FILETIME_SIZE);
		}
		else
		{
			ft = alloc_filetime_now();
			MEMORY_CHECK(ft);
			WRITE_EXACT(xiso, ft, XISO_FILETIME_SIZE);
		}
	}
	WRITE_EXACT(xiso, buf, XISO_UNUSED_SIZE);
	WRITE_EXACT(xiso, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH);

	if (!err && !in_root)
		CHANGE_DIR("..");
	if (!err)
		root.filename = iso_dir;

	if (root.start_sector)
		SEEK_ABSOLUTE(xiso, root.start_sector * XISO_SECTOR_SIZE);
	if (!err)
	{
		wt_context.path.clear();
		wt_context.xiso     = xiso;
		wt_context.from     = in_root ? ifile : -1;
		wt_context.progress = in_progress_callback;

		err = avl_traverse_depth_first(&root, (traversal_callback)write_tree, &wt_context, avl_traversal_method::prefix, 0);
	}

	if (SEEK_END_GET(xiso, 0, &pos))
	{
		int numBytes = (int)((XISO_FILE_MODULUS - pos % XISO_FILE_MODULUS) % XISO_FILE_MODULUS);
		WRITE_EXACT(xiso, buf, numBytes);
	}

	if (!err)
		err = write_volume_descriptors(xiso, (pos + (s64)i) / XISO_SECTOR_SIZE);

	SEEK_ABSOLUTE(xiso, XISO_OPTIMIZED_TAG_OFFSET);
	WRITE_EXACT(xiso, XISO_OPTIMIZED_TAG, XISO_OPTIMIZED_TAG_LENGTH);

	if (!in_root && !s_quiet)
	{
		if (err)
			FMT_ERROR("\ncould not create {}{}", iso_name.size() ? iso_name : "xiso", iso_name.size() && !in_name.size() ? ".iso" : "");
		else
			FMT_PRINT("\nsuccessfully created {}{} ({} files totalling {} bytes added)", iso_name.size() ? iso_name : "xiso", iso_name.size() && !in_name.size() ? ".iso" : "", s_total_files, (long long int)s_total_bytes);
	}

	if (root.subdirectory != EMPTY_SUBDIRECTORY)
		avl_traverse_depth_first(root.subdirectory, free_dir_node_avl, nullptr, avl_traversal_method::postfix, 0);

	if (xiso != -1)
	{
		_close(xiso);
		if (err)
			_unlink(xiso_path.c_str());
	}

	if (buf)
		delete[] buf;
	if (ft)
		delete ft;

	std::filesystem::current_path(currentPath);
	return err;
}

int TraverseXiso(int ifile, dir_node* in_dir_node, s64 in_dir_start, std::string in_path, modes in_mode, dir_node_avl** in_root, bool in_ll_compat, GameInfo* gameInfo)
{
	dir_node_avl* avl;
	std::string   path;
	s64           curpos;
	dir_node      subdir;
	dir_node*     dir;
	dir_node      node;
	int           err = 0;
	int           sector;
	u64           l_offset = 0;
	u16           tmp;

	if (in_dir_node == nullptr)
		in_dir_node = &node;

	memset(dir = in_dir_node, 0, sizeof(dir_node));

read_entry:
	if (READ_EXACT(ifile, &tmp, XISO_TABLE_OFFSET_SIZE))
	{
		if (tmp == XISO_PAD_SHORT)
		{
			// Directory is empty
			if (l_offset == 0)
			{
				if (in_mode == modes::generate_avl)
					avl_insert(in_root, EMPTY_SUBDIRECTORY);
				goto end_traverse;
			}

			l_offset = l_offset * XISO_DWORD_SIZE + (XISO_SECTOR_SIZE - (l_offset * XISO_DWORD_SIZE) % XISO_SECTOR_SIZE);
			if (SEEK_ABSOLUTE(ifile, in_dir_start + l_offset))
				goto read_entry;
		}
		else
			l_offset = tmp;
	}

	READ_EXACT(ifile, &dir->r_offset, XISO_TABLE_OFFSET_SIZE);
	READ_EXACT(ifile, &dir->start_sector, XISO_SECTOR_OFFSET_SIZE);
	READ_EXACT(ifile, &dir->file_size, XISO_FILESIZE_SIZE);
	READ_EXACT(ifile, &dir->attributes, XISO_ATTRIBUTES_SIZE);
	READ_EXACT(ifile, &dir->filename_length, XISO_FILENAME_LENGTH_SIZE);

	if (!err)
	{
		little16(l_offset);
		little16(dir->r_offset);
		little32(dir->file_size);
		little32(dir->start_sector);
	}

	if (!err)
	{
		if (READ_EXACT(ifile, dir->filename, dir->filename_length))
		{
			auto dirname                  = dir->filename;
			dirname[dir->filename_length] = 0;

			// security patch (Chris Bainbridge), modified by in to support "...", etc. 02.14.06 (in)
			if (!strcmp(dir->filename, ".") || !strcmp(dir->filename, "..") || strchr(dir->filename, '/') || strchr(dir->filename, '\\'))
			{
				XERROR("{} filename '{}' contains invalid character(s), aborting.", __LINE__, dirname);
				exit(1);
			}
		}
	}

	if (!err && in_mode == modes::generate_avl)
	{
		avl = new dir_node_avl();
		MEMORY_CHECK(avl);
		if (!err)
			avl->filename = dir->filename;
		if (!err)
		{
			dir->avl_node = avl;

			avl->file_size        = dir->file_size;
			avl->old_start_sector = dir->start_sector;

			if (avl_insert(in_root, avl) == avl_result::avl_error)
				XERROR("this iso appears to be corrupt", "");
		}
	}

	if (!err && l_offset)
	{
		in_ll_compat = false;

		dir->left = new dir_node();
		MEMORY_CHECK(dir->left);
		if (!err)
		{
			memset(dir->left, 0, sizeof(dir_node));
			SEEK_ABSOLUTE(ifile, in_dir_start + (s64)l_offset * XISO_DWORD_SIZE);
		}
		if (!err)
		{
			dir->left->parent = dir;
			dir               = dir->left;

			goto read_entry;
		}
	}

left_processed:
	if (dir->left)
	{
		delete dir->left;
		dir->left = nullptr;
	}

	if (SEEK_RELATIVE_GET(ifile, 0, &curpos))
	{
		if (dir->attributes & XISO_ATTRIBUTE_DIR)
		{
			if (in_path.size())
			{
				if (!err)
				{
					path = in_path + dir->filename + PATH_CHAR;
					if (dir->start_sector)
						SEEK_ABSOLUTE(ifile, (s64)dir->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek);
				}
			}
			else
				path.clear();

			if (!err)
			{
				if (!s_remove_systemupdate || !strstr(dir->filename, s_systemupdate))
				{
					if (in_mode == modes::extract)
					{
						MAKE_DIR(dir->filename);
						if (dir->start_sector)
							CHANGE_DIR(dir->filename);
					}
					if (!err && in_mode != modes::generate_avl && in_mode != modes::title && in_mode != modes::exe)
						XLOG("{}{}{}{} (0){}", in_mode == modes::extract ? "creating " : "", in_path, dir->filename, PATH_CHAR_STR, in_mode == modes::extract ? " [OK]" : "");
				}
			}

			if (!err && dir->start_sector && in_mode != modes::title && in_mode != modes::exe)
			{
				memcpy(&subdir, dir, sizeof(dir_node));

				subdir.parent = nullptr;
				if (!err && dir->file_size > 0)
					err = TraverseXiso(ifile, &subdir, (s64)dir->start_sector * XISO_SECTOR_SIZE + s_xbox_disc_lseek, path, in_mode, in_mode == modes::generate_avl ? &dir->avl_node->subdirectory : nullptr, in_ll_compat, nullptr);

				if (!s_remove_systemupdate || !strstr(dir->filename, s_systemupdate))
				{
					if (in_mode == modes::extract)
						CHANGE_DIR("..");
				}
			}
		}
		else if (in_mode != modes::generate_avl)
		{
			if (!err)
			{
				if (!s_remove_systemupdate || in_path.find(s_systemupdate) == std::string::npos)
				{
					if (in_mode == modes::title)
					{
						if (!_stricmp(dir->filename, DEFAULT_XBE))
						{
							err = extract_file(ifile, dir, in_mode, in_path, gameInfo);
							XLOG("{}{}{} ({}){}", in_mode == modes::extract ? "extracting " : "", in_path, dir->filename, dir->file_size, "");
						}
					}
					else
					{
						bool skipCount = false;

						if (in_mode == modes::extract)
							err = extract_file(ifile, dir, in_mode, in_path, nullptr);
						else
						{
							if (in_mode != modes::exe || strstr(dir->filename, ".xbe"))
								XLOG("{}{}{} ({}){}", in_mode == modes::extract ? "extracting " : "", in_path, dir->filename, dir->file_size, "");
							else
								skipCount = true;
						}

						if (!skipCount)
						{
							++s_total_files;
							++s_total_files_all_isos;
							s_total_bytes += dir->file_size;
							s_total_bytes_all_isos += dir->file_size;
						}
					}
				}
			}
		}
	}

	if (!err && dir->r_offset)
	{
		// compatibility for iso's built as linked lists (bleh!)
		if (in_ll_compat && (s64)dir->r_offset * XISO_DWORD_SIZE / XISO_SECTOR_SIZE > (sector = (int)((curpos - in_dir_start) / XISO_SECTOR_SIZE)))
			dir->r_offset = sector * (XISO_SECTOR_SIZE / XISO_DWORD_SIZE) + (XISO_SECTOR_SIZE / XISO_DWORD_SIZE);

		if (SEEK_ABSOLUTE(ifile, in_dir_start + dir->r_offset * XISO_DWORD_SIZE))
		{
			l_offset = dir->r_offset;
			goto read_entry;
		}
	}

end_traverse:
	if ((dir = dir->parent))
		goto left_processed;

	return err;
}

// API
//////

void GameInfo::CreateBufferUID()
{
	uid = fmt::format("{} ({})", title, key);
	strcpy_s(buffer, fmt::format("{} {}", uid, date).c_str());
}

struct XBE
{
	u32 magic;
	u8  signature[256];
	u32 base_addr;
	u32 header_size;
	u32 image_size;
	u32 image_header_size;
	u32 timedate;
	u32 certificate_addr;
	u32 num_section;
	u32 section_header_addr;
	u32 init_flags;
	u32 entry_point;
	u32 tls_addr;
	u32 stack_size;
	u32 pe_heap_reserve;
	u32 pe_heap_commit;
	u32 pe_heap_addr;
	u32 pe_image_size;
	u32 pe_checksum;
	u32 pe_timedate;
	u32 debug_path_addr;
	u32 debug_file_addr;
	u32 debug_file16_addr;
	u32 kernel_thunk_addr;
	u32 non_kernel_addr;
	u32 num_libv;
	u32 libv_addr;
	u32 kernel_libv_addr;
	u32 xapi_libv_addr;
	u32 logo_addr;
	u32 logo_size;
	u32 unknown1;
	u32 unknown2;
	u32 unknown3;
};

struct Certificate
{
	u32 size;
	u32 timedate;
	union
	{
		u32 title_id;
		struct
		{
			u16  title_number;
			char publisher_id[2];
		};
	};
	u16 title_name[40];
	u16 title_more[32];
	u32 allowed_media;
	u32 game_region;
	u32 game_rating;
	u32 disk_number;
	u32 version;
	u8  lan_key[16];
	u8  signature_key[16];
};

template <class I, class E, class S>
struct codecvt : std::codecvt<I, E, S>
{
	~codecvt() {}
};

int CreateXiso(std::string in_root_directory, std::string in_output_directory, std::string in_name, bool force)
{
	return CreateXiso(in_root_directory, in_output_directory, nullptr, -1, nullptr, in_name, nullptr, force);
}

int CreateXiso(std::string filename)
{
	std::filesystem::path path = filename;
	if (!std::filesystem::is_directory(path))
		path = path.parent_path();

	return CreateXiso(path.string(), path.parent_path().string(), "", false);
}

int DecodeXiso(std::string filename, std::string in_path, modes in_mode, std::string* out_iso_path, bool in_ll_compat, GameInfo* gameInfo)
{
	if (!filename.size())
		return 1;

	dir_node_avl* root   = nullptr;
	bool          repair = false;
	int           root_dir_sect;
	int           root_dir_size;
	int           err         = 0;
	int           add_slash   = 0;
	size_t        len         = 0;
	auto          currentPath = std::filesystem::current_path();
	std::string   iso_name;
	std::string   name;
	std::string   short_name;

	int ifile = OPEN_FILE(filename, READFLAGS, 0);
	if (!err)
	{
		len = filename.size();

		if (in_mode == modes::rewrite)
		{
			filename.resize(filename.size() - 4);
			repair = true;
		}

		name = filename.substr(filename.find_last_of("/\\") + 1);
		len  = name.size();

		if (in_mode != modes::title)
		{
			// create a directory of the same name as the file we are working on, minus the ".iso" portion
			if (name.ends_with(".iso"))
				short_name = name.substr(0, name.size() - 4);
		}
	}

	if (!err && !len)
		XERROR("invalid xiso image name: {}", filename);

	if (!err && in_mode == modes::extract && in_path.size())
	{
		MAKE_DIR(in_path);
		CHANGE_DIR(in_path);
	}

	if (!err)
		err = VerifyXiso(ifile, &root_dir_sect, &root_dir_size, name);

	iso_name = short_name.size() ? short_name : name;

	if (!err && in_mode != modes::rewrite)
	{
		XLOG("{} \"{}\":\n", in_mode == modes::extract ? "extracting" : "listing", name);

		if (in_mode == modes::extract)
		{
			if (!in_path.size())
			{
				MAKE_DIR(iso_name);
				CHANGE_DIR(iso_name);
			}
		}
	}

	if (!err && root_dir_sect && root_dir_size)
	{
		if (in_path.size())
		{
			if (!in_path.ends_with('/') && !in_path.ends_with('\\'))
				++add_slash;
		}

		std::string buf = fmt::format("{}{}{}{}", in_path.size() ? in_path : "", add_slash && (!in_path.size()) ? PATH_CHAR_STR : "", in_mode != modes::list && in_mode != modes::exe && (!in_path.size()) ? iso_name : "", PATH_CHAR);

		if (in_mode == modes::rewrite)
		{
			if (SEEK_ABSOLUTE(ifile, (s64)root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek))
				err = TraverseXiso(ifile, nullptr, (s64)root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek, buf, modes::generate_avl, &root, in_ll_compat, gameInfo);
			if (!err)
				err = CreateXiso(iso_name, in_path, root, ifile, out_iso_path, nullptr, nullptr, true);
		}
		else
		{
			if (SEEK_ABSOLUTE(ifile, root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek))
				err = TraverseXiso(ifile, nullptr, (s64)root_dir_sect * XISO_SECTOR_SIZE + s_xbox_disc_lseek, buf, in_mode, nullptr, in_ll_compat, gameInfo);
		}
	}

	if (err == err_iso_rewritten)
		err = 0;
	if (err)
		XERROR("failed to {} xbox iso image {}", in_mode == modes::rewrite ? "rewrite" : (in_mode == modes::extract ? "extract" : "list"), name);

	if (ifile != -1)
		_close(ifile);

	std::filesystem::current_path(currentPath);

	if (repair)
		filename += '.';

	return err;
}

int DecodeXiso(std::string filename)
{
	return DecodeXiso(filename, "", modes::extract, nullptr, false, nullptr);
}

/**
 * Open an .iso and find game info
 */
bool ExtractGameInfo(std::string filename, GameInfo* gameInfo, bool log)
{
	s_quiet    = true;
	auto start = std::chrono::steady_clock::now();

	gameInfo->path = filename;

	if (DecodeXiso(filename, "", modes::title, nullptr, true, gameInfo))
		return false;

	if (log)
	{
		auto finish  = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count() / 1000.0f;
		FMT_PRINT("ExtractGameInfo: {} in {:.3f} ms", gameInfo->buffer, elapsed);
	}
	return true;
}

/**
 * The logo is using a 1-2 bytes RLE encoding
 *              01234567 89abcdef
 * A (1 byte ): 1LLLCCCC
 * B (2 bytes): 01LLLLLL LLLLCCCC
 */
bool ExtractLogo(int ifile, GameInfo* gameInfo, int logoSize, bool log)
{
	const int size       = 100 * 17;
	u8        logo[size] = { 0 };
	u8        raw[size]  = { 0 };
	int       err        = 0;
	int       n          = 0;

	if (!READ_EXACT(ifile, &raw, logoSize))
		return false;

	for (int i = 0; i < logoSize; ++i)
	{
		int len   = 0;
		int value = 0;
		u8  data  = raw[i];

		if (data & 1)
		{
			len   = (data >> 1) & 0b111;
			value = (data >> 4);
		}
		else if (data & 2)
		{
			len   = (data >> 2) + ((raw[i + 1] & 0b1111) << 6);
			value = (raw[i + 1] >> 4);
			++i;
		}

		for (int j = 0; j < len && n < size; ++j)
		{
			logo[n] = value * 15;
			++n;
		}
	}

	// show/save logo
	if (log)
	{
		if (n != size)
			FMT_PRINT("{}", n);

		const char* grayscale = " .:-=+*#%@";

		int m = 0;
		for (int i = 0; i < 17; ++i)
		{
			std::string line;
			for (int j = 0; j < 100; ++j)
			{
				char val = grayscale[logo[m + j] / 24];
				line += val;
			}
			m += 100;
			FMT_PRINT("{}", line);
		}
	}
#ifdef SAVE_PNG
	stbi_write_png("logo_microsoft.png", 100, 17, 1, logo, 100);
#endif
	return true;
}

/**
 * Find game info from a file stream
 */
int ExtractMetadata(int ifile, GameInfo* gameInfo)
{
	int err = 0;
	s64 current;
	SEEK_RELATIVE_GET(ifile, 0, &current);

	XBE xbe;
	if (!READ_EXACT(ifile, &xbe, sizeof(XBE)))
		return err;

	PrintHexBytes((char*)&xbe, sizeof(XBE), 0, true);

	auto offset = (s64)(xbe.certificate_addr - xbe.base_addr);
	if (!SEEK_ABSOLUTE(ifile, current + xbe.certificate_addr - xbe.base_addr))
		return err;

	Certificate cert;
	if (!READ_EXACT(ifile, &cert, sizeof(Certificate)))
		return err;

	XLOG("", "");
	PrintHexBytes((char*)&cert, sizeof(Certificate), offset, true);

	const char* regions[] = {
		"",
		"A",
		"J",
		"AJ",
		"E",
		"AE",
		"JE",
		"AJE",
	};

	// title in unicode
	std::u16string title16;
	for (int i = 0; i < 40 && cert.title_name[i]; ++i)
		title16 += cert.title_name[i];

	if (title16.size() == 40)
		for (int i = 0; i < 32 && cert.title_more[i]; ++i)
			title16 += cert.title_more[i];

	std::wstring_convert<codecvt<char16_t, char, std::mbstate_t>, char16_t> convert;

	auto title8 = convert.to_bytes(title16);

	// date
	auto        duration = std::chrono::seconds(cert.timedate);
	const auto  epoch    = std::chrono::time_point<std::chrono::system_clock> {};
	std::time_t timer    = std::chrono::system_clock::to_time_t(epoch + duration);
	std::tm     gmtime;
	gmtime_s(&gmtime, &timer);

	// logo
	if (gameInfo->extract & 1)
	{
		SEEK_ABSOLUTE(ifile, current + xbe.logo_addr - xbe.base_addr);
		ExtractLogo(ifile, gameInfo, xbe.logo_size, true);
	}

	gameInfo->date   = fmt::format("{}-{:02}-{:02}", 1900 + gmtime.tm_year, 1 + gmtime.tm_mon, gmtime.tm_mday);
	gameInfo->id     = fmt::format("{}{}-{:03}", cert.publisher_id[1], cert.publisher_id[0], cert.title_number);
	gameInfo->region = regions[cert.game_region & 7];
	gameInfo->debug  = !!(cert.game_region & 8);
	gameInfo->key    = fmt::format("{}-{}{}", gameInfo->id, gameInfo->region, gameInfo->debug ? "*" : "");
	gameInfo->title  = title8;

	gameInfo->CreateBufferUID();

	XLOG("=> {}", gameInfo->buffer);
	return err;
}

/**
 * HexEditor sort of view
 */
void PrintHexBytes(char* buffer, int count, size_t offset, bool showHeader)
{
	if (s_quiet)
		return;

	if (showHeader)
		FMT_PRINT("Offset     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  Decoded text");

	for (int i = 0; i < count; i += 16)
	{
		auto line = fmt::format("{:09X} ", i + offset);
		for (int j = i; j < i + 16; ++j)
			if (j >= count)
				line += fmt::format("   ");
			else
				line += fmt::format(" {:02X}", (u8)buffer[j]);

		line += fmt::format("  ");
		for (int j = i; j < i + 16 && j < count; ++j)
		{
			char val = buffer[j];
			line += fmt::format("{}", !val ? '.' : ((val != 0x7f && (u8)val >= 0x20) ? val : '+'));
		}

		FMT_PRINT("{}", line);
	}
}

/**
 * Get game info from all .iso files in a folder
 */
std::vector<GameInfo> ScanFolder(std::string folder, bool log)
{
	auto start = std::chrono::steady_clock::now();

	const std::filesystem::path rootPath { folder };
	std::vector<GameInfo>       gameInfos;

	if (!std::filesystem::is_directory(rootPath))
	{
		FMT_ERROR("{} not a dir: {}", __LINE__, rootPath.string());
		return gameInfos;
	}

	s_quiet               = true;
	size_t maxTitleLength = 0;

	for (auto& dir_entry : std::filesystem::directory_iterator { rootPath })
	{
		auto& path = dir_entry.path();
		if (path.extension().string() == ".iso")
		{
			GameInfo gameInfo;
			ExtractGameInfo(path.string(), &gameInfo, false);
			gameInfos.push_back(gameInfo);

			maxTitleLength = std::max(maxTitleLength, gameInfo.title.size());
		}
	}

	if (log)
	{
		auto finish  = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count() / 1000.0f;

		for (auto& gameInfo : gameInfos)
			FMT_PRINT("{}  {:8} {:5}{} {:10}", PadRight(gameInfo.title, maxTitleLength + 1), gameInfo.id, gameInfo.region, gameInfo.debug ? '*' : ' ', gameInfo.date);

		FMT_PRINT("Parsed {} games in {:.3f} ms", gameInfos.size(), elapsed);
	}
	return gameInfos;
}

int VerifyXiso(int ifile, int* out_root_dir_sector, int* out_root_dir_size, std::string in_iso_name)
{
	int  err = 0;
	char buffer[XISO_HEADER_DATA_LENGTH];

	SEEK_ABSOLUTE(ifile, XISO_HEADER_OFFSET);
	if (READ_EXACT(ifile, buffer, XISO_HEADER_DATA_LENGTH) && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
	{
		SEEK_ABSOLUTE(ifile, XISO_HEADER_OFFSET + GLOBAL_LSEEK_OFFSET);
		if (READ_EXACT(ifile, buffer, XISO_HEADER_DATA_LENGTH) && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
		{
			SEEK_ABSOLUTE(ifile, XISO_HEADER_OFFSET + XGD3_LSEEK_OFFSET);
			if (READ_EXACT(ifile, buffer, XISO_HEADER_DATA_LENGTH) && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
			{
				SEEK_ABSOLUTE(ifile, XISO_HEADER_OFFSET + XGD1_LSEEK_OFFSET);
				if (READ_EXACT(ifile, buffer, XISO_HEADER_DATA_LENGTH) && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
					XERROR("{} does not appear to be a valid xbox iso image", in_iso_name);
				else
					s_xbox_disc_lseek = XGD1_LSEEK_OFFSET;
			}
			else
				s_xbox_disc_lseek = XGD3_LSEEK_OFFSET;
		}
		else
			s_xbox_disc_lseek = GLOBAL_LSEEK_OFFSET;
	}
	else
		s_xbox_disc_lseek = 0;

	// read root directory information
	READ_EXACT(ifile, out_root_dir_sector, XISO_SECTOR_OFFSET_SIZE);
	READ_EXACT(ifile, out_root_dir_size, XISO_DIRTABLE_SIZE);

	little32(*out_root_dir_sector);
	little32(*out_root_dir_size);

	// seek to header tail and verify media tag
	SEEK_RELATIVE(ifile, XISO_FILETIME_SIZE + XISO_UNUSED_SIZE);
	if (READ_EXACT(ifile, buffer, XISO_HEADER_DATA_LENGTH) && memcmp(buffer, XISO_HEADER_DATA, XISO_HEADER_DATA_LENGTH))
		XERROR("{} appears to be corrupt", in_iso_name);

	// seek to root directory sector
	if (!err)
	{
		if (!*out_root_dir_sector && !*out_root_dir_size)
		{
			XLOG("xbox image {} contains no files.", in_iso_name);
			err = err_iso_no_files;
		}
		else
			SEEK_ABSOLUTE(ifile, *out_root_dir_sector * XISO_SECTOR_SIZE);
	}

	return err;
}

// MAIN
///////

} // namespace exiso
