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

#include "xemu-shaders.h"
#include "xemu-custom-widgets.h"

#include "data/controller_mask.png.h"
#include "data/logo_sdf.png.h"

static DecalShader decalMask;
static DecalShader decalLogo;
GLuint             main_fb;
FBO*               controller_fbo;
FBO*               logo_fbo;
GLint              vp[4];
GLuint             g_ui_tex;
GLuint             g_logo_tex;

struct rect
{
	int x, y, w, h;
};

const rect tex_items[] = {
	{ 0, 148, 467, 364 }, // obj_controller
	{ 0, 81, 67, 67 },    // obj_lstick
	{ 0, 14, 67, 67 },    // obj_rstick
};

enum tex_item_names
{
	obj_controller,
	obj_lstick,
	obj_rstick,
};

void initialize_custom_ui_rendering()
{
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint*)&main_fb);
	glGetIntegerv(GL_VIEWPORT, vp);

	glActiveTexture(GL_TEXTURE0);
	g_ui_tex = load_texture_from_memory(controller_mask_data, controller_mask_size, 1);
	create_decal_shader(decalMask, SHADER_TYPE_MASK);
	g_logo_tex = load_texture_from_memory(logo_sdf_data, logo_sdf_size, 1);
	create_decal_shader(decalLogo, SHADER_TYPE_LOGO);
	controller_fbo = create_fbo(512, 512);
	logo_fbo       = create_fbo(512, 512);
	render_to_default_fb();
}

void render_meter(DecalShader& decalMask, float x, float y, float width, float height, float p, uint32_t color_bg, uint32_t color_fg)
{
	render_decal(decalMask, x, y, width, height, 0, 0, 1, 1, 0, 0, color_bg);
	render_decal(decalMask, x, y, width * p, height, 0, 0, 1, 1, 0, 0, color_fg);
}

