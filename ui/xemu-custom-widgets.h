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

#pragma once

#include <stdint.h>
#include "xemu-input.h"
#include "xemu-shaders.h"

void initialize_custom_ui_rendering(void);
void render_meter(DecalShader& s, float x, float y, float width, float height, float p, uint32_t color_bg, uint32_t color_fg);
void render_controller(float frame_x, float frame_y, uint32_t primary_color, uint32_t secondary_color, ControllerState* state);
void render_logo(uint32_t time, uint32_t primary_color, uint32_t secondary_color, uint32_t fill_color);
