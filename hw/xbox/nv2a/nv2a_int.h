/*
 * QEMU Geforce NV2A internal definitions
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2021 Matt Borgerson
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

#include <assert.h>

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/queue.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "migration/vmstate.h"
#include "sysemu/runstate.h"

#include "hw/hw.h"
#include "hw/display/vga.h"
#include "hw/display/vga_int.h"
#include "hw/display/vga_regs.h"
#include "hw/pci/pci.h"
// #include "cpu.h"

#include "swizzle.h"
#include "lru.h"
#include "gl/gloffscreen.h"

#include "nv2a.h"
#include "debug.h"
#include "shaders.h"
#include "nv2a_regs.h"

#define GET_MASK(v, mask) (((v) & (mask)) >> ctz32(mask))

#define SET_MASK(v, mask, val) \
	({ \
		const uint32_t __val = (val); \
		const uint32_t __mask = (mask); \
		(v) &= ~(__mask); \
		(v) |= ((__val) << ctz32(__mask)) & (__mask); \
	})

#define NV2A_DEVICE(obj) OBJECT_CHECK(NV2AState, (obj), "nv2a")

enum FIFOEngine
{
	ENGINE_SOFTWARE = 0,
	ENGINE_GRAPHICS = 1,
	ENGINE_DVD = 2,
};

typedef struct DMAObject
{
	uint32_t dma_class;
	uint32_t dma_target;
	hwaddr address;
	hwaddr limit;
} DMAObject;

typedef struct VertexAttribute
{
	bool dma_select;
	hwaddr offset;

	/* inline arrays are packed in order?
	 * Need to pass the offset to converted attributes */
	uint32_t inline_array_offset;

	float inline_value[4];

	uint32_t format;
	uint32_t size;	/* size of the data type */
	uint32_t count; /* number of components */
	uint32_t stride;

	bool needs_conversion;

	float* inline_buffer;
	bool inline_buffer_populated;

	GLint gl_count;
	GLenum gl_type;
	GLboolean gl_normalize;

	GLuint gl_inline_buffer;
} VertexAttribute;

typedef struct SurfaceFormatInfo
{
	uint32_t bytes_per_pixel;
	GLint gl_internal_format;
	GLenum gl_format;
	GLenum gl_type;
	GLenum gl_attachment;
} SurfaceFormatInfo;

typedef struct Surface
{
	bool draw_dirty;
	bool buffer_dirty;
	bool write_enabled_cache;
	uint32_t pitch;

	hwaddr offset;
} Surface;

typedef struct SurfaceShape
{
	uint32_t z_format;
	uint32_t color_format;
	uint32_t zeta_format;
	uint32_t log_width, log_height;
	uint32_t clip_x, clip_y;
	uint32_t clip_width, clip_height;
	uint32_t anti_aliasing;
} SurfaceShape;

typedef struct SurfaceBinding
{
	QTAILQ_ENTRY(SurfaceBinding) entry;
	// MemAccessCallback* access_cb;
	void* access_cb;

	hwaddr vram_addr;

	SurfaceFormatInfo fmt;
	SurfaceShape shape;
	uintptr_t dma_addr;
	uintptr_t dma_len;
	bool color;
	bool swizzle;

	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	size_t size;

	GLuint gl_buffer;

	int frame_time;
	int draw_time;
	bool draw_dirty;
	bool download_pending;
	bool upload_pending;
} SurfaceBinding;

typedef struct TextureShape
{
	bool cubemap;
	uint32_t dimensionality;
	uint32_t color_format;
	uint32_t levels;
	uint32_t width, height, depth;

	uint32_t min_mipmap_level, max_mipmap_level;
	uint32_t pitch;
} TextureShape;

typedef struct TextureBinding
{
	GLenum gl_target;
	GLuint gl_texture;
	uint32_t refcnt;
	int draw_time;
	uint64_t data_hash;
	uint32_t scale;
} TextureBinding;

