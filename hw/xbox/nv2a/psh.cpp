/*
 * QEMU Geforce NV2A pixel shader translation
 *
 * Copyright (c) 2013 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2020-2021 Matt Borgerson
 *
 * Based on:
 * Cxbx, PixelShader.cpp
 * Copyright (c) 2004 Aaron Robinson <caustik@caustik.com>
 *                    Kingofc <kingofc@freenet.de>
 * Xeon, XBD3DPixelShader.cpp
 * Copyright (c) 2003 _SF_
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include <fmt/core.h>
#include <sstream>
#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "qapi/qmp/qstring.h"

#include "shaders_common.h"
#include "psh.h"

/*
 * This implements translation of register combiners into glsl
 * fragment shaders, but all terminology is in terms of Xbox DirectX
 * pixel shaders, since I wanted to be lazy while referencing existing
 * work / stealing code.
 *
 * For some background, see the OpenGL extension:
 * https://www.opengl.org/registry/specs/NV/register_combiners.txt
 */

enum PS_TEXTUREMODES
{                                                 // valid in stage 0 1 2 3
	PS_TEXTUREMODES_NONE                 = 0x00L, // * * * *
	PS_TEXTUREMODES_PROJECT2D            = 0x01L, // * * * *
	PS_TEXTUREMODES_PROJECT3D            = 0x02L, // * * * *
	PS_TEXTUREMODES_CUBEMAP              = 0x03L, // * * * *
	PS_TEXTUREMODES_PASSTHRU             = 0x04L, // * * * *
	PS_TEXTUREMODES_CLIPPLANE            = 0x05L, // * * * *
	PS_TEXTUREMODES_BUMPENVMAP           = 0x06L, // - * * *
	PS_TEXTUREMODES_BUMPENVMAP_LUM       = 0x07L, // - * * *
	PS_TEXTUREMODES_BRDF                 = 0x08L, // - - * *
	PS_TEXTUREMODES_DOT_ST               = 0x09L, // - - * *
	PS_TEXTUREMODES_DOT_ZW               = 0x0aL, // - - * *
	PS_TEXTUREMODES_DOT_RFLCT_DIFF       = 0x0bL, // - - * -
	PS_TEXTUREMODES_DOT_RFLCT_SPEC       = 0x0cL, // - - - *
	PS_TEXTUREMODES_DOT_STR_3D           = 0x0dL, // - - - *
	PS_TEXTUREMODES_DOT_STR_CUBE         = 0x0eL, // - - - *
	PS_TEXTUREMODES_DPNDNT_AR            = 0x0fL, // - * * *
	PS_TEXTUREMODES_DPNDNT_GB            = 0x10L, // - * * *
	PS_TEXTUREMODES_DOTPRODUCT           = 0x11L, // - * * -
	PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST = 0x12L, // - - - *
	                                              // 0x13-0x1f reserved
};

enum PS_INPUTMAPPING
{
	PS_INPUTMAPPING_UNSIGNED_IDENTITY = 0x00L, // max(0,x)         OK for final combiner
	PS_INPUTMAPPING_UNSIGNED_INVERT   = 0x20L, // 1 - max(0,x)     OK for final combiner
	PS_INPUTMAPPING_EXPAND_NORMAL     = 0x40L, // 2*max(0,x) - 1   invalid for final combiner
	PS_INPUTMAPPING_EXPAND_NEGATE     = 0x60L, // 1 - 2*max(0,x)   invalid for final combiner
	PS_INPUTMAPPING_HALFBIAS_NORMAL   = 0x80L, // max(0,x) - 1/2   invalid for final combiner
	PS_INPUTMAPPING_HALFBIAS_NEGATE   = 0xa0L, // 1/2 - max(0,x)   invalid for final combiner
	PS_INPUTMAPPING_SIGNED_IDENTITY   = 0xc0L, // x                invalid for final combiner
	PS_INPUTMAPPING_SIGNED_NEGATE     = 0xe0L, // -x               invalid for final combiner
};

enum PS_REGISTER
{
	PS_REGISTER_ZERO     = 0x00L, // r
	PS_REGISTER_DISCARD  = 0x00L, // w
	PS_REGISTER_C0       = 0x01L, // r
	PS_REGISTER_C1       = 0x02L, // r
	PS_REGISTER_FOG      = 0x03L, // r
	PS_REGISTER_V0       = 0x04L, // r/w
	PS_REGISTER_V1       = 0x05L, // r/w
	PS_REGISTER_T0       = 0x08L, // r/w
	PS_REGISTER_T1       = 0x09L, // r/w
	PS_REGISTER_T2       = 0x0aL, // r/w
	PS_REGISTER_T3       = 0x0bL, // r/w
	PS_REGISTER_R0       = 0x0cL, // r/w
	PS_REGISTER_R1       = 0x0dL, // r/w
	PS_REGISTER_V1R0_SUM = 0x0eL, // r
	PS_REGISTER_EF_PROD  = 0x0fL, // r

	PS_REGISTER_ONE               = PS_REGISTER_ZERO | PS_INPUTMAPPING_UNSIGNED_INVERT, // OK for final combiner
	PS_REGISTER_NEGATIVE_ONE      = PS_REGISTER_ZERO | PS_INPUTMAPPING_EXPAND_NORMAL,   // invalid for final combiner
	PS_REGISTER_ONE_HALF          = PS_REGISTER_ZERO | PS_INPUTMAPPING_HALFBIAS_NEGATE, // invalid for final combiner
	PS_REGISTER_NEGATIVE_ONE_HALF = PS_REGISTER_ZERO | PS_INPUTMAPPING_HALFBIAS_NORMAL, // invalid for final combiner
};

