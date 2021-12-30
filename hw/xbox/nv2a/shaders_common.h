/*
 * QEMU Geforce NV2A shader common definitions
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"

#define STRUCT_VERTEX_DATA \
	"struct VertexData {\n" \
	"  float inv_w;\n" \
	"  vec4 D0;\n" \
	"  vec4 D1;\n" \
	"  vec4 B0;\n" \
	"  vec4 B1;\n" \
	"  float Fog;\n" \
	"  vec4 T0;\n" \
	"  vec4 T1;\n" \
	"  vec4 T2;\n" \
	"  vec4 T3;\n" \
	"};\n"

#ifdef __cplusplus
}
#endif
