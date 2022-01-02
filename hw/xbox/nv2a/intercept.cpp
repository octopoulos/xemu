// intercept.cpp

#include "intercept.h"
#include "nv2a_regs.h"

#include <cmath>
#include <epoxy/gl.h>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "qemu/fast-hash.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

constexpr char BASE_PREFIX[]   = "export/";
constexpr int  CAPTURE_ID      = -1;
constexpr bool COMBINE_BUFFERS = true;
constexpr int  DEBUG_FILTER    = 0;
constexpr int  DEBUG_GLTF      = 2;
constexpr int  DEBUG_INTERCEPT = 1;
constexpr int  DEBUG_NAN       = 0;
constexpr bool SAVE_IMAGE_BIN  = false;
constexpr bool SAVE_TEXTURES   = false;
constexpr bool USE_EXTENSIONS  = false;
constexpr bool USE_FEEDBACK    = false;
constexpr bool ZERO_MIN_INDEX  = true;

// namespace rsx
// {
const char* vectorTypes[] = {
	"",
	"SCALAR",
	"VEC2",
	"VEC3",
	"VEC4",
};

// s1, f, sf, ub, s32k, cmp, ub256
/*
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D			   0
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1				   1
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F				   2
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL			   4
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K			   5
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP				   6
*/
// ub_d3d, s1, f, ub_ogl, s32k, cmp
const int allTypes[]   = { 1, 1, 1, 1, 1, 1 };
const int byteTypes[]  = { 1, 0, 0, 1, 0, 0 };
const int compTypes[]  = { 0, 0, 0, 0, 0, 1 };
const int floatTypes[] = { 0, 0, 1, 0, 0, 0 };
const int shortTypes[] = { 0, 1, 0, 0, 1, 0 };

struct DDS_PIXELFORMAT
{
	u32 dwSize;
	u32 dwFlags;
	u32 dwFourCC;
	u32 dwRGBBitCount;
	u32 dwRBitMask;
	u32 dwGBitMask;
	u32 dwBBitMask;
	u32 dwABitMask;
};

struct DDS_HEADER
{
	u32             dwSize;
	u32             dwFlags;
	u32             dwHeight;
	u32             dwWidth;
	u32             dwPitchOrLinearSize;
	u32             dwDepth;
	u32             dwMipMapCount;
	u32             dwReserved1[11];
	DDS_PIXELFORMAT ddspf;
	u32             dwCaps;
	u32             dwCaps2;
	u32             dwCaps3;
	u32             dwCaps4;
	u32             dwReserved2;
};

union FloatUint
{
	u32   ivalue;
	float fvalue;
	struct
	{
		u16 slow;
		u16 shi;
	};
};

struct VertexBlock
{
	u32 addr;
	int count;
};

struct VertexAttrib
{
	int                      id;
	std::string              name;
	std::vector<VertexBlock> blocks;
	std::vector<float>       data;
	int                      type;
	int                      offset;
	int                      count;
	int                      inSize;
	int                      outSize;
	u32                      inStride;
	u32                      outStride;
	int                      componentType;
	double                   normalDelta;
	int                      normalId;
	float                    scaling;
	float                    mins[4];
	float                    maxs[4];
	int                      skip;
};

// id
int captureId = 0;
int frameId   = 0;

// intercept
int                                   intercept = 0;
std::vector<int>                      interceptDraws;
u32                                   interceptIndex = 0;
std::string                           interceptFilter;
std::chrono::steady_clock::time_point interceptStart;

// hash
std::map<u64, int> hashImages;
std::map<u64, int> hashSamplers;
std::map<u64, int> hashTextures;

// draw call
const char*               drawnCommands = "oabi";
int                       drawnCommand  = 0; // 0: none, 1: array, 2: inlined array, 3: indexed
int                       drawMode      = 0;
int                       indexCount    = 0;
std::vector<u16>          indices16;
std::vector<u32>          indices32;
u32                       minIndex = 0xFFFFFFFF;
u32                       maxIndex = 0;
std::vector<VertexAttrib> vas;
int                       vertexCount = 0;

// extra
std::vector<u8>    combinedBuffer;
usz                combinedPrevSize = 0;
std::vector<float> feedback;

// Commmon
//////////

/**
 * Analyse the intercept filter if it has changed
 */
void AnalyseFilter()
{
	std::string filter = ""; // g_cfg.misc.intercept_filter;
	if (filter == interceptFilter)
		return;

	interceptFilter = filter;
	interceptDraws.clear();

	std::sregex_token_iterator reg_end;
	std::stringstream          ss(filter);
	std::string                text;

	while (std::getline(ss, text, '\n'))
	{
		if (DEBUG_FILTER)
			std::cerr << "AnalyseFilter: \"" << text << "\"\n";

		std::regex                 re(",?(\\d+)");
		std::sregex_token_iterator it(text.begin(), text.end(), re, 1);
		for (; it != reg_end; it++)
		{
			if (DEBUG_FILTER)
				std::cerr << "[" << *it << "]";
			interceptDraws.push_back(std::stoi(*it));
		}
		std::cerr << '\n';
	}

	if (DEBUG_FILTER)
	{
		for (auto& draw : interceptDraws)
			std::cerr << '(' << draw << ')';
		std::cerr << '\n';
	}
}