typedef struct TextureKey
{
	TextureShape state;
	hwaddr texture_vram_offset;
	hwaddr texture_length;
	hwaddr palette_vram_offset;
	hwaddr palette_length;
} TextureKey;

typedef struct TextureLruNode
{
	LruNode node;
	TextureKey key;
	TextureBinding* binding;
	bool possibly_dirty;
} TextureLruNode;

typedef struct VertexKey
{
	size_t count;
	GLuint gl_type;
	GLboolean gl_normalize;
	size_t stride;
	hwaddr addr;
} VertexKey;

typedef struct VertexLruNode
{
	LruNode node;
	VertexKey key;
	GLuint gl_buffer;
	bool initialized;
} VertexLruNode;

typedef struct KelvinState
{
	hwaddr object_instance;
} KelvinState;

typedef struct ContextSurfaces2DState
{
	hwaddr object_instance;
	hwaddr dma_image_source;
	hwaddr dma_image_dest;
	uint32_t color_format;
	uint32_t source_pitch, dest_pitch;
	hwaddr source_offset, dest_offset;
} ContextSurfaces2DState;

typedef struct ImageBlitState
{
	hwaddr object_instance;
	hwaddr context_surfaces;
	uint32_t operation;
	uint32_t in_x, in_y;
	uint32_t out_x, out_y;
	uint32_t width, height;
} ImageBlitState;

