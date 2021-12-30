/*
 * QEMU texture swizzling routines
 *
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2013 espes
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

void swizzle_box(const uint8_t* src_buf, uint32_t width, uint32_t height, uint32_t depth, uint8_t* dst_buf, uint32_t row_pitch, uint32_t slice_pitch, uint32_t bytes_per_pixel);
void unswizzle_box(const uint8_t* src_buf, uint32_t width, uint32_t height, uint32_t depth, uint8_t* dst_buf, uint32_t row_pitch, uint32_t slice_pitch, uint32_t bytes_per_pixel);
void unswizzle_rect(const uint8_t* src_buf, uint32_t width, uint32_t height, uint8_t* dst_buf, uint32_t pitch, uint32_t bytes_per_pixel);
void swizzle_rect(const uint8_t* src_buf, uint32_t width, uint32_t height, uint8_t* dst_buf, uint32_t pitch, uint32_t bytes_per_pixel);

#ifdef __cplusplus
}
#endif