void ClearState()
{
	captureId = 0;
	combinedBuffer.resize(0);
	combinedBuffer.reserve(8 * 1024 * 1024); // 8M
	combinedPrevSize = 0;
	hashImages.clear();
	hashSamplers.clear();
	hashTextures.clear();
	indexCount = 0;
}

inline bool Debug()
{
	return (CAPTURE_ID > 0 && captureId == CAPTURE_ID);
}

// https://gist.github.com/milhidaka/95863906fe828198f47991c813dbe233
float DecodeFloat16(u16 float16_value)
{
	// MSB -> LSB
	// float16 = 1bit: sign, 5bit: exponent, 10bit: fraction
	// float32 = 1bit: sign, 8bit: exponent, 23bit: fraction
	// for normal exponent(1 to 0x1e): value = 2 ** (exponent - 15) * (1.fraction)
	// for denormalized exponent(0): value = 2 ** -14 * (0.fraction)
	u32 sign     = float16_value >> 15;
	u32 exponent = (float16_value >> 10) & 0x1F;
	u32 fraction = (float16_value & 0x3FF);
	u32 float32_value;

	if (exponent == 0)
	{
		if (fraction == 0)
		{
			// zero
			float32_value = (sign << 31);
		}
		else
		{
			// can be represented as ordinary value in float32
			// 2 ** -14 * 0.0101
			// => 2 ** -16 * 1.0100
			// int int_exponent = -14;
			exponent = 127 - 14;
			while ((fraction & (1 << 10)) == 0)
			{
				// int_exponent--;
				exponent--;
				fraction <<= 1;
			}
			fraction &= 0x3FF;
			// int_exponent += 127;
			float32_value = (sign << 31) | (exponent << 23) | (fraction << 13);
		}
	}
	// Inf or NaN
	else if (exponent == 0x1F)
		float32_value = (sign << 31) | (0xFF << 23) | (fraction << 13);
	// ordinary number
	else
		float32_value = (sign << 31) | ((exponent + (127 - 15)) << 23) | (fraction << 13);

	return *((float*)&float32_value);
}

/**
 * Find an unnamed attribute that matches the size and type, then name it
 */
void FindAttribute(int& counter, std::string name, int normalId, int sizeFilter, const int* allowedTypes)
{
	bool hasMultiple = name.ends_with('_');
	if (!hasMultiple && counter > 0)
		return;

	for (auto& va : vas)
	{
		if (va.skip)
			continue;
		if (va.name.size())
		{
			if (va.name == name)
				++counter;
			continue;
		}
		if (normalId != -1 && va.normalId != normalId)
			continue;

		if ((sizeFilter & (1 << va.outSize)) && allowedTypes[int(va.type)])
		{
			va.name = hasMultiple ? name + std::to_string(counter) : name;
			++counter;
			break;
		}
	}
}

constexpr u32 FOURCC(const char p[5])
{
	return (u32(p[3]) << 24) | (u32(p[2]) << 16) | (u32(p[1]) << 8) | p[0];
}

/**
 * Get indices + handle restart + calculate min/max
 */
template <typename T>
void GetIndices(std::vector<T>& indices, const u32 idxAddr, const u32 idxCount, bool is_primitive_restart_enabled, u32 primitive_restart_index)
{
	indices.reserve(indices.size() + idxCount);
	auto fifo = reinterpret_cast<T*>(idxAddr);

	T    lastIndex    = 0;
	T    restartIndex = 0;
	bool restart      = false;

	for (u32 i = 0; i < idxCount; ++i)
	{
		T index = fifo[i];

		if (is_primitive_restart_enabled && u32(index) == primitive_restart_index)
		{
			restart      = true;
			restartIndex = lastIndex;
		}
		else
		{
			if (restart)
			{
				indices.push_back(restartIndex);
				if (!(indices.size() & 1))
					indices.push_back(restartIndex);
				indices.push_back(index);
				restart = false;
			}
			indices.push_back(index);

			minIndex  = std::min<u32>(minIndex, static_cast<u32>(index));
			maxIndex  = std::max<u32>(maxIndex, static_cast<u32>(index));
			lastIndex = index;
		}
	}
}

/**
 * Check min-max + if normalised
 */
void MinMaxDelta(VertexAttrib& va)
{
	auto mins = va.mins;
	auto maxs = va.maxs;
	for (int i = 0; i < 4; ++i)
	{
		mins[i] = std::numeric_limits<float>::max();
		maxs[i] = std::numeric_limits<float>::lowest();
	}

	va.normalDelta = 0.0;
	auto& data     = va.data;

	for (usz i = 0, len = data.size(); i < len; i += va.outSize)
	{
		double delta = 0.0;

		for (int j = 0; j < va.outSize; ++j)
		{
			float value = data[i + j];
			if (std::isfinite(value))
			{
				mins[j] = std::min(mins[j], value);
				maxs[j] = std::max(maxs[j], value);
				delta += (double)value * (double)value;
			}
			else if (DEBUG_NAN)
				std::cerr << captureId << " ??? " << va.id << " : " << value << " type=" << int(va.type) << '\n';
		}

		va.normalDelta += (delta - 1) * (delta - 1);
	}

	va.normalDelta /= va.count;
}