typedef struct PGRAPHState
{
	QemuMutex lock;

	uint32_t pending_interrupts;
	uint32_t enabled_interrupts;

	int frame_time;
	int draw_time;

	struct s2t_rndr
	{
		GLuint fbo, vao, vbo, prog;
		GLuint tex_loc, surface_size_loc;
	} s2t_rndr;

	struct disp_rndr
	{
		GLuint fbo, vao, vbo, prog;
		GLuint display_size_loc;
		GLuint line_offset_loc;
		GLuint tex_loc;
		GLuint pvideo_tex;
		GLint pvideo_enable_loc;
		GLint pvideo_tex_loc;
		GLint pvideo_pos_loc;
		GLint palette_loc[256];
	} disp_rndr;

	/* subchannels state we're not sure the location of... */
	ContextSurfaces2DState context_surfaces_2d;
	ImageBlitState image_blit;
	KelvinState kelvin;

	hwaddr dma_color, dma_zeta;
	Surface surface_color, surface_zeta;
	uint32_t surface_type;
	SurfaceShape surface_shape;
	SurfaceShape last_surface_shape;
	QTAILQ_HEAD(, SurfaceBinding) surfaces;
	SurfaceBinding *color_binding, *zeta_binding;
	struct
	{
		int clip_x;
		int clip_width;
		int clip_y;
		int clip_height;
		int width;
		int height;
	} surface_binding_dim; // FIXME: Refactor

	hwaddr dma_a, dma_b;
	Lru texture_cache;
	struct TextureLruNode* texture_cache_entries;
	bool texture_dirty[NV2A_MAX_TEXTURES];
	TextureBinding* texture_binding[NV2A_MAX_TEXTURES];

	GHashTable* shader_cache;
	ShaderBinding* shader_binding;

	bool texture_matrix_enable[NV2A_MAX_TEXTURES];

	GLuint gl_framebuffer;

	GLuint gl_display_buffer;
	GLint gl_display_buffer_internal_format;
	GLsizei gl_display_buffer_width;
	GLsizei gl_display_buffer_height;
	GLenum gl_display_buffer_format;
	GLenum gl_display_buffer_type;

	hwaddr dma_state;
	hwaddr dma_notifies;
	hwaddr dma_semaphore;

	hwaddr dma_report;
	hwaddr report_offset;
	bool zpass_pixel_count_enable;
	uint32_t zpass_pixel_count_result;
	uint32_t gl_zpass_pixel_count_query_count;
	GLuint* gl_zpass_pixel_count_queries;

	hwaddr dma_vertex_a, dma_vertex_b;

	uint32_t primitive_mode;

	bool enable_vertex_program_write;

	uint32_t program_data[NV2A_MAX_TRANSFORM_PROGRAM_LENGTH][VSH_TOKEN_SIZE];
	bool program_data_dirty;

	uint32_t vsh_constants[NV2A_VERTEXSHADER_CONSTANTS][4];
	bool vsh_constants_dirty[NV2A_VERTEXSHADER_CONSTANTS];

	/* lighting constant arrays */
	uint32_t ltctxa[NV2A_LTCTXA_COUNT][4];
	bool ltctxa_dirty[NV2A_LTCTXA_COUNT];
	uint32_t ltctxb[NV2A_LTCTXB_COUNT][4];
	bool ltctxb_dirty[NV2A_LTCTXB_COUNT];
	uint32_t ltc1[NV2A_LTC1_COUNT][4];
	bool ltc1_dirty[NV2A_LTC1_COUNT];

	// should figure out where these are in lighting context
	float light_infinite_half_vector[NV2A_MAX_LIGHTS][3];
	float light_infinite_direction[NV2A_MAX_LIGHTS][3];
	float light_local_position[NV2A_MAX_LIGHTS][3];
	float light_local_attenuation[NV2A_MAX_LIGHTS][3];

	float point_params[8];

	VertexAttribute vertex_attributes[NV2A_VERTEXSHADER_ATTRIBUTES];
	uint16_t compressed_attrs;

	Lru element_cache;
	struct VertexLruNode* element_cache_entries;

	uint32_t inline_array_length;
	uint32_t inline_array[NV2A_MAX_BATCH_LENGTH];
	GLuint gl_inline_array_buffer;

	uint32_t inline_elements_length;
	uint32_t inline_elements[NV2A_MAX_BATCH_LENGTH];

	uint32_t inline_buffer_length;

	uint32_t draw_arrays_length;
	uint32_t draw_arrays_min_start;
	uint32_t draw_arrays_max_count;
	/* FIXME: Unknown size, possibly endless, 1000 will do for now */
	GLint gl_draw_arrays_start[1000];
	GLsizei gl_draw_arrays_count[1000];
	bool draw_arrays_prevent_connect;

	GLuint gl_memory_buffer;
	GLuint gl_vertex_array;

	uint32_t regs[0x2000];

	bool waiting_for_nop;
	bool waiting_for_flip;
	bool waiting_for_context_switch;
	bool downloads_pending;
	bool download_dirty_surfaces_pending;
	bool flush_pending;
	bool gl_sync_pending;
	QemuEvent downloads_complete;
	QemuEvent dirty_surfaces_download_complete;
	QemuEvent flush_complete;
	QemuEvent gl_sync_complete;

	uint32_t surface_scale_factor;
	uint8_t* scale_buf;
} PGRAPHState;

