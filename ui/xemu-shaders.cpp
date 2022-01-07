/*
 * xemu User Interface Rendering Helpers
 *
 * Copyright (C) 2020-2021 Matt Borgerson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <SDL.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <math.h>
#include "ui.h"
#include "xemu-shaders.h"
#include "xsettings.h"
#include "ui/shader/xemu-logo-frag.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

GLuint compile_shader(GLenum type, const char* src)
{
	GLint  status;
	char   err_buf[512];
	GLuint shader = glCreateShader(type);
	if (shader == 0)
	{
		ui::LogError("ERROR: Failed to create shader");
		return 0;
	}
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE)
	{
		glGetShaderInfoLog(shader, sizeof(err_buf), NULL, err_buf);
		ui::LogError("ERROR: Shader compilation failed!");
		ui::LogInfo("[Shader Info Log]");
		ui::Log("%s", err_buf);
		ui::LogInfo("[Shader Source]");
		ui::Log("%s", src);
		return 0;
	}

	return shader;
}

void create_decal_shader(DecalShader& s, SHADER_TYPE type)
{
	// Allocate shader wrapper object
	s.flip         = 0;
	s.scale        = 1.4;
	s.smoothing    = 1.0;
	s.outline_dist = 1.0;
	s.time         = 0;

	const char* vert_src =
	    "#version 150 core\n"
	    "uniform bool in_FlipY;\n"
	    "uniform vec4 in_ScaleOffset;\n"
	    "uniform vec4 in_TexScaleOffset;\n"
	    "in vec2 in_Position;\n"
	    "in vec2 in_Texcoord;\n"
	    "out vec2 Texcoord;\n"
	    "void main() {\n"
	    "    vec2 t = in_Texcoord;\n"
	    "    if (in_FlipY) t.y = 1-t.y;\n"
	    "    Texcoord = t*in_TexScaleOffset.xy + in_TexScaleOffset.zw;\n"
	    "    gl_Position = vec4(in_Position*in_ScaleOffset.xy+in_ScaleOffset.zw, 0.0, 1.0);\n"
	    "}\n";
	GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
	assert(vert != 0);

	const char* image_frag_src =
	    "#version 150 core\n"
	    "uniform sampler2D tex;\n"
	    "in  vec2 Texcoord;\n"
	    "out vec4 out_Color;\n"
	    "void main() {\n"
	    "    out_Color.rgba = texture(tex, Texcoord);\n"
	    "}\n";

	const char* image_gamma_frag_src =
	    "#version 400 core\n"
	    "uniform sampler2D tex;\n"
	    "uniform uint palette[256];\n"
	    "float gamma_ch(int ch, float col)\n"
	    "{\n"
	    "    return float(bitfieldExtract(palette[uint(col * 255.0)], ch*8, 8)) / 255.0;\n"
	    "}\n"
	    "\n"
	    "vec4 gamma(vec4 col)\n"
	    "{\n"
	    "    return vec4(gamma_ch(0, col.r), gamma_ch(1, col.g), gamma_ch(2, col.b), col.a);\n"
	    "}\n"
	    "in  vec2 Texcoord;\n"
	    "out vec4 out_Color;\n"
	    "void main() {\n"
	    "    out_Color.rgba = gamma(texture(tex, Texcoord));\n"
	    "}\n";

	// Simple 2-color decal shader
	// - in_ColorFill is first pass
	// - Red channel of the texture is used as primary color, mixed with 1-Red for
	//   secondary color.
	// - Blue is a lazy alpha removal for now
	// - Alpha channel passed through
	const char* mask_frag_src =
	    "#version 150 core\n"
	    "uniform sampler2D tex;\n"
	    "uniform vec4 in_ColorPrimary;\n"
	    "uniform vec4 in_ColorSecondary;\n"
	    "uniform vec4 in_ColorFill;\n"
	    "in  vec2 Texcoord;\n"
	    "out vec4 out_Color;\n"
	    "void main() {\n"
	    "    vec4 t = texture(tex, Texcoord);\n"
	    "    out_Color.rgba = in_ColorFill.rgba;\n"
	    "    out_Color.rgb += mix(in_ColorSecondary.rgb, in_ColorPrimary.rgb, t.r);\n"
	    "    out_Color.a += t.a - t.b;\n"
	    "}\n";

	const char* frag_src = NULL;
	if (type == SHADER_TYPE_MASK)
		frag_src = mask_frag_src;
	else if (type == SHADER_TYPE_BLIT)
		frag_src = image_frag_src;
	else if (type == SHADER_TYPE_BLIT_GAMMA)
		frag_src = image_gamma_frag_src;
	else if (type == SHADER_TYPE_LOGO)
		frag_src = xemu_logo_frag_src;
	else
		assert(0);

	GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
	assert(frag != 0);

	// Link vertex and fragment shaders
	s.prog = glCreateProgram();
	glAttachShader(s.prog, vert);
	glAttachShader(s.prog, frag);
	glBindFragDataLocation(s.prog, 0, "out_Color");
	glLinkProgram(s.prog);
	glUseProgram(s.prog);

	// Flag shaders for deletion when program is deleted
	glDeleteShader(vert);
	glDeleteShader(frag);

	s.FlipY_loc          = glGetUniformLocation(s.prog, "in_FlipY");
	s.ScaleOffset_loc    = glGetUniformLocation(s.prog, "in_ScaleOffset");
	s.TexScaleOffset_loc = glGetUniformLocation(s.prog, "in_TexScaleOffset");
	s.tex_loc            = glGetUniformLocation(s.prog, "tex");
	s.ColorPrimary_loc   = glGetUniformLocation(s.prog, "in_ColorPrimary");
	s.ColorSecondary_loc = glGetUniformLocation(s.prog, "in_ColorSecondary");
	s.ColorFill_loc      = glGetUniformLocation(s.prog, "in_ColorFill");
	s.time_loc           = glGetUniformLocation(s.prog, "iTime");
	s.scale_loc          = glGetUniformLocation(s.prog, "scale");
	for (int i = 0; i < 256; i++)
	{
		char name[64];
		snprintf(name, sizeof(name), "palette[%d]", i);
		s.palette_loc[i] = glGetUniformLocation(s.prog, name);
	}

	// Create a vertex array object
	glGenVertexArrays(1, &s.vao);
	glBindVertexArray(s.vao);

	// Populate vertex buffer
	glGenBuffers(1, &s.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
	const GLfloat verts[6][4] = {
		// x      y      s      t
		{ -1.0f, -1.0f, 0.0f, 0.0f }, // BL
		{ -1.0f, 1.0f, 0.0f, 1.0f },  // TL
		{ 1.0f, 1.0f, 1.0f, 1.0f },   // TR
		{ 1.0f, -1.0f, 1.0f, 0.0f },  // BR
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_COPY);

	// Populate element buffer
	glGenBuffers(1, &s.ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ebo);
	const GLint indicies[] = { 0, 1, 2, 3 };
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indicies), indicies, GL_STATIC_DRAW);

	// Bind vertex position attribute
	GLint pos_attr_loc = glGetAttribLocation(s.prog, "in_Position");
	glVertexAttribPointer(pos_attr_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
	glEnableVertexAttribArray(pos_attr_loc);

	// Bind vertex texture coordinate attribute
	GLint tex_attr_loc = glGetAttribLocation(s.prog, "in_Texcoord");
	if (tex_attr_loc >= 0)
	{
		glVertexAttribPointer(tex_attr_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
		glEnableVertexAttribArray(tex_attr_loc);
	}
}

GLuint load_texture(unsigned char* data, int width, int height, int channels)
{
	GLuint tex;
	glGenTextures(1, &tex);
	assert(tex != 0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, xsettings.shader_nearest ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, xsettings.shader_nearest ? GL_NEAREST : GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	return tex;
}

GLuint load_texture_from_file(const char* name, int flip)
{
	stbi_set_flip_vertically_on_load(flip);

	// Read file into memory
	int            width, height, channels = 0;
	unsigned char* data = stbi_load(name, &width, &height, &channels, 4);
	assert(data != NULL);

	GLuint tex = load_texture(data, width, height, channels);
	stbi_image_free(data);

	return tex;
}

GLuint load_texture_from_memory(const unsigned char* buf, unsigned int size)
{
	// Flip vertically so textures are loaded according to GL convention.
	stbi_set_flip_vertically_on_load(1);

	int            width, height, channels = 0;
	unsigned char* data = stbi_load_from_memory(buf, size, &width, &height, &channels, 4);
	assert(data != NULL);

	GLuint tex = load_texture(data, width, height, channels);
	stbi_image_free(data);

	return tex;
}

void render_decal(DecalShader& s, float x, float y, float w, float h, float tex_x, float tex_y, float tex_w, float tex_h, uint32_t primary, uint32_t secondary, uint32_t fill)
{
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	float ww = vp[2], wh = vp[3];

	x     = (int)x;
	y     = (int)y;
	w     = (int)w;
	h     = (int)h;
	tex_x = (int)tex_x;
	tex_y = (int)tex_y;
	tex_w = (int)tex_w;
	tex_h = (int)tex_h;

	int tw_i, th_i;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw_i);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th_i);
	float tw = tw_i, th = th_i;

#define COL(color, c) (float)(((color) >> ((c)*8)) & 0xff) / 255.0
	glUniform1i(s.FlipY_loc, s.flip);
	glUniform4f(s.ScaleOffset_loc, w / ww, h / wh, -1 + ((2 * x + w) / ww), -1 + ((2 * y + h) / wh));
	glUniform4f(s.TexScaleOffset_loc, tex_w / tw, tex_h / th, tex_x / tw, tex_y / th);
	glUniform1i(s.tex_loc, 0);
	glUniform4f(s.ColorPrimary_loc, COL(primary, 3), COL(primary, 2), COL(primary, 1), COL(primary, 0));
	glUniform4f(s.ColorSecondary_loc, COL(secondary, 3), COL(secondary, 2), COL(secondary, 1), COL(secondary, 0));
	glUniform4f(s.ColorFill_loc, COL(fill, 3), COL(fill, 2), COL(fill, 1), COL(fill, 0));
	if (s.time_loc >= 0)
		glUniform1f(s.time_loc, s.time / 1000.0f);
	if (s.scale_loc >= 0)
		glUniform1f(s.scale_loc, s.scale);
#undef COL
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);
}

void render_decal_image(DecalShader& s, float x, float y, float w, float h, float tex_x, float tex_y, float tex_w, float tex_h)
{
	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	float ww = vp[2], wh = vp[3];

	int tw_i, th_i;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw_i);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th_i);
	float tw = tw_i, th = th_i;

	glUniform1i(s.FlipY_loc, s.flip);
	glUniform4f(s.ScaleOffset_loc, w / ww, h / wh, -1 + ((2 * x + w) / ww), -1 + ((2 * y + h) / wh));
	glUniform4f(s.TexScaleOffset_loc, tex_w / tw, tex_h / th, tex_x / tw, tex_y / th);
	glUniform1i(s.tex_loc, 0);
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, NULL);
}

FBO* create_fbo(int width, int height)
{
	FBO* fbo = new FBO();
	assert(fbo != NULL);

	fbo->w = width;
	fbo->h = height;

	// Allocate the texture
	glGenTextures(1, &fbo->tex);
	glBindTexture(GL_TEXTURE_2D, fbo->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, xsettings.fbo_nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, xsettings.fbo_nearest ? GL_NEAREST : GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo->w, fbo->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// Allocate the framebuffer object
	glGenFramebuffers(1, &fbo->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo->tex, 0);
	GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, DrawBuffers);

	return fbo;
}

static GLboolean m_blend;

void render_to_default_fb(void)
{
	if (!m_blend)
		glDisable(GL_BLEND);

	// Restore default framebuffer, viewport, blending funciton
	glBindFramebuffer(GL_FRAMEBUFFER, main_fb);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

GLuint render_to_fbo(FBO* fbo)
{
	m_blend = glIsEnabled(GL_BLEND);
	if (!m_blend)
		glEnable(GL_BLEND);

	glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo);
	glViewport(0, 0, fbo->w, fbo->h);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	return fbo->tex;
}