/**
 * Save a binary file
 */
void SaveBinary(std::string relativeName, void* data, usz length, bool overwrite)
{
	if (Debug())
		std::cerr << "SaveBinary: " << relativeName << ' ' << length << '\n';

	auto absoluteName = BASE_PREFIX + relativeName;
	if (overwrite || !std::filesystem::exists(absoluteName))
	{
		std::ofstream out(absoluteName, std::ios::binary);
		out.write(static_cast<const char*>(data), length);
		out.close();
	}
}

/**
 * Either append data to the combined buffer, or save the binary directly
 */
usz SaveCombined(std::string relativeName, void* data, usz length)
{
	if (COMBINE_BUFFERS)
	{
		auto size = combinedBuffer.size();
		// make sure the size is a multiple of 4 bytes
		auto padding = 4 - ((size + length) % 4);
		combinedBuffer.resize(size + length + padding);
		std::memcpy(combinedBuffer.data() + size, data, length);
		combinedPrevSize = size;
		return size;
	}
	else
	{
		SaveBinary(relativeName, data, length, true);
		return 0;
	}
}

/**
 * Save a PNG file if doesn't exist yet
 */
std::string SavePNG(std::string string, const int width, const int height, const int channel, const u8* data, const int pitch)
{
	auto baseName     = string + ".png";
	auto absoluteName = BASE_PREFIX + baseName;
	if (!std::filesystem::exists(absoluteName))
		stbi_write_png(absoluteName.c_str(), width, height, channel, data, pitch);
	return baseName;
}

// GLTF
///////

// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html
rapidjson::Document doc;
rapidjson::Value    accessors;
rapidjson::Value    attributes;
rapidjson::Value    buffers;
rapidjson::Value    bufferViews;
rapidjson::Value    extensionsUsed;
rapidjson::Value    extras;
rapidjson::Value    images;
rapidjson::Value    materials;
rapidjson::Value    meshes;
rapidjson::Value    nodes;
rapidjson::Value    primitiveExtensions;
rapidjson::Value    samplers;
rapidjson::Value    textures;
auto&               allocator = doc.GetAllocator();
int                 texCoord  = 0;

/**
 * Create a string value
 */
rapidjson::Value CreateString(std::string string)
{
	rapidjson::Value value;
	value.SetString(string.c_str(), static_cast<u32>(string.size()), allocator);
	return value;
}

/**
 * Add an accessor
 */
void GLTF_Accessor(int bufferView, int byteOffset, int byteStride, int componentType, int count, rapidjson::Value& accessorMin, rapidjson::Value& accessorMax, int size)
{
	rapidjson::Value accessor(rapidjson::kObjectType);
	accessor.AddMember("bufferView", bufferView, allocator);
	accessor.AddMember("byteOffset", byteOffset, allocator);
	if (byteStride) accessor.AddMember("byteStride", byteStride, allocator);
	accessor.AddMember("componentType", componentType, allocator);
	accessor.AddMember("count", count, allocator);
	accessor.AddMember("min", accessorMin, allocator);
	accessor.AddMember("max", accessorMax, allocator);
	// if (normalized)
	//	accessor.AddMember("normalized", true, allocator);
	accessor.AddMember("type", rapidjson::StringRef(vectorTypes[size]), allocator);
	accessors.PushBack(accessor, allocator);
}

/**
 * Clear the document and add common data
 */
void GLTF_Begin()
{
	interceptStart = std::chrono::steady_clock::now();
	if (DEBUG_GLTF & 1)
		std::cerr << "GLTF_Begin\n";

	doc.SetObject();
	accessors.SetArray();
	attributes.SetObject();
	buffers.SetArray();
	bufferViews.SetArray();
	extensionsUsed.SetArray();
	extras.SetObject();
	images.SetArray();
	materials.SetArray();
	meshes.SetArray();
	nodes.SetArray();
	primitiveExtensions.SetObject();
	samplers.SetArray();
	textures.SetArray();

	AnalyseFilter();
	interceptIndex = 0;
}

/**
 * Save a buffer + add it to the GLTF
 */