enum PS_COMBINERCOUNTFLAGS
{
	PS_COMBINERCOUNT_MUX_LSB = 0x0000L, // mux on r0.a lsb
	PS_COMBINERCOUNT_MUX_MSB = 0x0001L, // mux on r0.a msb

	PS_COMBINERCOUNT_SAME_C0   = 0x0000L, // c0 same in each stage
	PS_COMBINERCOUNT_UNIQUE_C0 = 0x0010L, // c0 unique in each stage

	PS_COMBINERCOUNT_SAME_C1   = 0x0000L, // c1 same in each stage
	PS_COMBINERCOUNT_UNIQUE_C1 = 0x0100L  // c1 unique in each stage
};

enum PS_COMBINEROUTPUT
{
	PS_COMBINEROUTPUT_IDENTITY         = 0x00L, // y = x
	PS_COMBINEROUTPUT_BIAS             = 0x08L, // y = x - 0.5
	PS_COMBINEROUTPUT_SHIFTLEFT_1      = 0x10L, // y = x*2
	PS_COMBINEROUTPUT_SHIFTLEFT_1_BIAS = 0x18L, // y = (x - 0.5)*2
	PS_COMBINEROUTPUT_SHIFTLEFT_2      = 0x20L, // y = x*4
	PS_COMBINEROUTPUT_SHIFTRIGHT_1     = 0x30L, // y = x/2

	PS_COMBINEROUTPUT_AB_BLUE_TO_ALPHA = 0x80L, // RGB only

	PS_COMBINEROUTPUT_CD_BLUE_TO_ALPHA = 0x40L, // RGB only

	PS_COMBINEROUTPUT_AB_MULTIPLY    = 0x00L,
	PS_COMBINEROUTPUT_AB_DOT_PRODUCT = 0x02L, // RGB only

	PS_COMBINEROUTPUT_CD_MULTIPLY    = 0x00L,
	PS_COMBINEROUTPUT_CD_DOT_PRODUCT = 0x01L, // RGB only

	PS_COMBINEROUTPUT_AB_CD_SUM = 0x00L, // 3rd output is AB+CD
	PS_COMBINEROUTPUT_AB_CD_MUX = 0x04L, // 3rd output is MUX(AB,CD) based on R0.a
};

enum PS_CHANNEL
{
	PS_CHANNEL_RGB   = 0x00, // used as RGB source
	PS_CHANNEL_BLUE  = 0x00, // used as ALPHA source
	PS_CHANNEL_ALPHA = 0x10, // used as RGB or ALPHA source
};

enum PS_FINALCOMBINERSETTING
{
	PS_FINALCOMBINERSETTING_CLAMP_SUM = 0x80, // V1+R0 sum clamped to [0,1]

	PS_FINALCOMBINERSETTING_COMPLEMENT_V1 = 0x40, // unsigned invert mapping

	PS_FINALCOMBINERSETTING_COMPLEMENT_R0 = 0x20, // unsigned invert mapping
};

enum PS_DOTMAPPING
{                                              // valid in stage 0 1 2 3
	PS_DOTMAPPING_ZERO_TO_ONE         = 0x00L, // - * * *
	PS_DOTMAPPING_MINUS1_TO_1_D3D     = 0x01L, // - * * *
	PS_DOTMAPPING_MINUS1_TO_1_GL      = 0x02L, // - * * *
	PS_DOTMAPPING_MINUS1_TO_1         = 0x03L, // - * * *
	PS_DOTMAPPING_HILO_1              = 0x04L, // - * * *
	PS_DOTMAPPING_HILO_HEMISPHERE_D3D = 0x05L, // - * * *
	PS_DOTMAPPING_HILO_HEMISPHERE_GL  = 0x06L, // - * * *
	PS_DOTMAPPING_HILO_HEMISPHERE     = 0x07L, // - * * *
};

// Structures to describe the PS definition

struct InputInfo
{
	int reg, mod, chan;
};

struct InputVarInfo
{
	InputInfo a, b, c, d;
};

struct FCInputInfo
{
	InputInfo a, b, c, d, e, f, g;
	bool      v1r0_sum, clamp_sum, inv_v1, inv_r0, enabled;
};

struct OutputInfo
{
	int ab, cd, muxsum, flags, ab_op, cd_op, muxsum_op, mapping, ab_alphablue, cd_alphablue;
};

struct PSStageInfo
{
	InputVarInfo rgb_input;
	InputVarInfo alpha_input;
	OutputInfo   rgb_output;
	OutputInfo   alpha_output;
	int          c0;
	int          c1;
};

struct PixelShader
{
	PshState          state;
	int               num_stages;
	int               flags;
	PSStageInfo       stage[8];
	FCInputInfo       final_input;
	int               tex_modes[4];
	int               input_tex[4];
	int               dot_map[4];
	std::string       varE, varF;
	std::stringstream code;
	int               cur_stage;
	int               num_var_refs;
	char              var_refs[32][32];
	int               num_const_refs;
	char              const_refs[32][32];
};

