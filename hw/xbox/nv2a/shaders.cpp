/*
 * QEMU Geforce NV2A shader generator
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2020-2021 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/fast-hash.h"

#include <chrono>
#include <fmt/core.h>
#include <sstream>

#include "shaders_common.h"
#include "shaders.h"
#include "ui/xemu-notifications.h"
#include "ui/xsettings.h"

static double shaderCompileTime  = 0;
static int    shaderCompileCount = 0;
static double shaderStringTime   = 0;
static int    shaderStringCount  = 0;

extern std::string psh_translate(const PshState state);

extern void vsh_translate(uint16_t version, const uint32_t* tokens, unsigned int length, bool z_perspective, std::stringstream& header, std::stringstream& body);

static std::string generate_geometry_shader(enum ShaderPolygonMode polygon_front_mode, enum ShaderPolygonMode polygon_back_mode, enum ShaderPrimitiveMode primitive_mode, GLenum* gl_primitive_mode)
{
	// FIXME: Missing support for 2-sided-poly mode
	assert(polygon_front_mode == polygon_back_mode);
	enum ShaderPolygonMode polygon_mode = polygon_front_mode;

	// POINT mode shouldn't require any special work
	if (polygon_mode == POLY_MODE_POINT)
	{
		*gl_primitive_mode = GL_POINTS;
		return "";
	}

	// Handle LINE and FILL mode
	const char* layout_in  = NULL;
	const char* layout_out = NULL;
	const char* body       = NULL;
	switch (primitive_mode)
	{
	case PRIM_TYPE_POINTS:
		*gl_primitive_mode = GL_POINTS;
		return "";
	case PRIM_TYPE_LINES:
		*gl_primitive_mode = GL_LINES;
		return "";
	case PRIM_TYPE_LINE_LOOP:
		*gl_primitive_mode = GL_LINE_LOOP;
		return "";
	case PRIM_TYPE_LINE_STRIP:
		*gl_primitive_mode = GL_LINE_STRIP;
		return "";
	case PRIM_TYPE_TRIANGLES:
		*gl_primitive_mode = GL_TRIANGLES;
		if (polygon_mode == POLY_MODE_FILL)
			return "";

		assert(polygon_mode == POLY_MODE_LINE);
		layout_in  = "layout(triangles) in;\n";
		layout_out = "layout(line_strip, max_vertices = 4) out;\n";
		body =
		    "  emit_vertex(0);\n"
		    "  emit_vertex(1);\n"
		    "  emit_vertex(2);\n"
		    "  emit_vertex(0);\n"
		    "  EndPrimitive();\n";
		break;
	case PRIM_TYPE_TRIANGLE_STRIP:
		*gl_primitive_mode = GL_TRIANGLE_STRIP;
		if (polygon_mode == POLY_MODE_FILL)
			return "";

		assert(polygon_mode == POLY_MODE_LINE);
		layout_in  = "layout(triangles) in;\n";
		layout_out = "layout(line_strip, max_vertices = 4) out;\n";
		// Imagine a quad made of a tristrip, the comments tell you which vertex we are using
		body =
		    "  if ((gl_PrimitiveIDIn & 1) == 0) {\n"
		    "    if (gl_PrimitiveIDIn == 0) {\n"
		    "      emit_vertex(0);\n" // bottom right
		    "    }\n"
		    "    emit_vertex(1);\n" // top right
		    "    emit_vertex(2);\n" // bottom left
		    "    emit_vertex(0);\n" // bottom right
		    "  } else {\n"
		    "    emit_vertex(2);\n" // bottom left
		    "    emit_vertex(1);\n" // top left
		    "    emit_vertex(0);\n" // top right
		    "  }\n"
		    "  EndPrimitive();\n";
		break;
	case PRIM_TYPE_TRIANGLE_FAN:
		*gl_primitive_mode = GL_TRIANGLE_FAN;
		if (polygon_mode == POLY_MODE_FILL)
			return "";

		assert(polygon_mode == POLY_MODE_LINE);
		layout_in  = "layout(triangles) in;\n";
		layout_out = "layout(line_strip, max_vertices = 4) out;\n";
		body =
		    "  if (gl_PrimitiveIDIn == 0) {\n"
		    "    emit_vertex(0);\n"
		    "  }\n"
		    "  emit_vertex(1);\n"
		    "  emit_vertex(2);\n"
		    "  emit_vertex(0);\n"
		    "  EndPrimitive();\n";
		break;
	case PRIM_TYPE_QUADS:
		*gl_primitive_mode = GL_LINES_ADJACENCY;
		layout_in          = "layout(lines_adjacency) in;\n";
		if (polygon_mode == POLY_MODE_LINE)
		{
			layout_out = "layout(line_strip, max_vertices = 5) out;\n";
			body =
			    "  emit_vertex(0);\n"
			    "  emit_vertex(1);\n"
			    "  emit_vertex(2);\n"
			    "  emit_vertex(3);\n"
			    "  emit_vertex(0);\n"
			    "  EndPrimitive();\n";
		}
		else if (polygon_mode == POLY_MODE_FILL)
		{
			layout_out = "layout(triangle_strip, max_vertices = 4) out;\n";
			body =
			    "  emit_vertex(0);\n"
			    "  emit_vertex(1);\n"
			    "  emit_vertex(3);\n"
			    "  emit_vertex(2);\n"
			    "  EndPrimitive();\n";
		}
		else
		{
			assert(false);
			return "";
		}
		break;
	case PRIM_TYPE_QUAD_STRIP:
		*gl_primitive_mode = GL_LINE_STRIP_ADJACENCY;
		layout_in          = "layout(lines_adjacency) in;\n";
		if (polygon_mode == POLY_MODE_LINE)
		{
			layout_out = "layout(line_strip, max_vertices = 5) out;\n";
			body =
			    "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
			    "  if (gl_PrimitiveIDIn == 0) {\n"
			    "    emit_vertex(0);\n"
			    "  }\n"
			    "  emit_vertex(1);\n"
			    "  emit_vertex(3);\n"
			    "  emit_vertex(2);\n"
			    "  emit_vertex(0);\n"
			    "  EndPrimitive();\n";
		}
		else if (polygon_mode == POLY_MODE_FILL)
		{
			layout_out = "layout(triangle_strip, max_vertices = 4) out;\n";
			body =
			    "  if ((gl_PrimitiveIDIn & 1) != 0) { return; }\n"
			    "  emit_vertex(0);\n"
			    "  emit_vertex(1);\n"
			    "  emit_vertex(2);\n"
			    "  emit_vertex(3);\n"
			    "  EndPrimitive();\n";
		}
		else
		{
			assert(false);
			return "";
		}
		break;
	case PRIM_TYPE_POLYGON:
		if (polygon_mode == POLY_MODE_LINE)
			*gl_primitive_mode = GL_LINE_LOOP;
		else if (polygon_mode == POLY_MODE_FILL)
			*gl_primitive_mode = GL_TRIANGLE_FAN;
		else
			assert(false);
		return "";
	default:
		assert(false);
		return "";
	}

	// generate a geometry shader to support deprecated primitive types
	assert(layout_in);
	assert(layout_out);
	assert(body);

	std::stringstream ss;
	ss << "#version 330\n\n"
	   << layout_in
	   << layout_out
	   << "\n" STRUCT_VERTEX_DATA
	      "noperspective in VertexData v_vtx[];\n"
	      "noperspective out VertexData g_vtx;\n"
	      "\n"
	      "void emit_vertex(int index) {\n"
	      "  gl_Position = gl_in[index].gl_Position;\n"
	      "  gl_PointSize = gl_in[index].gl_PointSize;\n"
	      "  g_vtx = v_vtx[index];\n"
	      "  EmitVertex();\n"
	      "}\n"
	      "\n"
	      "void main() {\n"
	   << body << "}\n";

	return ss.str();
}

static void append_skinning_code(std::stringstream& str, bool mix, int count, const char* type, const char* output, const char* input, const char* matrix, const char* swizzle)
{
	if (count == 0)
		str << fmt::format("{} {} = ({} * {}0).{};\n", type, output, input, matrix, swizzle);
	else
	{
		str << fmt::format("{} {} = {}(0.0);\n", type, output, type);
		if (mix)
		{
			// Generated final weight (like GL_WEIGHT_SUM_UNITY_ARB)
			str << "{\n"
			       "  float weight_i;\n"
			       "  float weight_n = 1.0;\n";
			for (int i = 0; i < count; i++)
			{
				if (i < (count - 1))
				{
					char c = "xyzw"[i];
					str << fmt::format(
					    "  weight_i = weight.{};\n"
					    "  weight_n -= weight_i;\n",
					    c);
				}
				else
					str << "  weight_i = weight_n;\n";

				str << fmt::format("  {} += ({} * {}{}).{} * weight_i;\n", output, input, matrix, i, swizzle);
			}
			str << "}\n";
		}
		else
		{
			// Individual weights
			for (int i = 0; i < count; i++)
			{
				char c = "xyzw"[i];
				str << fmt::format("{} += ({} * {}{}).{} * weight.{};\n", output, input, matrix, i, swizzle, c);
			}
		}
	}
}

#define GLSL_C(idx)      "c[" stringify(idx) "]"
#define GLSL_LTCTXA(idx) "ltctxa[" stringify(idx) "]"

#define GLSL_C_MAT4(idx) "mat4(" GLSL_C(idx) ", " GLSL_C(idx + 1) ", " GLSL_C(idx + 2) ", " GLSL_C(idx + 3) ")"

#define GLSL_DEFINE(a, b) "#define " stringify(a) " " b "\n"

static void generate_fixed_function(const ShaderState state, std::stringstream& header, std::stringstream& body)
{
	// generate vertex shader mimicking fixed function
	header << "#define position      v0\n"
	          "#define weight        v1\n"
	          "#define normal        v2.xyz\n"
	          "#define diffuse       v3\n"
	          "#define specular      v4\n"
	          "#define fogCoord      v5.x\n"
	          "#define pointSize     v6\n"
	          "#define backDiffuse   v7\n"
	          "#define backSpecular  v8\n"
	          "#define texture0      v9\n"
	          "#define texture1      v10\n"
	          "#define texture2      v11\n"
	          "#define texture3      v12\n"
	          "#define reserved1     v13\n"
	          "#define reserved2     v14\n"
	          "#define reserved3     v15\n"
	          "\n"
	          "uniform vec4 ltctxa[" stringify(NV2A_LTCTXA_COUNT)
	              "];\n"
	              "uniform vec4 ltctxb[" stringify(NV2A_LTCTXB_COUNT)
	                  "];\n"
	                  "uniform vec4 ltc1[" stringify(NV2A_LTC1_COUNT)
	                      "];\n"
	                      "\n" GLSL_DEFINE(projectionMat, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_PMAT0))
	                          GLSL_DEFINE(compositeMat, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_CMAT0)) "\n" GLSL_DEFINE(texPlaneS0, GLSL_C(NV_IGRAPH_XF_XFCTX_TG0MAT + 0))
	                              GLSL_DEFINE(texPlaneT0, GLSL_C(NV_IGRAPH_XF_XFCTX_TG0MAT + 1))
	                                  GLSL_DEFINE(texPlaneQ0, GLSL_C(NV_IGRAPH_XF_XFCTX_TG0MAT + 2))
	                                      GLSL_DEFINE(texPlaneR0, GLSL_C(NV_IGRAPH_XF_XFCTX_TG0MAT + 3)) "\n" GLSL_DEFINE(texPlaneS1, GLSL_C(NV_IGRAPH_XF_XFCTX_TG1MAT + 0))
	                                          GLSL_DEFINE(texPlaneT1, GLSL_C(NV_IGRAPH_XF_XFCTX_TG1MAT + 1))
	                                              GLSL_DEFINE(texPlaneQ1, GLSL_C(NV_IGRAPH_XF_XFCTX_TG1MAT + 2))
	                                                  GLSL_DEFINE(texPlaneR1, GLSL_C(NV_IGRAPH_XF_XFCTX_TG1MAT + 3)) "\n" GLSL_DEFINE(texPlaneS2, GLSL_C(NV_IGRAPH_XF_XFCTX_TG2MAT + 0))
	                                                      GLSL_DEFINE(texPlaneT2, GLSL_C(NV_IGRAPH_XF_XFCTX_TG2MAT + 1))
	                                                          GLSL_DEFINE(texPlaneQ2, GLSL_C(NV_IGRAPH_XF_XFCTX_TG2MAT + 2))
	                                                              GLSL_DEFINE(texPlaneR2, GLSL_C(NV_IGRAPH_XF_XFCTX_TG2MAT + 3)) "\n" GLSL_DEFINE(texPlaneS3, GLSL_C(NV_IGRAPH_XF_XFCTX_TG3MAT + 0))
	                                                                  GLSL_DEFINE(texPlaneT3, GLSL_C(NV_IGRAPH_XF_XFCTX_TG3MAT + 1))
	                                                                      GLSL_DEFINE(texPlaneQ3, GLSL_C(NV_IGRAPH_XF_XFCTX_TG3MAT + 2))
	                                                                          GLSL_DEFINE(texPlaneR3, GLSL_C(NV_IGRAPH_XF_XFCTX_TG3MAT + 3)) "\n" GLSL_DEFINE(modelViewMat0, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_MMAT0))
	                                                                              GLSL_DEFINE(modelViewMat1, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_MMAT1))
	                                                                                  GLSL_DEFINE(modelViewMat2, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_MMAT2))
	                                                                                      GLSL_DEFINE(modelViewMat3, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_MMAT3)) "\n" GLSL_DEFINE(invModelViewMat0, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_IMMAT0))
	                                                                                          GLSL_DEFINE(invModelViewMat1, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_IMMAT1))
	                                                                                              GLSL_DEFINE(invModelViewMat2, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_IMMAT2))
	                                                                                                  GLSL_DEFINE(invModelViewMat3, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_IMMAT3)) "\n" GLSL_DEFINE(eyePosition, GLSL_C(NV_IGRAPH_XF_XFCTX_EYEP))
	                                                                                                      "\n"
	                                                                                                      "#define lightAmbientColor(i) "
	                                                                                                      "ltctxb[" stringify(NV_IGRAPH_XF_LTCTXB_L0_AMB)
	                                                                                                          " + (i)*6].xyz\n"
	                                                                                                          "#define lightDiffuseColor(i) "
	                                                                                                          "ltctxb[" stringify(NV_IGRAPH_XF_LTCTXB_L0_DIF)
	                                                                                                              " + (i)*6].xyz\n"
	                                                                                                              "#define lightSpecularColor(i) "
	                                                                                                              "ltctxb[" stringify(NV_IGRAPH_XF_LTCTXB_L0_SPC)
	                                                                                                                  " + (i)*6].xyz\n"
	                                                                                                                  "\n"
	                                                                                                                  "#define lightSpotFalloff(i) "
	                                                                                                                  "ltctxa[" stringify(NV_IGRAPH_XF_LTCTXA_L0_K)
	                                                                                                                      " + (i)*2].xyz\n"
	                                                                                                                      "#define lightSpotDirection(i) "
	                                                                                                                      "ltctxa[" stringify(NV_IGRAPH_XF_LTCTXA_L0_SPT)
	                                                                                                                          " + (i)*2]\n"
	                                                                                                                          "\n"
	                                                                                                                          "#define lightLocalRange(i) "
	                                                                                                                          "ltc1[" stringify(NV_IGRAPH_XF_LTC1_r0)
	                                                                                                                              " + (i)].x\n"
	                                                                                                                              "\n" GLSL_DEFINE(sceneAmbientColor, GLSL_LTCTXA(NV_IGRAPH_XF_LTCTXA_FR_AMB) ".xyz")
	                                                                                                                                  GLSL_DEFINE(materialEmissionColor, GLSL_LTCTXA(NV_IGRAPH_XF_LTCTXA_CM_COL) ".xyz")
	                                                                                                                                      "\n"
	                                                                                                                                      "uniform mat4 invViewport;\n"
	                                                                                                                                      "\n";

	// Skinning
	int  count;
	bool mix;
	switch (state.skinning)
	{
	case SKINNING_OFF:
		mix   = false;
		count = 0;
		break;
	case SKINNING_1WEIGHTS:
		mix   = true;
		count = 2;
		break;
	case SKINNING_2WEIGHTS2MATRICES:
		mix   = false;
		count = 2;
		break;
	case SKINNING_2WEIGHTS:
		mix   = true;
		count = 3;
		break;
	case SKINNING_3WEIGHTS3MATRICES:
		mix   = false;
		count = 3;
		break;
	case SKINNING_3WEIGHTS:
		mix   = true;
		count = 4;
		break;
	case SKINNING_4WEIGHTS4MATRICES:
		mix   = false;
		count = 4;
		break;
	default:
		assert(false);
		break;
	}

	body << fmt::format("/* Skinning mode {} */\n", state.skinning);

	append_skinning_code(body, mix, count, "vec4", "tPosition", "position", "modelViewMat", "xyzw");
	append_skinning_code(body, mix, count, "vec3", "tNormal", "vec4(normal, 0.0)", "invModelViewMat", "xyz");

	// Normalization
	if (state.normalization)
		body << "tNormal = normalize(tNormal);\n";

	// Texgen
	for (int i = 0; i < NV2A_MAX_TEXTURES; i++)
	{
		body << fmt::format("/* Texgen for stage {} */\n", i);
		// Set each component individually
		// FIXME: could be nicer if some channels share the same texgen
		for (int j = 0; j < 4; j++)
		{
			// TODO: TexGen View Model missing!
			char c       = "xyzw"[j];
			char cSuffix = "STRQ"[j];
			switch (state.texgen[i][j])
			{
			case TEXGEN_DISABLE:
				body << fmt::format("oT{}.{} = texture{}.{};\n", i, c, i, c);
				break;
			case TEXGEN_EYE_LINEAR:
				body << fmt::format("oT{}.{} = dot(texPlane{}{}, tPosition);\n", i, c, cSuffix, i);
				break;
			case TEXGEN_OBJECT_LINEAR:
				body << fmt::format("oT{}.{} = dot(texPlane{}{}, position);\n", i, c, cSuffix, i);
				assert(false); // Untested
				break;
			case TEXGEN_SPHERE_MAP:
				assert(j < 2); // Channels S,T only!
				body << "{\n"
				     // FIXME: u, r and m only have to be calculated once
				     << "  vec3 u = normalize(tPosition.xyz);\n"
				     // FIXME: tNormal before or after normalization? Always normalize?
				     << "  vec3 r = reflect(u, tNormal);\n";

				/* FIXME: This would consume 1 division fewer and *might* be
				 *        faster than length:
				 *   // [z=1/(2*x) => z=1/x*0.5]
				 *   vec3 ro = r + vec3(0.0, 0.0, 1.0);
				 *   float m = inversesqrt(dot(ro,ro))*0.5;
				 */

				body << "  float invM = 1.0 / (2.0 * length(r + vec3(0.0, 0.0, 1.0)));\n"
				     << fmt::format("  oT{}.{} = r.{} * invM + 0.5;\n", i, c, c)
				     << "}\n";
				break;
			case TEXGEN_REFLECTION_MAP:
				assert(j < 3); // Channels S,T,R only!
				body << "{\n"
				     // FIXME: u and r only have to be calculated once, can share the one from SPHERE_MAP
				     << "  vec3 u = normalize(tPosition.xyz);\n"
				     << "  vec3 r = reflect(u, tNormal);\n"
				     << fmt::format("  oT{}.{} = r.{};\n", i, c, c)
				     << "}\n";
				break;
			case TEXGEN_NORMAL_MAP:
				assert(j < 3); // Channels S,T,R only!
				body << fmt::format("oT{}.{} = tNormal.{};\n", i, c, c);
				break;
			default:
				assert(false);
				break;
			}
		}
	}

	// Apply texture matrices
	for (int i = 0; i < NV2A_MAX_TEXTURES; i++)
	{
		if (state.texture_matrix_enable[i])
			body << fmt::format("oT{} = oT{} * texMat{};\n", i, i, i);
	}

	// Lighting
	if (state.lighting)
	{
		// FIXME: Do 2 passes if we want 2 sided-lighting?

		static char alpha_source_diffuse[]  = "diffuse.a";
		static char alpha_source_specular[] = "specular.a";
		static char alpha_source_material[] = "material_alpha";
		const char* alpha_source            = alpha_source_diffuse;
		if (state.diffuse_src == MATERIAL_COLOR_SRC_MATERIAL)
		{
			header << "uniform float material_alpha;\n";
			alpha_source = alpha_source_material;
		}
		else if (state.diffuse_src == MATERIAL_COLOR_SRC_SPECULAR)
			alpha_source = alpha_source_specular;

		if (state.ambient_src == MATERIAL_COLOR_SRC_MATERIAL)
			body << fmt::format("oD0 = vec4(sceneAmbientColor, {});\n", alpha_source);
		else if (state.ambient_src == MATERIAL_COLOR_SRC_DIFFUSE)
			body << fmt::format("oD0 = vec4(diffuse.rgb, {});\n", alpha_source);
		else if (state.ambient_src == MATERIAL_COLOR_SRC_SPECULAR)
			body << fmt::format("oD0 = vec4(specular.rgb, {});\n", alpha_source);

		body << "oD0.rgb *= materialEmissionColor.rgb;\n";
		if (state.emission_src == MATERIAL_COLOR_SRC_MATERIAL)
			body << "oD0.rgb += sceneAmbientColor;\n";
		else if (state.emission_src == MATERIAL_COLOR_SRC_DIFFUSE)
			body << "oD0.rgb += diffuse.rgb;\n";
		else if (state.emission_src == MATERIAL_COLOR_SRC_SPECULAR)
			body << "oD0.rgb += specular.rgb;\n";

		body << "oD1 = vec4(0.0, 0.0, 0.0, specular.a);\n";

		for (int i = 0; i < NV2A_MAX_LIGHTS; i++)
		{
			if (state.light[i] == LIGHT_OFF)
				continue;

			/* FIXME: It seems that we only have to handle the surface colors if
			 *        they are not part of the material [= vertex colors].
			 *        If they are material the cpu will premultiply light
			 *        colors
			 */

			body << fmt::format("/* Light {} */ {{\n", i);

			if (state.light[i] == LIGHT_LOCAL || state.light[i] == LIGHT_SPOT)
			{
				header << fmt::format(
				    "uniform vec3 lightLocalPosition{};\n"
				    "uniform vec3 lightLocalAttenuation{};\n",
				    i, i);
				body << fmt::format(
				    "  vec3 VP = lightLocalPosition{} - tPosition.xyz/tPosition.w;\n"
				    "  float d = length(VP);\n"
				    // FIXME: if (d > lightLocalRange) { .. don't process this light .. } /* inclusive?! */ - what about
				    // directional lights?
				    "  VP = normalize(VP);\n"
				    "  float attenuation = 1.0 / (lightLocalAttenuation{}.x\n"
				    "                               + lightLocalAttenuation{}.y * d\n"
				    "                               + lightLocalAttenuation{}.z * d * d);\n"
				    // FIXME: Not sure if eyePosition is correct
				    "  vec3 halfVector = normalize(VP + eyePosition.xyz / eyePosition.w);\n"
				    "  float nDotVP = max(0.0, dot(tNormal, VP));\n"
				    "  float nDotHV = max(0.0, dot(tNormal, halfVector));\n",
				    i, i, i, i);
			}

			switch (state.light[i])
			{
			case LIGHT_INFINITE:

				// lightLocalRange will be 1e+30 here

				header << fmt::format(
				    "uniform vec3 lightInfiniteHalfVector{};\n"
				    "uniform vec3 lightInfiniteDirection{};\n",
				    i, i);
				body << fmt::format(
				    "  float attenuation = 1.0;\n"
				    "  float nDotVP = max(0.0, dot(tNormal, normalize(vec3(lightInfiniteDirection{}))));\n"
				    "  float nDotHV = max(0.0, dot(tNormal, vec3(lightInfiniteHalfVector{})));\n",
				    i, i);

				// FIXME: Do specular

				// FIXME: tBackDiffuse

				break;
			case LIGHT_LOCAL:
				// Everything done already
				break;
			case LIGHT_SPOT:
				// https://docs.microsoft.com/en-us/windows/win32/direct3d9/attenuation-and-spotlight-factor#spotlight-factor
				body << fmt::format(
				    "  vec4 spotDir = lightSpotDirection({});\n"
				    "  float invScale = 1/length(spotDir.xyz);\n"
				    "  float cosHalfPhi = -invScale*spotDir.w;\n"
				    "  float cosHalfTheta = invScale + cosHalfPhi;\n"
				    "  float spotDirDotVP = dot(spotDir.xyz, VP);\n"
				    "  float rho = invScale*spotDirDotVP;\n"
				    "  if (rho > cosHalfTheta) {{\n"
				    "  }} else if (rho <= cosHalfPhi) {{\n"
				    "    attenuation = 0.0;\n"
				    "  }} else {{\n"
				    "    attenuation *= spotDirDotVP + spotDir.w;\n" // FIXME: lightSpotFalloff
				    "  }}\n",
				    i);
				break;
			default:
				assert(false);
				break;
			}

			body << fmt::format(
			    "  float pf;\n"
			    "  if (nDotVP == 0.0) {{\n"
			    "    pf = 0.0;\n"
			    "  }} else {{\n"
			    "    pf = pow(nDotHV, /* specular(l, m, n, l1, m1, n1) */ 0.001);\n"
			    "  }}\n"
			    "  vec3 lightAmbient = lightAmbientColor({}) * attenuation;\n"
			    "  vec3 lightDiffuse = lightDiffuseColor({}) * attenuation * nDotVP;\n"
			    "  vec3 lightSpecular = lightSpecularColor({}) * pf;\n",
			    i, i, i)
			     << "  oD0.xyz += lightAmbient;\n";

			switch (state.diffuse_src)
			{
			case MATERIAL_COLOR_SRC_MATERIAL:
				body << "  oD0.xyz += lightDiffuse;\n";
				break;
			case MATERIAL_COLOR_SRC_DIFFUSE:
				body << "  oD0.xyz += diffuse.xyz * lightDiffuse;\n";
				break;
			case MATERIAL_COLOR_SRC_SPECULAR:
				body << "  oD0.xyz += specular.xyz * lightDiffuse;\n";
				break;
			}

			body << "  oD1.xyz += specular.xyz * lightSpecular;\n"
			     << "}\n";
		}
	}
	else
	{
		body << "  oD0 = diffuse;\n"
		     << "  oD1 = specular;\n";
	}
	body << "  oB0 = backDiffuse;\n"
	     << "  oB1 = backSpecular;\n";

	// Fog
	if (state.fog_enable)
	{
		// From: https://www.opengl.org/registry/specs/NV/fog_distance.txt
		switch (state.foggen)
		{
		case FOGGEN_SPEC_ALPHA:
			// FIXME: Do we have to clamp here?
			body << "  float fogDistance = clamp(specular.a, 0.0, 1.0);\n";
			break;
		case FOGGEN_RADIAL:
			body << "  float fogDistance = length(tPosition.xyz);\n";
			break;
		case FOGGEN_PLANAR:
		case FOGGEN_ABS_PLANAR:
			body << "  float fogDistance = dot(fogPlane.xyz, tPosition.xyz) + fogPlane.w;\n";
			if (state.foggen == FOGGEN_ABS_PLANAR)
				body << "  fogDistance = abs(fogDistance);\n";
			break;
		case FOGGEN_FOG_X:
			body << "  float fogDistance = fogCoord;\n";
			break;
		default:
			assert(false);
			break;
		}
	}

	// If skinning is off the composite matrix already includes the MV matrix
	if (state.skinning == SKINNING_OFF)
		body << "  tPosition = position;\n";

	body << "   oPos = invViewport * (tPosition * compositeMat);\n"
	        "   oPos.z = oPos.z * 2.0 - oPos.w;\n";

	// FIXME: Testing
	if (state.point_params_enable)
	{
		body << fmt::format(
		    "  float d_e = length(position * modelViewMat0);\n"
		    "  oPts.x = 1/sqrt({} + {}*d_e + {}*d_e*d_e) + {};\n",
		    state.point_params[0], state.point_params[1], state.point_params[2], state.point_params[6]);
		body << fmt::format(
		    "  oPts.x = min(oPts.x*{} + {}, 64.0) * {};\n", state.point_params[3], state.point_params[7],
		    state.surface_scale_factor);
	}
	else
		body << fmt::format("  oPts.x = {} * {};\n", state.point_size, state.surface_scale_factor);

	body << "  vtx.inv_w = 1.0 / oPos.w;\n";
}

static std::string generate_vertex_shader(const ShaderState state, char vtx_prefix)
{
	std::stringstream header;
	header << "#version 400\n"
	          "\n"
	          "uniform vec2 clipRange;\n"
	          "uniform vec2 surfaceSize;\n"
	          "\n"
	          // All constants in 1 array declaration
	          "uniform vec4 c[" stringify(NV2A_VERTEXSHADER_CONSTANTS)
	              "];\n"
	              "\n"
	              "uniform vec4 fogColor;\n"
	              "uniform float fogParam[2];\n"
	              "\n"

	    GLSL_DEFINE(fogPlane, GLSL_C(NV_IGRAPH_XF_XFCTX_FOG))
	        GLSL_DEFINE(texMat0, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_T0MAT))
	            GLSL_DEFINE(texMat1, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_T1MAT))
	                GLSL_DEFINE(texMat2, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_T2MAT))
	                    GLSL_DEFINE(texMat3, GLSL_C_MAT4(NV_IGRAPH_XF_XFCTX_T3MAT))

	                        "\n"
	                        "vec4 oPos = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oD0 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oD1 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oB0 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oB1 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oPts = vec4(0.0,0.0,0.0,1.0);\n"
	                        /* FIXME: NV_vertex_program says: "FOGC is the transformed vertex's fog
	                         * coordinate. The register's first floating-point component is interpolated
	                         * across the assembled primitive during rasterization and used as the fog
	                         * distance to compute per-fragment the fog factor when fog is enabled.
	                         * However, if both fog and vertex program mode are enabled, but the FOGC
	                         * vertex result register is not written, the fog factor is overridden to
	                         * 1.0. The register's other three components are ignored."
	                         *
	                         * That probably means it will read back as vec4(0.0, 0.0, 0.0, 1.0) but
	                         * will be set to 1.0 AFTER the VP if it was never written?
	                         * We should test on real hardware..
	                         *
	                         * We'll force 1.0 for oFog.x for now.
	                         */
	                        "vec4 oFog = vec4(1.0,0.0,0.0,1.0);\n"
	                        "vec4 oT0 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oT1 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oT2 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "vec4 oT3 = vec4(0.0,0.0,0.0,1.0);\n"
	                        "\n"
	                        "vec4 decompress_11_11_10(int cmp) {\n"
	                        "    float x = float(bitfieldExtract(cmp, 0,  11)) / 1023.0;\n"
	                        "    float y = float(bitfieldExtract(cmp, 11, 11)) / 1023.0;\n"
	                        "    float z = float(bitfieldExtract(cmp, 22, 10)) / 511.0;\n"
	                        "    return vec4(x, y, z, 1);\n"
	                        "}\n" STRUCT_VERTEX_DATA;

	header << fmt::format("noperspective out VertexData {}_vtx;\n", vtx_prefix)
	       << fmt::format("#define vtx {}_vtx\n", vtx_prefix)
	       << "\n";
	for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++)
	{
		if (state.compressed_attrs & (1 << i))
			header << fmt::format("layout(location = {}) in int v{}_cmp;\n", i, i);
		else
			header << fmt::format("layout(location = {}) in vec4 v{};\n", i, i);
	}
	header << "\n";

	std::stringstream body;
	body << "void main() {\n";

	for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++)
	{
		if (state.compressed_attrs & (1 << i))
			body << fmt::format("vec4 v{} = decompress_11_11_10(v{}_cmp);\n", i, i);
	}

	if (state.fixed_function)
		generate_fixed_function(state, header, body);
	else if (state.vertex_program)
		vsh_translate(VSH_VERSION_XVS, (uint32_t*)state.program_data, state.program_length, state.z_perspective, header, body);
	else
		assert(false);

	// Fog

	if (state.fog_enable)
	{
		if (state.vertex_program)
		{
			/* FIXME: Does foggen do something here? Let's do some tracking..
			 *
			 *   "RollerCoaster Tycoon" has
			 *      state.vertex_program = true; state.foggen == FOGGEN_PLANAR
			 *      but expects oFog.x as fogdistance?! Writes oFog.xyzw = v0.z
			 */
			body << "  float fogDistance = oFog.x;\n";
		}

		// FIXME: Do this per pixel?
		switch (state.fog_mode)
		{
		case FOG_MODE_LINEAR:
		case FOG_MODE_LINEAR_ABS:

			/* f = (end - d) / (end - start)
			 *    fogParam[1] = -1 / (end - start)
			 *    fogParam[0] = 1 - end * fogParam[1];
			 */

			body << "  if (isinf(fogDistance)) {\n"
			     << "    fogDistance = 0.0;\n"
			     << "  }\n"
			     << "  float fogFactor = fogParam[0] + fogDistance * fogParam[1];\n"
			     << "  fogFactor -= 1.0;\n";
			break;
		case FOG_MODE_EXP:
			body << "  if (isinf(fogDistance)) {\n"
			     << "    fogDistance = 0.0;\n"
			     << "  }\n";
		// fallthru
		case FOG_MODE_EXP_ABS:

			/* f = 1 / (e^(d * density))
			 *    fogParam[1] = -density / (2 * ln(256))
			 *    fogParam[0] = 1.5
			 */

			body << "  float fogFactor = fogParam[0] + exp2(fogDistance * fogParam[1] * 16.0);\n"
			     << "  fogFactor -= 1.5;\n";
			break;
		case FOG_MODE_EXP2:
		case FOG_MODE_EXP2_ABS:

			/* f = 1 / (e^((d * density)^2))
			 *    fogParam[1] = -density / (2 * sqrt(ln(256)))
			 *    fogParam[0] = 1.5
			 */

			body << "  float fogFactor = fogParam[0] + exp2(-fogDistance * fogDistance * fogParam[1] * fogParam[1] * "
			        "32.0);\n"
			     << "  fogFactor -= 1.5;\n";
			break;
		default:
			assert(false);
			break;
		}
		// Calculate absolute for the modes which need it
		switch (state.fog_mode)
		{
		case FOG_MODE_LINEAR_ABS:
		case FOG_MODE_EXP_ABS:
		case FOG_MODE_EXP2_ABS:
			body << "  fogFactor = abs(fogFactor);\n";
			break;
		default:
			break;
		}
		body << "  oFog.xyzw = vec4(fogFactor);\n";
	}
	else
	{
		// FIXME: Is the fog still calculated / passed somehow?!
		body << "  oFog.xyzw = vec4(1.0);\n";
	}

	// Set outputs
	body << "\n"
	        "  vtx.D0 = clamp(oD0, 0.0, 1.0) * vtx.inv_w;\n"
	        "  vtx.D1 = clamp(oD1, 0.0, 1.0) * vtx.inv_w;\n"
	        "  vtx.B0 = clamp(oB0, 0.0, 1.0) * vtx.inv_w;\n"
	        "  vtx.B1 = clamp(oB1, 0.0, 1.0) * vtx.inv_w;\n"
	        "  vtx.Fog = oFog.x * vtx.inv_w;\n"
	        "  vtx.T0 = oT0 * vtx.inv_w;\n"
	        "  vtx.T1 = oT1 * vtx.inv_w;\n"
	        "  vtx.T2 = oT2 * vtx.inv_w;\n"
	        "  vtx.T3 = oT3 * vtx.inv_w;\n"
	        "  gl_Position = oPos;\n"
	        "  gl_PointSize = oPts.x;\n"
	        "\n"
	        "}\n";

	// Return combined header + source
	header << body.str();
	return header.str();
}