void GLTF_Buffer(int target, void* data, usz length, std::string suffix, int stride)
{
	auto fileName = fmt::format("{}-{}{}", frameId, captureId, suffix);
	auto offset   = SaveCombined(fileName, data, length);

	if (!COMBINE_BUFFERS)
	{
		rapidjson::Value buffer(rapidjson::kObjectType);
		buffer.AddMember("uri", CreateString(fileName), allocator);
		buffer.AddMember("byteLength", length, allocator);
		buffers.PushBack(buffer, allocator);
	}

	rapidjson::Value bufferView(rapidjson::kObjectType);
	bufferView.AddMember("buffer", COMBINE_BUFFERS ? 0 : buffers.Size() - 1, allocator);
	bufferView.AddMember("byteLength", length, allocator);
	bufferView.AddMember("byteOffset", offset, allocator);
	if (stride) bufferView.AddMember("byteStride", stride, allocator);
	bufferView.AddMember("target", target, allocator);
	bufferViews.PushBack(bufferView, allocator);
}

/**
 * Finish and save the document
 */
void GLTF_End()
{
	// save combined binary
	if (COMBINE_BUFFERS)
	{
		auto             fileName = fmt::format("{}.bin", frameId);
		rapidjson::Value buffer(rapidjson::kObjectType);
		buffer.AddMember("uri", CreateString(fileName), allocator);
		buffer.AddMember("byteLength", combinedBuffer.size(), allocator);
		buffers.PushBack(buffer, allocator);

		SaveBinary(fileName, combinedBuffer.data(), combinedBuffer.size(), true);
	}

	// save GLTF
	rapidjson::Value asset(rapidjson::kObjectType);
	asset.AddMember("version", "2.0", allocator);
	asset.AddMember("extras", extras, allocator);

	// first node contains all children
	rapidjson::Value children(rapidjson::kArrayType);
	for (int i = 0, length = nodes.Size(); i < length - 1; ++i)
		children.PushBack(i + 1, allocator);
	nodes[0].AddMember("children", children, allocator);

	rapidjson::Value scenes(rapidjson::kArrayType);
	rapidjson::Value scene(rapidjson::kObjectType);
	rapidjson::Value sceneNodes(rapidjson::kArrayType);
	sceneNodes.PushBack(0, allocator);
	scene.AddMember("nodes", sceneNodes, allocator);
	scenes.PushBack(scene, allocator);

								doc.AddMember("asset", asset, allocator);
	if (extensionsUsed.Size()) 	doc.AddMember("extensionsUsed", extensionsUsed, allocator);
								doc.AddMember("scenes", scenes, allocator);
								doc.AddMember("nodes", nodes, allocator);
								doc.AddMember("meshes", meshes, allocator);
	if (accessors.Size()) 		doc.AddMember("accessors", accessors, allocator);
								doc.AddMember("bufferViews", bufferViews, allocator);
								doc.AddMember("buffers", buffers, allocator);
	if (materials.Size()) 		doc.AddMember("materials", materials, allocator);
	if (textures.Size()) 		doc.AddMember("textures", textures, allocator);
	if (samplers.Size()) 		doc.AddMember("samplers", samplers, allocator);
	if (images.Size()) 			doc.AddMember("images", images, allocator);

	std::ofstream                                      ofs(fmt::format("{}{}.gltf", BASE_PREFIX, frameId));
	rapidjson::OStreamWrapper                          osw(ofs);
	rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(osw);
	writer.SetIndent('\t', 1);
	doc.Accept(writer);
	ofs.close();

	if (DEBUG_GLTF & 2)
	{
		auto finish  = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finish - interceptStart).count();
		std::cerr << "GLTF_End: " << interceptIndex << " in " << elapsed / 1000.0f << " ms\n";
	}
}

/**
 * Build an array + find min-max
 */
template <typename T>
void GLTF_Extract(VertexAttrib& va, int multiplier, int componentType, float scaling)
{
	// 0) setup
	va.componentType = componentType;
	va.scaling       = scaling;

	const int inSize  = va.inSize;
	const int outSize = inSize * multiplier;
	va.outSize        = outSize;

	const int numOutput   = va.count * outSize;
	const int outputBytes = numOutput * sizeof(float);

	auto& data = va.data;
	data.reserve(numOutput);

	// 1) extract data
	for (const auto& block : va.blocks)
	{
		const int inputBytes = block.count * va.inStride;

		// VTX_FMT_COMP32
		// https://bartwronski.com/2017/04/02/small-float-formats-r11g11b10f-precision/
		if (va.type == NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP)
		{
			{
				// python test:
				// value = 0b00000000000111111111110000000000
				// [(((value >> 21) & 0x7FF) << 5), (((value >> 10) & 0x7FF) << 5), ((value & 0x3FF) << 6)]
				// >> [0, 65504, 0]
				for (int i = 0; i < inputBytes; i += va.inStride)
				{
					auto values = reinterpret_cast<u32*>(block.addr + i);
					for (int j = 0; j < inSize; ++j)
					{
						u32 value = values[j];
						s16 r     = (((value >> 21) & 0x7FF) << 5);
						s16 g     = (((value >> 10) & 0x7FF) << 5);
						s16 b     = ((value & 0x3FF) << 6);

						data.push_back(r / 32767.f);
						data.push_back(g / 32767.f);
						data.push_back(b / 32767.f);
					}
				}
			}
		}
		// standard
		else
		{
			for (int i = 0; i < inputBytes; i += va.inStride)
			{
				auto values = reinterpret_cast<T>(block.addr + i);
				for (int j = 0; j < inSize; ++j)
				{
					float value = static_cast<float>(values[j]) / scaling;
					data.push_back(value);
				}
			}
		}
	}

	// 2) check min-max + if normalised
	MinMaxDelta(va);

	if (Debug())
	{
		for (usz i = 0, len = data.size(); i < len; i += va.outSize)
		{
			std::cerr << "  " << va.id << "  ";
			for (int j = 0; j < va.outSize; ++j)
				std::cerr << ' ' << data[i + j];
			std::cerr << '\n';
		}
	}
}

template <typename T>
void GLTF_Feedback(std::vector<T> indices, int drawMode)
{
	if (!USE_FEEDBACK)
		return;

	auto feedbackCount = feedback.size();
	std::cerr << "feedCount=" << feedbackCount << " indexCount=" << indices.size()
	          << " vertexCount=" << vertexCount << " drawMode=" << drawMode << '\n';

	int multiplier = 1;
	switch (drawMode)
	{
	case GL_TRIANGLES:
		multiplier = 4;
		break;
	case GL_TRIANGLE_STRIP:
		multiplier = 2;
		break;
	}

	if (feedbackCount != indices.size() * multiplier)
	{
		std::cerr << "Wrong number\n";
		return;
	}

	int maxId = -1;
	for (auto& va : vas)
	{
		maxId = std::max(maxId, va.id);
		if (va.name == "POSITION")
			va.skip = 1;
	}

	VertexAttrib va { 0 };
	va.count     = vertexCount;
	va.id        = maxId + 1;
	va.name      = "POSITION";
	va.outSize   = 3;
	va.outStride = 4 * sizeof(float);

	auto& data = va.data;
	data.resize(vertexCount * 4u);

	for (usz i = 0, j = 0; i < feedbackCount; i += 4, ++j)
	{
		usz index4        = indices[j] * 4u;
		data[index4 + 0u] = feedback[i + 0u];
		data[index4 + 1u] = feedback[i + 1u];
		data[index4 + 2u] = feedback[i + 2u];
		data[index4 + 3u] = feedback[i + 3u];
	}

	MinMaxDelta(va);
	vas.push_back(va);
}

/**
 * Guess the names of all attributes
 */
bool GLTF_GuessVertexAttributes()
{
	if (!vas.size())
		return false;

	texCoord = 0;

	// 1) attrs with low normalDelta => normal
	for (int i = 0; i < 2; ++i)
	{
		VertexAttrib* best   = nullptr;
		double        lowest = -1;

		for (auto& va : vas)
		{
			if (!va.normalId && (lowest < 0 || va.normalDelta < lowest))
			{
				lowest = va.normalDelta;
				best   = &va;
			}
		}

		if (best && lowest < 1)
			best->normalId = 1 + i;
		else
			break;
	}

	// 2) find attributes
	// POSITION
	{
		int position = 0;
		for (auto& normalId : { 0, 2, 1 })
		{
			FindAttribute(position, "POSITION", normalId, 1 << 3, floatTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 3, shortTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 3, compTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 4, floatTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 4, shortTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 3, byteTypes);
			FindAttribute(position, "POSITION", normalId, 1 << 3, byteTypes);
			FindAttribute(position, "POSITION", normalId, (1 << 3) | (1 << 4), allTypes);
		}

		if (!position)
			return false;
	}

	// NORMAL
	{
		int normal = 0;
		for (auto& normalId : { 1, 2, 0 })
		{
			FindAttribute(normal, "NORMAL", normalId, 1 << 3, floatTypes);
			FindAttribute(normal, "NORMAL", normalId, 1 << 3, shortTypes);
			FindAttribute(normal, "NORMAL", normalId, 1 << 3, compTypes);
		}
	}

	// TEXCOORD_n + COLOR_n
	{
		FindAttribute(texCoord, "TEXCOORD_0", -1, 1 << 2, floatTypes);
		FindAttribute(texCoord, "TEXCOORD_0", -1, 1 << 2, shortTypes);
		FindAttribute(texCoord, "TEXCOORD_0", -1, 1 << 2, byteTypes);

		int color = 0;
		FindAttribute(color, "COLOR_0", -1, 1 << 4, byteTypes);
		FindAttribute(color, "COLOR_0", -1, 1 << 4, shortTypes);
		FindAttribute(color, "COLOR_0", -1, 1 << 4, floatTypes);
		FindAttribute(color, "COLOR_0", -1, (1 << 3) | (1 << 4), allTypes);

		FindAttribute(texCoord, "TEXCOORD_", -1, 1 << 2, allTypes);
		FindAttribute(color, "COLOR_", -1, (1 << 3) | (1 << 4), allTypes);
	}

	// 3) make sure POSITION is VEC3
	for (auto& va : vas)
	{
		if (va.name != "POSITION" || va.outSize != 4)
			continue;
		// std::cerr << captureId << " POSITION_RESIZE: " << va.outSize << '\n';
		va.outSize   = 3;
		va.outStride = 4 * sizeof(float);
	}

	if (Debug())
	{
		std::cerr << captureId << " GuessVertexAttributes:\n";
		for (auto& va : vas)
		{
			std::cerr << "va" << va.id << std::setw(12) << va.name << " type=" << int(va.type)
			          << " stride=" << va.inStride << " compType=" << va.componentType << " inSize=" << va.inSize
			          << " outSize=" << va.outSize << " normalId=" << va.normalId << " delta=" << va.normalDelta << '\n';
		}
	}

	return true;
}