static void add_var_ref(PixelShader* ps, const char* var)
{
	for (int i = 0; i < ps->num_var_refs; i++)
	{
		if (strcmp((char*)ps->var_refs[i], var) == 0)
			return;
	}
	strcpy((char*)ps->var_refs[ps->num_var_refs++], var);
}

static void add_const_ref(PixelShader* ps, const char* var)
{
	for (int i = 0; i < ps->num_const_refs; i++)
	{
		if (strcmp((char*)ps->const_refs[i], var) == 0)
			return;
	}
	strcpy((char*)ps->const_refs[ps->num_const_refs++], var);
}

// Get the code for a variable used in the program
static std::string get_var(PixelShader* ps, int reg, bool is_dest)
{
	switch (reg)
	{
	case PS_REGISTER_DISCARD:
		if (is_dest)
			return "";
		else
			return "vec4(0.0)";
		break;
	case PS_REGISTER_C0:
		if (ps->flags & PS_COMBINERCOUNT_UNIQUE_C0 || ps->cur_stage == 8)
		{
			auto reg = fmt::format("c0_{}", ps->cur_stage);
			add_const_ref(ps, reg.c_str());
			return reg;
		}
		else
		{
			// Same c0
			add_const_ref(ps, "c0_0");
			return "c0_0";
		}
		break;
	case PS_REGISTER_C1:
		if (ps->flags & PS_COMBINERCOUNT_UNIQUE_C1 || ps->cur_stage == 8)
		{
			auto reg = fmt::format("c1_{}", ps->cur_stage);
			add_const_ref(ps, reg.c_str());
			return reg;
		}
		else
		{
			// Same c1
			add_const_ref(ps, "c1_0");
			return "c1_0";
		}
		break;
	case PS_REGISTER_FOG: return "pFog";
	case PS_REGISTER_V0: return "v0";
	case PS_REGISTER_V1: return "v1";
	case PS_REGISTER_T0: return "t0";
	case PS_REGISTER_T1: return "t1";
	case PS_REGISTER_T2: return "t2";
	case PS_REGISTER_T3: return "t3";
	case PS_REGISTER_R0:
		add_var_ref(ps, "r0");
		return "r0";
	case PS_REGISTER_R1:
		add_var_ref(ps, "r1");
		return "r1";
	case PS_REGISTER_V1R0_SUM:
		add_var_ref(ps, "r0");
		return "vec4(v1.rgb + r0.rgb, 0.0)";
	case PS_REGISTER_EF_PROD:
		return fmt::format("vec4({} * {}, 0.0)", ps->varE, ps->varF);
	default:
		assert(false);
		return "";
	}
}

// Get input variable code
static std::string get_input_var(PixelShader* ps, InputInfo in, bool is_alpha)
{
	auto reg = get_var(ps, in.reg, false);

	if (!is_alpha)
	{
		switch (in.chan)
		{
		case PS_CHANNEL_RGB: reg += ".rgb"; break;
		case PS_CHANNEL_ALPHA: reg += ".aaa"; break;
		default:
			assert(false);
			break;
		}
	}
	else
	{
		switch (in.chan)
		{
		case PS_CHANNEL_BLUE: reg += ".b"; break;
		case PS_CHANNEL_ALPHA: reg += ".a"; break;
		default:
			assert(false);
			break;
		}
	}

	std::string res;
	switch (in.mod)
	{
	case PS_INPUTMAPPING_UNSIGNED_IDENTITY:
		res = fmt::format("max({}, 0.0)", reg);
		break;
	case PS_INPUTMAPPING_UNSIGNED_INVERT:
		res = fmt::format("(1.0 - clamp({}, 0.0, 1.0))", reg);
		break;
	case PS_INPUTMAPPING_EXPAND_NORMAL:
		res = fmt::format("(2.0 * max({}, 0.0) - 1.0)", reg);
		break;
	case PS_INPUTMAPPING_EXPAND_NEGATE:
		res = fmt::format("(-2.0 * max({}, 0.0) + 1.0)", reg);
		break;
	case PS_INPUTMAPPING_HALFBIAS_NORMAL:
		res = fmt::format("(max({}, 0.0) - 0.5)", reg);
		break;
	case PS_INPUTMAPPING_HALFBIAS_NEGATE:
		res = fmt::format("(-max({}, 0.0) + 0.5)", reg);
		break;
	case PS_INPUTMAPPING_SIGNED_IDENTITY:
		res = reg;
		break;
	case PS_INPUTMAPPING_SIGNED_NEGATE:
		res = fmt::format("-{}", reg);
		break;
	default:
		assert(false);
		break;
	}

	return res;
}

// Get code for the output mapping of a stage
static std::string get_output(std::string reg, int mapping)
{
	std::string res;
	switch (mapping)
	{
	case PS_COMBINEROUTPUT_IDENTITY:
		res = reg;
		break;
	case PS_COMBINEROUTPUT_BIAS:
		res = fmt::format("({} - 0.5)", reg);
		break;
	case PS_COMBINEROUTPUT_SHIFTLEFT_1:
		res = fmt::format("({} * 2.0)", reg);
		break;
	case PS_COMBINEROUTPUT_SHIFTLEFT_1_BIAS:
		res = fmt::format("(({} - 0.5) * 2.0)", reg);
		break;
	case PS_COMBINEROUTPUT_SHIFTLEFT_2:
		res = fmt::format("({} * 4.0)", reg);
		break;
	case PS_COMBINEROUTPUT_SHIFTRIGHT_1:
		res = fmt::format("({} / 2.0)", reg);
		break;
	default:
		assert(false);
		break;
	}
	return res;
}