typedef struct NV2AState
{
	/*< private >*/
	PCIDevice parent_obj;
	/*< public >*/

	qemu_irq irq;
	bool exiting;

	VGACommonState vga;
	GraphicHwOps hw_ops;
	QEMUTimer* vblank_timer;

	MemoryRegion* vram;
	MemoryRegion vram_pci;
	uint8_t* vram_ptr;
	MemoryRegion ramin;
	uint8_t* ramin_ptr;

	MemoryRegion mmio;
	MemoryRegion block_mmio[NV_NUM_BLOCKS];

	struct
	{
		uint32_t pending_interrupts;
		uint32_t enabled_interrupts;
	} pmc;

	struct
	{
		uint32_t pending_interrupts;
		uint32_t enabled_interrupts;
		uint32_t regs[0x2000];
		QemuMutex lock;
		QemuThread thread;
		QemuCond fifo_cond;
		QemuCond fifo_idle_cond;
		bool fifo_kick;
		bool halt;
	} pfifo;

	struct
	{
		uint32_t regs[0x1000];
	} pvideo;

	struct
	{
		uint32_t pending_interrupts;
		uint32_t enabled_interrupts;
		uint32_t numerator;
		uint32_t denominator;
		uint32_t alarm_time;
	} ptimer;

	struct
	{
		uint32_t regs[0x1000];
	} pfb;

	struct PGRAPHState pgraph;

	struct
	{
		uint32_t pending_interrupts;
		uint32_t enabled_interrupts;
		hwaddr start;
		uint32_t raster;
	} pcrtc;

	struct
	{
		uint32_t core_clock_coeff;
		uint64_t core_clock_freq;
		uint32_t memory_clock_coeff;
		uint32_t video_clock_coeff;
		uint32_t general_control;
		uint32_t fp_vdisplay_end;
		uint32_t fp_vcrtc;
		uint32_t fp_vsync_end;
		uint32_t fp_vvalid_end;
		uint32_t fp_hdisplay_end;
		uint32_t fp_hcrtc;
		uint32_t fp_hvalid_end;
	} pramdac;

	struct
	{
		uint16_t write_mode_address;
		uint8_t palette[256 * 3];
	} puserdac;

} NV2AState;

typedef struct NV2ABlockInfo
{
	const char* name;
	hwaddr offset;
	uint64_t size;
	MemoryRegionOps ops;
} NV2ABlockInfo;

extern GloContext* g_nv2a_context_render;
extern GloContext* g_nv2a_context_display;

void nv2a_update_irq(NV2AState* d);

#ifdef NV2A_DEBUG
void nv2a_reg_log_read(int block, hwaddr addr, uint64_t val);
void nv2a_reg_log_write(int block, hwaddr addr, uint64_t val);
#else
#	define nv2a_reg_log_read(block, addr, val) do {} while (0)
#	define nv2a_reg_log_write(block, addr, val) do {} while (0)
#endif

#define DEFINE_PROTO(n) \
	uint64_t n##_read(void* opaque, hwaddr addr, uint32_t size); \
	void n##_write(void* opaque, hwaddr addr, uint64_t val, uint32_t size);

DEFINE_PROTO(pmc)
DEFINE_PROTO(pbus)
DEFINE_PROTO(pfifo)
DEFINE_PROTO(prma)
DEFINE_PROTO(pvideo)
DEFINE_PROTO(ptimer)
DEFINE_PROTO(pcounter)
DEFINE_PROTO(pvpe)
DEFINE_PROTO(ptv)
DEFINE_PROTO(prmfb)
DEFINE_PROTO(prmvio)
DEFINE_PROTO(pfb)
DEFINE_PROTO(pstraps)
DEFINE_PROTO(pgraph)
DEFINE_PROTO(pcrtc)
DEFINE_PROTO(prmcio)
DEFINE_PROTO(pramdac)
DEFINE_PROTO(prmdio)
// DEFINE_PROTO(pramin)
DEFINE_PROTO(user)
#undef DEFINE_PROTO

DMAObject nv_dma_load(NV2AState* d, hwaddr dma_obj_address);
void* nv_dma_map(NV2AState* d, hwaddr dma_obj_address, hwaddr* len);

void pgraph_init(NV2AState* d);
void pgraph_destroy(PGRAPHState* pg);
void pgraph_context_switch(NV2AState* d, uint32_t channel_id);
int pgraph_method(
	NV2AState* d, uint32_t subchannel, uint32_t method, uint32_t parameter, uint32_t* parameters,
	size_t num_words_available, size_t max_lookahead_words);
void pgraph_gl_sync(NV2AState* d);
void pgraph_process_pending_downloads(NV2AState* d);
void pgraph_download_dirty_surfaces(NV2AState* d);
void pgraph_flush(NV2AState* d);

void* pfifo_thread(void* arg);
void pfifo_kick(NV2AState* d);

#ifdef __cplusplus
}
#endif