template <typename T>
bool GLTF_Indices(std::vector<T>& indices, std::string suffix, int componentType)
{
	indexCount = (int)indices.size();
	if (!indexCount)
		return false;

	auto data = indices.data();

	// make indices start at 0
	if (ZERO_MIN_INDEX && minIndex > 0)
	{
		for (int i = 0; i < indexCount; ++i)
			data[i] -= minIndex;
		maxIndex -= minIndex;
		minIndex = 0;
	}

	GLTF_Buffer(GL_ELEMENT_ARRAY_BUFFER, data, indexCount * sizeof(T), suffix, 0);

	rapidjson::Value accessorMin(rapidjson::kArrayType);
	rapidjson::Value accessorMax(rapidjson::kArrayType);
	accessorMin.PushBack(minIndex, allocator);
	accessorMax.PushBack(maxIndex, allocator);
	GLTF_Accessor(bufferViews.Size() - 1, 0, 0, componentType, indexCount, accessorMin, accessorMax, 1);
	return true;
}

/**
 * Save all vertex attributes
 */
void GLTF_SaveVertexAttributes()
{
	attributes.SetObject();

	for (auto& va : vas)
	{
		// save buffer
		auto baseName = fmt::format("-{}{}-{}-{}.bin", drawnCommands[drawnCommand], va.id, va.inSize, int(va.type));

		auto& data = va.data;
		GLTF_Buffer(GL_ARRAY_BUFFER, data.data(), data.size() * sizeof(float), baseName, va.outStride);

		rapidjson::Value accessorMin(rapidjson::kArrayType);
		rapidjson::Value accessorMax(rapidjson::kArrayType);
		for (int j = 0; j < va.outSize; ++j)
		{
			accessorMin.PushBack(va.mins[j], allocator);
			accessorMax.PushBack(va.maxs[j], allocator);
		}

		GLTF_Accessor(bufferViews.Size() - 1, 0, 0, GL_FLOAT, va.count, accessorMin, accessorMax, va.outSize);

		// add attribute to GLTF
		if (!va.skip)
		{
			if (!va.name.size())
				va.name = fmt::format("attr_{}_{}_{}", va.id, va.inSize, int(va.type));
			auto attrName = CreateString(va.name);
			attributes.AddMember(attrName, accessors.Size() - 1, allocator);
		}

		if (Debug())
		{
			auto writtenBytes = data.size() * sizeof(float);
			auto writtenCount = data.size();

			std::cerr << captureId << " name=" << va.name << " type=" << int(va.type) << " inSize=" << va.inSize
			          << " stride=" << va.inStride << " count=" << va.count << " writtenCount=" << writtenCount
			          << " writtenBytes=" << writtenBytes << " outputBytes=" << (va.count * va.outSize * sizeof(float))
			          << " outSize=" << va.outSize << '\n';

			std::cerr << "blocks:";
			for (const auto& block : va.blocks)
				std::cerr << ' ' << block.count;
			std::cerr << '\n';
		}
	}
}

/**
 * Save texture + add to the document
 */