// Add the GLSL code for a stage
static void add_stage_code(
    PixelShader* ps, InputVarInfo input, OutputInfo output, const char* write_mask, bool is_alpha)
{
	auto a = get_input_var(ps, input.a, is_alpha);
	auto b = get_input_var(ps, input.b, is_alpha);
	auto c = get_input_var(ps, input.c, is_alpha);
	auto d = get_input_var(ps, input.d, is_alpha);

	const char* caster = "";
	if (strlen(write_mask) == 3)
		caster = "vec3";

	std::string ab;
	if (output.ab_op == PS_COMBINEROUTPUT_AB_DOT_PRODUCT)
		ab = fmt::format("dot({}, {})", a, b);
	else
		ab = fmt::format("({} * {})", a, b);

	std::string cd;
	if (output.cd_op == PS_COMBINEROUTPUT_CD_DOT_PRODUCT)
		cd = fmt::format("dot({}, {})", c, d);
	else
		cd = fmt::format("({} * {})", c, d);

	auto ab_mapping = get_output(ab, output.mapping);
	auto cd_mapping = get_output(cd, output.mapping);
	auto ab_dest    = get_var(ps, output.ab, true);
	auto cd_dest    = get_var(ps, output.cd, true);
	auto sum_dest   = get_var(ps, output.muxsum, true);

	if (ab_dest.size())
		ps->code << fmt::format("{}.{} = clamp({}({}), -1.0, 1.0);\n", ab_dest, write_mask, caster, ab_mapping);
	else
		ab_dest = ab_mapping;

	if (cd_dest.size())
		ps->code << fmt::format("{}.{} = clamp({}({}), -1.0, 1.0);\n", cd_dest, write_mask, caster, cd_mapping);
	else
		cd_dest = cd_mapping;

	if (!is_alpha && output.flags & PS_COMBINEROUTPUT_AB_BLUE_TO_ALPHA)
		ps->code << fmt::format("{}.a = {}.b;\n", ab_dest, ab_dest);
	if (!is_alpha && output.flags & PS_COMBINEROUTPUT_CD_BLUE_TO_ALPHA)
		ps->code << fmt::format("{}.a = {}.b;\n", cd_dest, cd_dest);

	std::string sum;
	if (output.muxsum_op == PS_COMBINEROUTPUT_AB_CD_SUM)
		sum = fmt::format("({} + {})", ab, cd);
	else
		sum = fmt::format("((r0.a >= 0.5) ? {}({}) : {}({}))", caster, cd, caster, ab);

	auto sum_mapping = get_output(sum, output.mapping);
	if (sum_dest.size())
		ps->code << fmt::format("{}.{} = clamp({}({}), -1.0, 1.0);\n", sum_dest, write_mask, caster, sum_mapping);
}

// Add code for the final combiner stage
static void add_final_stage_code(PixelShader* ps, FCInputInfo final)
{
	ps->varE = get_input_var(ps, final.e, false);
	ps->varF = get_input_var(ps, final.f, false);

	auto a = get_input_var(ps, final.a, false);
	auto b = get_input_var(ps, final.b, false);
	auto c = get_input_var(ps, final.c, false);
	auto d = get_input_var(ps, final.d, false);
	auto g = get_input_var(ps, final.g, true);

	ps->code << fmt::format("fragColor.rgb = {} + mix(vec3({}), vec3({}), vec3({}));\n", d, c, b, a)
	         << fmt::format("fragColor.a = {};\n", g);

	ps->varE.clear();
	ps->varF.clear();
}