void render_controller(float frame_x, float frame_y, uint32_t primary_color, uint32_t secondary_color, ControllerState* state)
{
	// Location within the controller texture of masked button locations,
	// relative to the origin of the controller
	const rect jewel       = { 177, 172, 113, 118 };
	const rect lstick_ctr  = { 93, 246, 0, 0 };
	const rect rstick_ctr  = { 342, 148, 0, 0 };
	const rect buttons[12] = {
		{ 367, 187, 30, 38 }, // A
		{ 368, 229, 30, 38 }, // B
		{ 330, 204, 30, 38 }, // X
		{ 331, 247, 30, 38 }, // Y
		{ 82, 121, 31, 47 },  // D-Left
		{ 104, 160, 44, 25 }, // D-Up
		{ 141, 121, 31, 47 }, // D-Right
		{ 104, 105, 44, 25 }, // D-Down
		{ 187, 94, 34, 24 },  // Back
		{ 246, 94, 36, 26 },  // Start
		{ 348, 288, 30, 38 }, // White
		{ 386, 268, 30, 38 }, // Black
	};

	uint8_t  alpha = 0;
	uint32_t now   = SDL_GetTicks();
	float    t;

	glUseProgram(decalMask.prog);
	glBindVertexArray(decalMask.vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_ui_tex);

	// Add a 5 pixel space around the controller so we can wiggle the controller
	// around to visualize rumble in action
	frame_x += 5;
	frame_y += 5;
	float original_frame_x = frame_x;
	float original_frame_y = frame_y;

	// Floating point versions that will get scaled
	float rumble_l = 0;
	float rumble_r = 0;

	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ZERO);

	uint32_t jewel_color = secondary_color;

	// Check to see if the guide button is pressed
	const uint32_t animate_guide_button_duration = 2000;
	if (state->buttons & PAD_BUTTON_GUIDE)
		state->animate_guide_button_end = now + animate_guide_button_duration;

	if (now < state->animate_guide_button_end)
	{
		t             = 1.0f - (float)(state->animate_guide_button_end - now) / (float)animate_guide_button_duration;
		float sin_wav = (1 - sin(M_PI * t / 2.0f));

		// Animate guide button by highlighting logo jewel and fading out over time
		alpha       = sin_wav * 255.0f;
		jewel_color = primary_color + alpha;

		// Add a little extra flare: wiggle the frame around while we rumble
		frame_x += ((float)(rand() % 5) - 2.5) * (1 - t);
		frame_y += ((float)(rand() % 5) - 2.5) * (1 - t);
		rumble_l = rumble_r = sin_wav;
	}

	// Render controller texture
	render_decal(
	    decalMask, frame_x + 0, frame_y + 0, tex_items[obj_controller].w, tex_items[obj_controller].h,
	    tex_items[obj_controller].x, tex_items[obj_controller].y, tex_items[obj_controller].w, tex_items[obj_controller].h,
	    primary_color, secondary_color, 0);

	glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE); // Blend with controller cutouts
	render_decal(decalMask, frame_x + jewel.x, frame_y + jewel.y, jewel.w, jewel.h, 0, 0, 1, 1, 0, 0, jewel_color);

	// The controller has alpha cutouts where the buttons are. Draw a surface
	// behind the buttons if they are activated
	for (int i = 0; i < 12; i++)
	{
		bool enabled = !!(state->buttons & (1 << i));
		if (!enabled)
			continue;
		render_decal(
		    decalMask, frame_x + buttons[i].x, frame_y + buttons[i].y, buttons[i].w, buttons[i].h,
		    0, 0, 1, 1,
		    0, 0, (enabled ? primary_color : secondary_color) + 0xff);
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Blend with controller

	// Render left thumbstick
	float w        = tex_items[obj_lstick].w;
	float h        = tex_items[obj_lstick].h;
	float c_x      = frame_x + lstick_ctr.x;
	float c_y      = frame_y + lstick_ctr.y;
	float lstick_x = (float)state->axis[PAD_AXIS_LSTICK_X] / 32768.0;
	float lstick_y = (float)state->axis[PAD_AXIS_LSTICK_Y] / 32768.0;
	render_decal(
	    decalMask, (int)(c_x - w / 2.0f + 10.0f * lstick_x), (int)(c_y - h / 2.0f + 10.0f * lstick_y), w, h,
	    tex_items[obj_lstick].x, tex_items[obj_lstick].y, w, h,
	    (state->buttons & PAD_BUTTON_LSTICK) ? secondary_color : primary_color, (state->buttons & PAD_BUTTON_LSTICK) ? primary_color : secondary_color, 0);

	// Render right thumbstick
	w              = tex_items[obj_rstick].w;
	h              = tex_items[obj_rstick].h;
	c_x            = frame_x + rstick_ctr.x;
	c_y            = frame_y + rstick_ctr.y;
	float rstick_x = (float)state->axis[PAD_AXIS_RSTICK_X] / 32768.0;
	float rstick_y = (float)state->axis[PAD_AXIS_RSTICK_Y] / 32768.0;
	render_decal(
	    decalMask, (int)(c_x - w / 2.0f + 10.0f * rstick_x), (int)(c_y - h / 2.0f + 10.0f * rstick_y), w, h,
	    tex_items[obj_rstick].x, tex_items[obj_rstick].y, w, h,
	    (state->buttons & PAD_BUTTON_RSTICK) ? secondary_color : primary_color,
	    (state->buttons & PAD_BUTTON_RSTICK) ? primary_color : secondary_color, 0);

	glBlendFunc(GL_ONE, GL_ZERO); // Don't blend, just overwrite values in buffer

	// Render trigger bars
	float          ltrig                    = state->axis[PAD_AXIS_LTRIG] / 32767.0;
	float          rtrig                    = state->axis[PAD_AXIS_RTRIG] / 32767.0;
	const uint32_t animate_trigger_duration = 1000;
	if ((ltrig > 0) || (rtrig > 0))
	{
		state->animate_trigger_end = now + animate_trigger_duration;
		rumble_l                   = fmax(rumble_l, ltrig);
		rumble_r                   = fmax(rumble_r, rtrig);
	}

	// Animate trigger alpha down after a period of inactivity
	alpha = 0x80;
	if (state->animate_trigger_end > now)
	{
		t             = 1.0f - (float)(state->animate_trigger_end - now) / (float)animate_trigger_duration;
		float sin_wav = (1 - sin(M_PI * t / 2.0f));
		alpha += fmin(sin_wav * 0x40, 0x80);
	}

	render_meter(decalMask, original_frame_x + 10, original_frame_y + tex_items[obj_controller].h + 20, 150, 5, ltrig, primary_color + alpha, primary_color + 0xff);
	render_meter(decalMask, original_frame_x + tex_items[obj_controller].w - 160, original_frame_y + tex_items[obj_controller].h + 20, 150, 5, rtrig, primary_color + alpha, primary_color + 0xff);

	// Apply rumble updates
	state->rumble_l = (int)(rumble_l * (float)0xffff);
	state->rumble_r = (int)(rumble_r * (float)0xffff);
	xemu_input_update_rumble(state);

	glBindVertexArray(0);
	glUseProgram(0);
}

void render_logo(uint32_t time, uint32_t primary_color, uint32_t secondary_color, uint32_t fill_color)
{
	decalLogo.time = time;
	glUseProgram(decalLogo.prog);
	glBindVertexArray(decalMask.vao);
	glBlendFunc(GL_ONE, GL_ZERO);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_logo_tex);
	render_decal(decalLogo, 0, 0, 512, 512, 0, 0, 128, 128, primary_color, secondary_color, fill_color);
	glBindVertexArray(0);
	glUseProgram(0);
}