template <typename T>
bool GLTF_Texture(rapidjson::Value& material, T& tex, std::string middleFix, int& counter)
{
	int imageId = -1;
	{
		u64  hash   = 0;
		auto dataIt = hashImages.find(hash);

		if (dataIt == hashImages.end())
		{
			std::string baseName;
			{
			}

			rapidjson::Value image(rapidjson::kObjectType);
			image.AddMember("uri", CreateString(baseName), allocator);
			images.PushBack(image, allocator);

			imageId          = images.Size() - 1;
			hashImages[hash] = imageId;
		}
		else
			imageId = dataIt->second;
	}

	if (imageId >= 0)
	{
		// a) sampler
		int samplerId;
		{
			// https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html#schema-reference-sampler
			u64 hash = 0;

			auto samplerIt = hashSamplers.find(hash);
			if (samplerIt == hashSamplers.end())
			{
				rapidjson::Value sampler(rapidjson::kObjectType);
				samplers.PushBack(sampler, allocator);

				samplerId          = samplers.Size() - 1;
				hashSamplers[hash] = samplerId;
			}
			else
				samplerId = samplerIt->second;
		}

		// b) texture
		int textureId;
		{
			auto string = fmt::format("{},{}", samplerId, imageId);
			u64  hash   = 0;

			auto textureIt = hashTextures.find(hash);
			if (textureIt == hashTextures.end())
			{
				rapidjson::Value texture(rapidjson::kObjectType);
				texture.AddMember("sampler", samplerId, allocator);
				texture.AddMember("source", imageId, allocator);
				textures.PushBack(texture, allocator);

				textureId          = textures.Size() - 1;
				hashTextures[hash] = textureId;
			}
			else
				textureId = textureIt->second;
		}

		// c) material
		rapidjson::Value materialColor(rapidjson::kObjectType);
		materialColor.AddMember("index", textureId, allocator);

		if (!material.HasMember("pbrMetallicRoughness"))
		{
			rapidjson::Value materialInfo(rapidjson::kObjectType);

			if (texCoord)
				materialColor.AddMember("texCoord", texCoord - 1, allocator);

			materialInfo.AddMember("baseColorTexture", materialColor, allocator);
			materialInfo.AddMember("metallicFactor", 0.5f, allocator);
			materialInfo.AddMember("roughnessFactor", 0.5f, allocator);
			auto materialName = CreateString("mat_" + std::to_string(materials.Size()));
			material.AddMember("name", materialName, allocator);
			material.AddMember("pbrMetallicRoughness", materialInfo, allocator);
		}
		else if (!material.HasMember("normalTexture")) 		material.AddMember("normalTexture", materialColor, allocator);
		else if (!material.HasMember("emissiveTexture")) 	material.AddMember("emissiveTexture", materialColor, allocator);
		else if (!material.HasMember("occlusionTexture")) 	material.AddMember("occlusionTexture", materialColor, allocator);
	}

	return true;
}

/**
 * Add a vertex attribute
 */
void GLTF_VertexAttribute(int index, const u32 inputMask, bool isIndexed)
{
	if (!(inputMask & (1u << index)))
		return;

	// const auto& info = method_registers.vertex_arrays_info[index];
	// if (!info.size())
	// 	return;

	// const u32 base_address = get_vertex_offset_from_base(
	// 	method_registers.vertex_data_base_offset(), info.offset() & 0x7fffffff);
	// const u32 memory_location = info.offset() >> 31;

	// VertexAttrib va{ 0 };
	// va.id = index;
	// const u32 addr = get_address(base_address, memory_location);
	// va.inSize = info.size();
	// va.inStride = info.stride();
	// va.type = info.type();
	// const u32 vertSize = get_vertex_type_size_on_host(va.type, va.inSize);

	// // 1) read data for array / indexed
	// va.count = 0;

	// if (!isIndexed)
	// {
	// 	method_registers.current_draw_clause.begin();
	// 	do
	// 	{
	// 		const auto& range = method_registers.current_draw_clause.get_range();
	// 		va.blocks.push_back({addr + (range.first * va.inStride), int(range.count)});
	// 		va.count += range.count;
	// 	}
	// 	while (method_registers.current_draw_clause.next());
	// }
	// else
	// {
	// 	va.count = maxIndex - minIndex + 1;
	// 	va.blocks.push_back({ addr + (minIndex * va.inStride), va.count });
	// }

	// // 2) extract/convert data (check shader)
	// // elem_size_table[] = { 2, 4, 2, 1, 2, 4, 1 }
	// // scaling_table[] = { 32768., 1., 1., 255., 1., 32767., 1. }
	// switch (va.type)
	// {
	// case vertex_base_type::s1:    GLTF_Extract<s16>  (va, 1, GL_SHORT,         32768.f); break; // 0: VTX_FMT_SNORM16
	// case vertex_base_type::f:     GLTF_Extract<float>(va, 1, GL_FLOAT,             1.f); break; // 1: VTX_FMT_FLOAT32
	// case vertex_base_type::sf:    GLTF_Extract<float>(va, 1, GL_FLOAT,             1.f); break; // 2: VTX_FMT_FLOAT16
	// case vertex_base_type::ub:    GLTF_Extract<u8>   (va, 1, GL_UNSIGNED_BYTE,   255.f); break; // 3: VTX_FMT_UNORM8
	// case vertex_base_type::s32k:  GLTF_Extract<s16>  (va, 1, GL_SHORT,             1.f); break; // 4: VTX_FMT_SINT16
	// case vertex_base_type::cmp:   GLTF_Extract<float>(va, 3, GL_FLOAT,         32767.f); break; // 5: VTX_FMT_COMP32
	// case vertex_base_type::ub256: GLTF_Extract<u8>   (va, 1, GL_UNSIGNED_BYTE,   255.f); break; // 6: VTX_FMT_UINT8
	// }

	// vas.push_back(va);
}

// API
//////

void AddVertexAttribute()
{
	if (!(intercept & 1))
		return;
}

std::vector<float>* GetFeedback()
{
	return USE_FEEDBACK ? &feedback : nullptr;
}

int GetIntercept()
{
	return intercept;
}

/**
 * Called when the draw call has started
 */