static std::string psh_convert(PixelShader* ps)
{
	std::stringstream preflight;

	preflight
	    << STRUCT_VERTEX_DATA
	    << "noperspective in VertexData g_vtx;\n"
	    << "#define vtx g_vtx\n"
	    << "\n"
	    << "out vec4 fragColor;\n"
	    << "\n"
	    << "uniform vec4 fogColor;\n";

	const char* dotmap_funcs[] = {
		"dotmap_zero_to_one",
		"dotmap_minus1_to_1_d3d",
		"dotmap_minus1_to_1_gl",
		"dotmap_minus1_to_1",
		"dotmap_hilo_1",
		"dotmap_hilo_hemisphere_d3d",
		"dotmap_hilo_hemisphere_gl",
		"dotmap_hilo_hemisphere",
	};

	preflight << "float sign1(float x) {\n"
	             "    x *= 255.0;\n"
	             "    return (x-128.0)/127.0;\n"
	             "}\n"
	             "float sign2(float x) {\n"
	             "    x *= 255.0;\n"
	             "    if (x >= 128.0) return (x-255.5)/127.5;\n"
	             "               else return (x+0.5)/127.5;\n"
	             "}\n"
	             "float sign3(float x) {\n"
	             "    x *= 255.0;\n"
	             "    if (x >= 128.0) return (x-256.0)/127.0;\n"
	             "               else return (x)/127.0;\n"
	             "}\n"
	             "float sign3_to_0_to_1(float x) {\n"
	             "    if (x >= 0) return x/2;\n"
	             "           else return 1+x/2;\n"
	             "}\n"
	             "vec3 dotmap_zero_to_one(vec3 col) {\n"
	             "    return col;\n"
	             "}\n"
	             "vec3 dotmap_minus1_to_1_d3d(vec3 col) {\n"
	             "    return vec3(sign1(col.r),sign1(col.g),sign1(col.b));\n"
	             "}\n"
	             "vec3 dotmap_minus1_to_1_gl(vec3 col) {\n"
	             "    return vec3(sign2(col.r),sign2(col.g),sign2(col.b));\n"
	             "}\n"
	             "vec3 dotmap_minus1_to_1(vec3 col) {\n"
	             "    return vec3(sign3(col.r),sign3(col.g),sign3(col.b));\n"
	             "}\n"
	             "vec3 dotmap_hilo_1(vec3 col) {\n"
	             "    return col;\n" // FIXME
	             "}\n"
	             "vec3 dotmap_hilo_hemisphere_d3d(vec3 col) {\n"
	             "    return col;\n" // FIXME
	             "}\n"
	             "vec3 dotmap_hilo_hemisphere_gl(vec3 col) {\n"
	             "    return col;\n" // FIXME
	             "}\n"
	             "vec3 dotmap_hilo_hemisphere(vec3 col) {\n"
	             "    return col;\n" // FIXME
	             "}\n"
	             "const float[9] gaussian3x3 = float[9](\n"
	             "    1.0/16.0, 2.0/16.0, 1.0/16.0,\n"
	             "    2.0/16.0, 4.0/16.0, 2.0/16.0,\n"
	             "    1.0/16.0, 2.0/16.0, 1.0/16.0);\n"
	             "const vec2[9] convolution3x3 = vec2[9](\n"
	             "    vec2(-1.0,-1.0),vec2(0.0,-1.0),vec2(1.0,-1.0),\n"
	             "    vec2(-1.0, 0.0),vec2(0.0, 0.0),vec2(1.0, 0.0),\n"
	             "    vec2(-1.0, 1.0),vec2(0.0, 1.0),vec2(1.0, 1.0));\n"
	             "vec4 gaussianFilter2DRectProj(sampler2DRect sampler, vec3 texCoord) {\n"
	             "    vec4 sum = vec4(0.0);\n"
	             "    for (int i = 0; i < 9; i++) {\n"
	             "        sum += gaussian3x3[i]*textureProj(sampler,\n"
	             "                   texCoord + vec3(convolution3x3[i], 0.0));\n"
	             "    }\n"
	             "    return sum;\n"
	             "}\n";

	// Window Clipping
	preflight << "uniform ivec4 clipRegion[8];\n";
	std::stringstream clip;
	clip << fmt::format("/*  Window-clip ({}) */\n", ps->state.window_clip_exclusive ? "Exclusive" : "Inclusive");
	if (!ps->state.window_clip_exclusive)
		clip << "bool clipContained = false;\n";

	clip << "for (int i = 0; i < 8; i++) {\n"
	        "  bvec4 clipTest = bvec4(lessThan(gl_FragCoord.xy-0.5, clipRegion[i].xy),\n"
	        "                         greaterThan(gl_FragCoord.xy-0.5, clipRegion[i].zw));\n"
	        "  if (!any(clipTest)) {\n";
	if (ps->state.window_clip_exclusive)
		clip << "    discard;\n";
	else
		clip << "    clipContained = true;\n"
		        "    break;\n";

	clip << "  }\n"
	        "}\n";
	if (!ps->state.window_clip_exclusive)
		clip << "if (!clipContained) {\n"
		        "  discard;\n"
		        "}\n";

	// calculate perspective-correct inputs
	std::stringstream vars;
	vars << "vec4 pD0 = vtx.D0 / vtx.inv_w;\n"
	     << "vec4 pD1 = vtx.D1 / vtx.inv_w;\n"
	     << "vec4 pB0 = vtx.B0 / vtx.inv_w;\n"
	     << "vec4 pB1 = vtx.B1 / vtx.inv_w;\n"
	     << "vec4 pFog = vec4(fogColor.rgb, clamp(vtx.Fog / vtx.inv_w, 0.0, 1.0));\n"
	     << "vec4 pT0 = vtx.T0 / vtx.inv_w;\n"
	     << "vec4 pT1 = vtx.T1 / vtx.inv_w;\n"
	     << "vec4 pT2 = vtx.T2 / vtx.inv_w;\n";
	if (ps->state.point_sprite)
	{
		assert(!ps->state.rect_tex[3]);
		vars << "vec4 pT3 = vec4(gl_PointCoord, 1.0, 1.0);\n";
	}
	else
		vars << "vec4 pT3 = vtx.T3 / vtx.inv_w;\n";

	vars << "\n"
	     << "vec4 v0 = pD0;\n"
	     << "vec4 v1 = pD1;\n";

	ps->code.clear();

	for (int i = 0; i < 4; i++)
	{
		const char* sampler_type = NULL;

		assert(ps->dot_map[i] < 8);
		const char* dotmap_func = dotmap_funcs[ps->dot_map[i]];
		if (ps->dot_map[i] > 3)
			NV2A_UNIMPLEMENTED("Dot Mapping mode %s", dotmap_func);

		switch (ps->tex_modes[i])
		{
		case PS_TEXTUREMODES_NONE:
			vars << fmt::format("vec4 t{} = vec4(0.0); /* PS_TEXTUREMODES_NONE */\n", i);
			break;
		case PS_TEXTUREMODES_PROJECT2D:
		{
			sampler_type       = ps->state.rect_tex[i] ? "sampler2DRect" : "sampler2D";
			const char* lookup = "textureProj";
			if ((ps->state.conv_tex[i] == CONVOLUTION_FILTER_GAUSSIAN) || (ps->state.conv_tex[i] == CONVOLUTION_FILTER_QUINCUNX))
			{
				/* FIXME: Quincunx looks better than Linear and costs less than
				 * Gaussian, but Gaussian should be plenty fast so use it for
				 * now.
				 */
				if (ps->state.rect_tex[i])
					lookup = "gaussianFilter2DRectProj";
				else
					NV2A_UNIMPLEMENTED("Convolution for 2D textures");
			}
			vars << fmt::format("pT{}.xy = texScale{} * pT{}.xy;\n", i, i, i)
			     << fmt::format("vec4 t{} = {}(texSamp{}, pT{}.xyw);\n", i, lookup, i, i);
			break;
		}
		case PS_TEXTUREMODES_PROJECT3D:
			sampler_type = "sampler3D";
			vars << fmt::format("vec4 t{} = textureProj(texSamp{}, pT{}.xyzw);\n", i, i, i);
			break;
		case PS_TEXTUREMODES_CUBEMAP:
			sampler_type = "samplerCube";
			vars << fmt::format("vec4 t{} = texture(texSamp{}, pT{}.xyz / pT{}.w);\n", i, i, i, i);
			break;
		case PS_TEXTUREMODES_PASSTHRU:
			vars << fmt::format("vec4 t{} = pT{};\n", i, i);
			break;
		case PS_TEXTUREMODES_CLIPPLANE:
		{
			vars << fmt::format("vec4 t{} = vec4(0.0); /* PS_TEXTUREMODES_CLIPPLANE */\n", i);
			for (int j = 0; j < 4; j++)
			{
				vars << fmt::format(
				    "  if(pT{}.{} {} 0.0) {{ discard; }};\n", i, "xyzw"[j], ps -> state.compare_mode[i][j] ? ">=" : "<");
			}
			break;
		}
		case PS_TEXTUREMODES_BUMPENVMAP:
			assert(i >= 1);
			sampler_type = ps->state.rect_tex[i] ? "sampler2DRect" : "sampler2D";
			preflight << fmt::format("uniform mat2 bumpMat{};\n", i);

			if (ps->state.snorm_tex[ps->input_tex[i]])
			{
				// Input color channels already signed (FIXME: May not always want signed textures in this case)
				vars << fmt::format("vec2 dsdt{} = t{}.bg;\n", i, ps->input_tex[i]);
			}
			else
			{
				// Convert to signed (FIXME: loss of accuracy due to filtering/interpolation)
				vars << fmt::format(
				    "vec2 dsdt{} = vec2(sign3(t{}.b), sign3(t{}.g));\n", i, ps->input_tex[i], ps->input_tex[i]);
			}

			vars << fmt::format("dsdt{} = bumpMat{} * dsdt{};\n", i, i, i)
			     << fmt::format("vec4 t{} = texture(texSamp{}, texScale{} * (pT{}.xy + dsdt{}));\n", i, i, i, i, i);
			break;
		case PS_TEXTUREMODES_BUMPENVMAP_LUM:
			assert(i >= 1);
			sampler_type = ps->state.rect_tex[i] ? "sampler2DRect" : "sampler2D";
			preflight << fmt::format("uniform float bumpScale{};\n", i)
			          << fmt::format("uniform float bumpOffset{};\n", i)
			          << fmt::format("uniform mat2 bumpMat{};\n", i);

			if (ps->state.snorm_tex[ps->input_tex[i]])
			{
				// Input color channels already signed (FIXME: May not always want signed textures in this case)
				vars << fmt::format(
				    "vec3 dsdtl{} = vec3(t{}.bg, sign3_to_0_to_1(t{}.r));\n", i, ps->input_tex[i], ps->input_tex[i]);
			}
			else
			{
				// Convert to signed (FIXME: loss of accuracy due to filtering/interpolation)
				vars << fmt::format(
				    "vec3 dsdtl{} = vec3(sign3(t{}.b), sign3(t{}.g), t{}.r);\n", i, ps->input_tex[i], ps->input_tex[i],
				    ps->input_tex[i]);
			}

			vars << fmt::format("dsdtl{}.st = bumpMat{} * dsdtl{}.st;\n", i, i, i)
			     << fmt::format("vec4 t{} = texture(texSamp{}, texScale{} * (pT{}.xy + dsdtl{}.st));\n", i, i, i, i, i)
			     << fmt::format("t{} = t{} * (bumpScale{} * dsdtl{}.p + bumpOffset{});\n", i, i, i, i, i);
			break;
		case PS_TEXTUREMODES_BRDF:
			assert(i >= 2);
			vars << fmt::format("vec4 t{} = vec4(0.0); /* PS_TEXTUREMODES_BRDF */\n", i);
			NV2A_UNIMPLEMENTED("PS_TEXTUREMODES_BRDF");
			break;
		case PS_TEXTUREMODES_DOT_ST:
			assert(i >= 2);
			sampler_type = ps->state.rect_tex[i] ? "sampler2DRect" : "sampler2D";
			vars << "/* PS_TEXTUREMODES_DOT_ST */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec4 t{} = texture(texSamp{}, texScale{} * vec2(dot{}, dot{}));\n", i, i, i, i - 1, i);
			break;
		case PS_TEXTUREMODES_DOT_ZW:
			assert(i >= 2);
			vars << "/* PS_TEXTUREMODES_DOT_ZW */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec4 t{} = vec4(0.0);\n", i);
			// FIXME: vars << fmt::format("gl_FragDepth = t{}.x;\n", i);
			break;
		case PS_TEXTUREMODES_DOT_RFLCT_DIFF:
			assert(i == 2);
			sampler_type = "samplerCube";
			vars << "/* PS_TEXTUREMODES_DOT_RFLCT_DIFF */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i]);
			assert(ps->dot_map[i + 1] < 8);
			vars << fmt::format(
			    "float dot{}_n = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i + 1, dotmap_funcs[ps->dot_map[i + 1]],
			    ps->input_tex[i + 1])
			     << fmt::format("vec3 n_{} = vec3(dot{}, dot{}, dot{}_n);\n", i, i - 1, i, i)
			     << fmt::format("vec4 t{} = texture(texSamp{}, n_{});\n", i, i, i);
			break;
		case PS_TEXTUREMODES_DOT_RFLCT_SPEC:
			assert(i == 3);
			sampler_type = "samplerCube";
			vars << "/* PS_TEXTUREMODES_DOT_RFLCT_SPEC */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec3 n_{} = vec3(dot{}, dot{}, dot{});\n", i, i - 2, i - 1, i)
			     << fmt::format("vec3 e_{} = vec3(pT{}.w, pT{}.w, pT{}.w);\n", i, i - 2, i - 1, i)
			     << fmt::format("vec3 rv_{} = 2*n_{}*dot(n_{},e_{})/dot(n_{},n_{}) - e_{};\n", i, i, i, i, i, i, i)
			     << fmt::format("vec4 t{} = texture(texSamp{}, rv_{});\n", i, i, i);
			break;
		case PS_TEXTUREMODES_DOT_STR_3D:
			assert(i == 3);
			sampler_type = "sampler3D";
			vars << "/* PS_TEXTUREMODES_DOT_STR_3D */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec4 t{} = texture(texSamp{}, vec3(dot{}, dot{}, dot{}));\n", i, i, i - 2, i - 1, i);
			break;
		case PS_TEXTUREMODES_DOT_STR_CUBE:
			assert(i == 3);
			sampler_type = "samplerCube";
			vars << "/* PS_TEXTUREMODES_DOT_STR_CUBE */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec4 t{} = texture(texSamp{}, vec3(dot{}, dot{}, dot{}));\n", i, i, i - 2, i - 1, i);
			break;
		case PS_TEXTUREMODES_DPNDNT_AR:
			assert(i >= 1);
			assert(!ps->state.rect_tex[i]);
			sampler_type = "sampler2D";
			vars << fmt::format("vec4 t{} = texture(texSamp{}, t{}.ar);\n", i, i, ps->input_tex[i]);
			break;
		case PS_TEXTUREMODES_DPNDNT_GB:
			assert(i >= 1);
			assert(!ps->state.rect_tex[i]);
			sampler_type = "sampler2D";
			vars << fmt::format("vec4 t{} = texture(texSamp{}, t{}.gb);\n", i, i, ps->input_tex[i]);
			break;
		case PS_TEXTUREMODES_DOTPRODUCT:
			assert(i == 1 || i == 2);
			vars << "/* PS_TEXTUREMODES_DOTPRODUCT */\n"
			     << fmt::format("float dot{} = dot(pT{}.xyz, {}(t{}.rgb));\n", i, i, dotmap_func, ps->input_tex[i])
			     << fmt::format("vec4 t{} = vec4(0.0);\n", i);
			break;
		case PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST:
			assert(i == 3);
			vars << fmt::format("vec4 t{} = vec4(0.0); /* PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST */\n", i);
			NV2A_UNIMPLEMENTED("PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST");
			break;
		default:
			fprintf(stderr, "Unknown ps tex mode: 0x%x\n", ps->tex_modes[i]);
			assert(false);
			break;
		}

		preflight << fmt::format("uniform float texScale{};\n", i);
		if (sampler_type != NULL)
		{
			preflight << fmt::format("uniform {} texSamp{};\n", sampler_type, i);

			/* As this means a texture fetch does happen, do alphakill */
			if (ps->state.alphakill[i])
				vars << fmt::format("if (t{}.a == 0.0) {{ discard; }};\n", i);
		}
	}

	for (int i = 0; i < ps->num_stages; i++)
	{
		ps->cur_stage = i;
		ps->code << fmt::format("// Stage {}\n", i);
		add_stage_code(ps, ps->stage[i].rgb_input, ps->stage[i].rgb_output, "rgb", false);
		add_stage_code(ps, ps->stage[i].alpha_input, ps->stage[i].alpha_output, "a", true);
	}

	if (ps->final_input.enabled)
	{
		ps->cur_stage = 8;
		ps->code << "// Final Combiner\n";
		add_final_stage_code(ps, ps->final_input);
	}

	if (ps->state.alpha_test && ps->state.alpha_func != ALPHA_FUNC_ALWAYS)
	{
		preflight << "uniform float alphaRef;\n";
		if (ps->state.alpha_func == ALPHA_FUNC_NEVER)
			ps->code << "discard;\n";
		else
		{
			const char* alpha_op;
			switch (ps->state.alpha_func)
			{
			case ALPHA_FUNC_LESS: alpha_op = "<"; break;
			case ALPHA_FUNC_EQUAL: alpha_op = "=="; break;
			case ALPHA_FUNC_LEQUAL: alpha_op = "<="; break;
			case ALPHA_FUNC_GREATER: alpha_op = ">"; break;
			case ALPHA_FUNC_NOTEQUAL: alpha_op = "!="; break;
			case ALPHA_FUNC_GEQUAL: alpha_op = ">="; break;
			default:
				assert(false);
				break;
			}
			ps->code << fmt::format("if (!(fragColor.a {} alphaRef)) discard;\n", alpha_op);
		}
	}

	for (int i = 0; i < ps->num_const_refs; i++)
		preflight << fmt::format("uniform vec4 {};\n", ps->const_refs[i]);

	for (int i = 0; i < ps->num_var_refs; i++)
	{
		vars << fmt::format("vec4 {};\n", ps->var_refs[i]);
		if (strcmp(ps->var_refs[i], "r0") == 0)
		{
			if (ps->tex_modes[0] != PS_TEXTUREMODES_NONE)
				vars << "r0.a = t0.a;\n";
			else
				vars << "r0.a = 1.0;\n";
		}
	}

	std::stringstream final;
	final << "#version 330\n\n"
	      << preflight.str()
	      << "void main() {\n"
	      << clip.str()
	      << vars.str()
	      << ps->code.str()
	      << "}\n";

	return final.str();
}