static GLuint create_gl_shader(GLenum gl_shader_type, const char* code, const char* name)
{
	GLint compiled = 0;

	NV2A_GL_DGROUP_BEGIN("Creating new %s", name);

	NV2A_DPRINTF("compile new %s, code:\n%s\n", name, code);

	GLuint shader = glCreateShader(gl_shader_type);
	glShaderSource(shader, 1, &code, 0);
	glCompileShader(shader);

	// Check it compiled
	compiled = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled)
	{
		GLint log_length;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
		GLchar* log = new GLchar[log_length];
		glGetShaderInfoLog(shader, log_length, NULL, log);
		fprintf(stderr, "%s\n\nnv2a: %s compilation failed: %s\n", code, name, log);
		delete[] log;

		NV2A_GL_DGROUP_END();
		abort();
	}

	NV2A_GL_DGROUP_END();

	return shader;
}

ShaderBinding* generate_shaders(const ShaderState state)
{
	auto start = std::chrono::steady_clock::now();

	int      i, j;
	char     tmp[256];
	uint64_t hash1 = 0;
	uint64_t hash2 = 0;

	char   vtx_prefix;
	GLuint program = glCreateProgram();

	// Create an option geometry shader and find primitive type
	GLenum gl_primitive_mode;
	auto   start1               = std::chrono::steady_clock::now();
	auto   geometry_shader_code = generate_geometry_shader(state.polygon_front_mode, state.polygon_back_mode, state.primitive_mode, &gl_primitive_mode);
	if (geometry_shader_code.size())
	{
		auto finish = std::chrono::steady_clock::now();
		shaderStringTime += std::chrono::duration_cast<std::chrono::microseconds>(finish - start1).count() * 0.001f;
		++shaderStringCount;

		hash1 = fast_hash((const uint8_t*)geometry_shader_code.c_str(), geometry_shader_code.size());

		GLuint geometry_shader = create_gl_shader(GL_GEOMETRY_SHADER, geometry_shader_code.c_str(), "geometry shader");
		glAttachShader(program, geometry_shader);
		vtx_prefix = 'v';
	}
	else
		vtx_prefix = 'g';

	// create the vertex shader
	auto start2             = std::chrono::steady_clock::now();
	auto vertex_shader_code = generate_vertex_shader(state, vtx_prefix);
	auto finish2            = std::chrono::steady_clock::now();
	shaderStringTime += std::chrono::duration_cast<std::chrono::microseconds>(finish2 - start2).count() * 0.001f;
	++shaderStringCount;

	hash2 = fast_hash((const uint8_t*)vertex_shader_code.c_str(), vertex_shader_code.size());

	GLuint vertex_shader = create_gl_shader(GL_VERTEX_SHADER, vertex_shader_code.c_str(), "vertex shader");
	glAttachShader(program, vertex_shader);

	// generate a fragment shader from register combiners
	auto start3               = std::chrono::steady_clock::now();
	auto fragment_shader_code = psh_translate(state.psh);
	auto finish3              = std::chrono::steady_clock::now();
	shaderStringTime += std::chrono::duration_cast<std::chrono::microseconds>(finish3 - start3).count() * 0.001f;
	++shaderStringCount;

	GLuint fragment_shader = create_gl_shader(GL_FRAGMENT_SHADER, fragment_shader_code.c_str(), "fragment shader");
	glAttachShader(program, fragment_shader);

	// link the program
	glLinkProgram(program);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		GLchar log[2048];
		glGetProgramInfoLog(program, 2048, NULL, log);
		fprintf(stderr, "nv2a: shader linking failed: %s\n", log);
		abort();
	}

	auto finish = std::chrono::steady_clock::now();
	shaderCompileTime += std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count() * 0.001f;
	++shaderCompileCount;

	if (xsettings.shader_hint)
	{
		sprintf_s(
			tmp, "generate_shaders %d %d %d / %.3f/%d=%.3f / %.3f/%d=%.3f",
			program, vertex_shader, fragment_shader, shaderStringTime, shaderStringCount, shaderStringTime / shaderStringCount, shaderCompileTime, shaderCompileCount, shaderCompileTime / shaderCompileCount);
		xemu_queue_notification(tmp, true);
	}
	glUseProgram(program);

	// set texture samplers
	for (i = 0; i < NV2A_MAX_TEXTURES; i++)
	{
		char samplerName[16];
		snprintf(samplerName, sizeof(samplerName), "texSamp%d", i);
		GLint texSampLoc = glGetUniformLocation(program, samplerName);
		if (texSampLoc >= 0)
			glUniform1i(texSampLoc, i);
	}

	// validate the program
	glValidateProgram(program);
	GLint valid = 0;
	glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
	if (!valid)
	{
		GLchar log[1024];
		glGetProgramInfoLog(program, 1024, NULL, log);
		fprintf(stderr, "nv2a: shader validation failed: %s\n", log);
		abort();
	}

	ShaderBinding* ret     = (ShaderBinding*)g_malloc0(sizeof(ShaderBinding));
	ret->gl_program        = program;
	ret->gl_primitive_mode = gl_primitive_mode;

	// lookup fragment shader uniforms
	for (i = 0; i < 9; i++)
	{
		for (j = 0; j < 2; j++)
		{
			snprintf(tmp, sizeof(tmp), "c%d_%d", j, i);
			ret->psh_constant_loc[i][j] = glGetUniformLocation(program, tmp);
		}
	}
	ret->alpha_ref_loc = glGetUniformLocation(program, "alphaRef");
	for (i = 1; i < NV2A_MAX_TEXTURES; i++)
	{
		snprintf(tmp, sizeof(tmp), "bumpMat%d", i);
		ret->bump_mat_loc[i] = glGetUniformLocation(program, tmp);
		snprintf(tmp, sizeof(tmp), "bumpScale%d", i);
		ret->bump_scale_loc[i] = glGetUniformLocation(program, tmp);
		snprintf(tmp, sizeof(tmp), "bumpOffset%d", i);
		ret->bump_offset_loc[i] = glGetUniformLocation(program, tmp);
	}

	for (int i = 0; i < NV2A_MAX_TEXTURES; i++)
	{
		snprintf(tmp, sizeof(tmp), "texScale%d", i);
		ret->tex_scale_loc[i] = glGetUniformLocation(program, tmp);
	}

	// lookup vertex shader uniforms
	for (i = 0; i < NV2A_VERTEXSHADER_CONSTANTS; i++)
	{
		snprintf(tmp, sizeof(tmp), "c[%d]", i);
		ret->vsh_constant_loc[i] = glGetUniformLocation(program, tmp);
	}
	ret->surface_size_loc = glGetUniformLocation(program, "surfaceSize");
	ret->clip_range_loc   = glGetUniformLocation(program, "clipRange");
	ret->fog_color_loc    = glGetUniformLocation(program, "fogColor");
	ret->fog_param_loc[0] = glGetUniformLocation(program, "fogParam[0]");
	ret->fog_param_loc[1] = glGetUniformLocation(program, "fogParam[1]");

	ret->inv_viewport_loc = glGetUniformLocation(program, "invViewport");
	for (i = 0; i < NV2A_LTCTXA_COUNT; i++)
	{
		snprintf(tmp, sizeof(tmp), "ltctxa[%d]", i);
		ret->ltctxa_loc[i] = glGetUniformLocation(program, tmp);
	}
	for (i = 0; i < NV2A_LTCTXB_COUNT; i++)
	{
		snprintf(tmp, sizeof(tmp), "ltctxb[%d]", i);
		ret->ltctxb_loc[i] = glGetUniformLocation(program, tmp);
	}
	for (i = 0; i < NV2A_LTC1_COUNT; i++)
	{
		snprintf(tmp, sizeof(tmp), "ltc1[%d]", i);
		ret->ltc1_loc[i] = glGetUniformLocation(program, tmp);
	}
	for (i = 0; i < NV2A_MAX_LIGHTS; i++)
	{
		snprintf(tmp, sizeof(tmp), "lightInfiniteHalfVector%d", i);
		ret->light_infinite_half_vector_loc[i] = glGetUniformLocation(program, tmp);
		snprintf(tmp, sizeof(tmp), "lightInfiniteDirection%d", i);
		ret->light_infinite_direction_loc[i] = glGetUniformLocation(program, tmp);

		snprintf(tmp, sizeof(tmp), "lightLocalPosition%d", i);
		ret->light_local_position_loc[i] = glGetUniformLocation(program, tmp);
		snprintf(tmp, sizeof(tmp), "lightLocalAttenuation%d", i);
		ret->light_local_attenuation_loc[i] = glGetUniformLocation(program, tmp);
	}
	for (i = 0; i < 8; i++)
	{
		snprintf(tmp, sizeof(tmp), "clipRegion[%d]", i);
		ret->clip_region_loc[i] = glGetUniformLocation(program, tmp);
	}

	if (state.fixed_function)
		ret->material_alpha_loc = glGetUniformLocation(program, "material_alpha");
	else
		ret->material_alpha_loc = -1;

	return ret;
}