bool NewDrawBegin(int drawMode_)
{
	// std::cerr << captureId << " NewDraw: " << intercept << '\n';
	if (!(intercept & 1))
		return false;

	// captured enough?
	if (auto interceptSize = interceptDraws.size(); interceptIndex > 0 && interceptIndex >= interceptSize)
		return false;

	++captureId;
	vas.clear();

	drawnCommand = 0;
	drawMode     = drawMode_;
	indices16.clear();
	indices32.clear();
	minIndex = 0xFFFFFFFF;
	maxIndex = 0;
	primitiveExtensions.SetObject();
	return true;
}

/**
 * Called when the draw call has ended
 */
void NewDrawEnd()
{
	if (!(intercept & 1))
		return;

	// index
	int indexId = -1;
	if (indices16.size())
	{
		GLTF_Indices(indices16, "-i16.bin", GL_UNSIGNED_SHORT);
		GLTF_Feedback(indices16, drawMode);
		indexId = accessors.Size() - 1;
	}
	else if (indices32.size())
	{
		GLTF_Indices(indices32, "-i32.bin", GL_UNSIGNED_INT);
		GLTF_Feedback(indices32, drawMode);
		indexId = accessors.Size() - 1;
	}

	// material
	int materialId = -1;
	if (SAVE_TEXTURES)
	{
		// save fragment tex mem
		rapidjson::Value material(rapidjson::kObjectType);
		// int texFrag = 0;
		// for (const auto& tex : method_registers.fragment_textures)
		// 	GLTF_Texture(material, tex, "-tf-", texFrag);

		// save vertex texture mem
		// int texVert = 0;
		// for (const auto& tex : method_registers.vertex_textures)
		// 	GLTF_Texture(material, tex, "-tv-", texVert);

		if (material.MemberCount())
		{
			materials.PushBack(material, allocator);
			materialId = materials.Size() - 1;
		}
	}

	// vertex
	{
		GLTF_SaveVertexAttributes();

		rapidjson::Value primitive(rapidjson::kObjectType);

		if (attributes.MemberCount()) 			primitive.AddMember("attributes", attributes, allocator);
		if (primitiveExtensions.MemberCount()) 	primitive.AddMember("extensions", primitiveExtensions, allocator);
		if (indexId >= 0) 						primitive.AddMember("indices", indexId, allocator);
		if (materialId >= 0) 					primitive.AddMember("material", materialId, allocator);
												primitive.AddMember("mode", drawMode, allocator);

		if (!nodes.Size())
		{
			rapidjson::Value node(rapidjson::kObjectType);
			node.AddMember("name", "node_0", allocator);
			nodes.PushBack(node, allocator);
		}

		int              nodeId = nodes.Size();
		rapidjson::Value node(rapidjson::kObjectType);
		node.AddMember("name", CreateString(fmt::format("node_{}_{}_{}", nodeId, vertexCount, indexCount / 3)), allocator);
		if (nodeId > 0)
			node.AddMember("mesh", nodeId - 1, allocator);
		nodes.PushBack(node, allocator);

		rapidjson::Value mesh(rapidjson::kObjectType);
		rapidjson::Value primitives(rapidjson::kArrayType);
		primitives.PushBack(primitive, allocator);
		mesh.AddMember("primitives", primitives, allocator);
		meshes.PushBack(mesh, allocator);
	}
}

/**
 * Export all data
 */
bool NewDrawMain()
{
	if (!NewDrawBegin(0))
		return false;

	if (!drawnCommand)
		return false;

	// limit capture to specific draws
	if (interceptDraws.size())
	{
		if (vertexCount != interceptDraws[interceptIndex])
			return false;
		++interceptIndex;
	}

	if (!GLTF_GuessVertexAttributes())
		return false;

	NewDrawEnd();
	return true;
}

/**
 * Called at the beginning and end of a frame
 */
void NewFrame(bool start)
{
	if (!(intercept & 1))
		return;

	if (nodes.GetType() == rapidjson::kArrayType && nodes.Size())
	{
		GLTF_End();
		nodes.SetArray();
	}

	if (!start)
	{
		SetIntercept(0);
		return;
	}

	++frameId;
	ClearState();
	GLTF_Begin();
}

void SetIndices(u32* indices_, u32 indexCount_, u32 minIndex_, u32 maxIndex_)
{
	if (!(intercept & 1))
		return;

	indices32.resize(indexCount_);
	minIndex = minIndex_;
	maxIndex = maxIndex_;

	std::memcpy(indices32.data(), indices_, indexCount_ * sizeof(u32));
	GLTF_Indices(indices32, "-i32.bin", GL_UNSIGNED_SHORT);
}

/**
 * Set intercept mode
 * @param value 0:stop, &1:capturing, &2:single frame, &4:continuous
 */
void SetIntercept(int value)
{
	int prev = intercept;
	if (!value)
		intercept &= ~1;
	else
		intercept = value;
	if (DEBUG_INTERCEPT && intercept != prev)
		std::cerr << "SetIntercept: " << prev << " => " << intercept << '\n';
}
// } // namespace rsx