static void parse_input(InputInfo* var, int value)
{
	var->reg  = value & 0xF;
	var->chan = value & 0x10;
	var->mod  = value & 0xE0;
}

static void parse_combiner_inputs(uint32_t value, InputInfo* a, InputInfo* b, InputInfo* c, InputInfo* d)
{
	parse_input(d, value & 0xFF);
	parse_input(c, (value >> 8) & 0xFF);
	parse_input(b, (value >> 16) & 0xFF);
	parse_input(a, (value >> 24) & 0xFF);
}

static void parse_combiner_output(uint32_t value, OutputInfo* out)
{
	out->cd           = value & 0xF;
	out->ab           = (value >> 4) & 0xF;
	out->muxsum       = (value >> 8) & 0xF;
	int flags         = value >> 12;
	out->flags        = flags;
	out->cd_op        = flags & 1;
	out->ab_op        = flags & 2;
	out->muxsum_op    = flags & 4;
	out->mapping      = flags & 0x38;
	out->ab_alphablue = flags & 0x80;
	out->cd_alphablue = flags & 0x40;
}

std::string psh_translate(const PshState state)
{
	PixelShader ps = { 0 };
	ps.state       = state;

	ps.num_stages = state.combiner_control & 0xFF;
	ps.flags      = state.combiner_control >> 8;
	for (int i = 0; i < 4; i++)
		ps.tex_modes[i] = (state.shader_stage_program >> (i * 5)) & 0x1F;

	ps.dot_map[0] = 0;
	ps.dot_map[1] = (state.other_stage_input >> 0) & 0xf;
	ps.dot_map[2] = (state.other_stage_input >> 4) & 0xf;
	ps.dot_map[3] = (state.other_stage_input >> 8) & 0xf;

	ps.input_tex[0] = -1;
	ps.input_tex[1] = 0;
	ps.input_tex[2] = (state.other_stage_input >> 16) & 0xF;
	ps.input_tex[3] = (state.other_stage_input >> 20) & 0xF;
	for (int i = 0; i < ps.num_stages; i++)
	{
		parse_combiner_inputs(state.rgb_inputs[i], &ps.stage[i].rgb_input.a, &ps.stage[i].rgb_input.b, &ps.stage[i].rgb_input.c, &ps.stage[i].rgb_input.d);
		parse_combiner_inputs(state.alpha_inputs[i], &ps.stage[i].alpha_input.a, &ps.stage[i].alpha_input.b, &ps.stage[i].alpha_input.c, &ps.stage[i].alpha_input.d);

		parse_combiner_output(state.rgb_outputs[i], &ps.stage[i].rgb_output);
		parse_combiner_output(state.alpha_outputs[i], &ps.stage[i].alpha_output);
	}

	InputInfo blank;
	ps.final_input.enabled = state.final_inputs_0 || state.final_inputs_1;
	if (ps.final_input.enabled)
	{
		parse_combiner_inputs(state.final_inputs_0, &ps.final_input.a, &ps.final_input.b, &ps.final_input.c, &ps.final_input.d);
		parse_combiner_inputs(state.final_inputs_1, &ps.final_input.e, &ps.final_input.f, &ps.final_input.g, &blank);

		int flags                = state.final_inputs_1 & 0xFF;
		ps.final_input.clamp_sum = flags & PS_FINALCOMBINERSETTING_CLAMP_SUM;
		ps.final_input.inv_v1    = flags & PS_FINALCOMBINERSETTING_COMPLEMENT_V1;
		ps.final_input.inv_r0    = flags & PS_FINALCOMBINERSETTING_COMPLEMENT_R0;
	}

	return psh_convert(&ps);
}
