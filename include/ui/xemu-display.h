#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Avoid compiler warning because macro is redefined in SDL_syswm.h. */
#undef WIN32_LEAN_AND_MEAN

#include <SDL.h>
#include <SDL_syswm.h>

#include "ui/kbd-state.h"

// FIXME: Cleanup
typedef struct _SDL2_Console
{
	DisplayChangeListener dcl;
	DisplaySurface* surface;
	DisplayOptions* opts;
	SDL_Texture* texture;
	SDL_Window* real_window;
	SDL_Renderer* real_renderer;
	int idx;
	int last_vm_running; /* per console for caption reasons */
	int x, y, w, h;
	int hidden;
	int opengl;
	int updates;
	int idle_counter;
	int ignore_hotkeys;
	SDL_GLContext winctx;
	QKbdState* kbd;
	bool y0_top;
	bool scanout_mode;
} SDL2_Console;

void sdl2_window_create(SDL2_Console* scon);
void sdl2_window_destroy(SDL2_Console* scon);
void sdl2_window_resize(SDL2_Console* scon);
void sdl2_poll_events(SDL2_Console* scon);

void sdl2_process_key(SDL2_Console* scon, SDL_KeyboardEvent* ev);

void sdl2_2d_update(DisplayChangeListener* dcl, int x, int y, int w, int h);
void sdl2_2d_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface);
void sdl2_2d_refresh(DisplayChangeListener* dcl);
void sdl2_2d_redraw(SDL2_Console* scon);
bool sdl2_2d_check_format(DisplayChangeListener* dcl, pixman_format_code_t format);

void sdl2_gl_update(DisplayChangeListener* dcl, int x, int y, int w, int h);
void sdl2_gl_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface);
void sdl2_gl_refresh(DisplayChangeListener* dcl);
void sdl2_gl_redraw(SDL2_Console* scon);

QEMUGLContext sdl2_gl_create_context(DisplayChangeListener* dcl, QEMUGLParams* params);
void sdl2_gl_destroy_context(DisplayChangeListener* dcl, QEMUGLContext ctx);
int sdl2_gl_make_context_current(DisplayChangeListener* dcl, QEMUGLContext ctx);
QEMUGLContext sdl2_gl_get_current_context(DisplayChangeListener* dcl);

void sdl2_gl_scanout_disable(DisplayChangeListener* dcl);
void sdl2_gl_scanout_texture(
	DisplayChangeListener* dcl, uint32_t backing_id, bool backing_y_0_top, uint32_t backing_width,
	uint32_t backing_height, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void sdl2_gl_scanout_flush(DisplayChangeListener* dcl, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif
