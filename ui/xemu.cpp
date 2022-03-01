/*
 * xemu SDL display driver
 *
 * Copyright (c) 2020-2021 Matt Borgerson
 *
 * Based on sdl2.c, sdl2-gl.c
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/* Ported SDL 1.2 code to 2.0 by Dave Airlie. */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "qemu/rcu.h"
#include "qemu-version.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "qapi/qmp/qdict.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/xemu-display.h"
#include "sysemu/runstate.h"
#include "sysemu/runstate-action.h"
#include "sysemu/sysemu.h"
#include "xemu-hud.h"
#include "xemu-input.h"
#include "xsettings.h"
#include "xemu-shaders.h"
#include "xemu-os-utils.h"
#include "shuriken-version.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize.h"
#include "stb/stb_image_write.h"
#include "stb_sprintf.h"

#include "data/shuriken_64.png.h"

#include <filesystem>
#include <fstream>
#include <map>

#include "ui.h"
#include "hw/xbox/nv2a/nv2a.h"

#ifdef _WIN32
// Provide hint to prefer high-performance graphics for hybrid systems
// https://gpuopen.com/learn/amdpowerxpressrequesthighperformance/
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
// https://docs.nvidia.com/gameworks/content/technologies/desktop/optimus.htm
__declspec(dllexport) DWORD NvOptimusEnablement                  = 1;
#endif

extern "C" void tcg_register_init_ctx(void); // tcg.c

// #define DEBUG_XEMU_C
#ifdef DEBUG_XEMU_C
#	define DPRINTF(...) ui::Log(__VA_ARGS__)
#else
#	define DPRINTF(...)
#endif

extern exiso::GameInfo gameInfo;
extern const char*     sRenderers[];

static int           sdl2_num_outputs;
static SDL2_Console* sdl2_console;
static int           gui_grab; // if true, all keyboard/mouse events are grabbed
static int           gui_fullscreen;
static SDL_Cursor*   sdl_cursor_normal;
static SDL_Cursor*   sdl_cursor_hidden;
static int           absolute_enabled;
static int           guest_cursor;
static int           guest_x;
static int           guest_y;
static SDL_Cursor*   guest_sprite;
static SDL_GLContext m_context;
static QemuSemaphore display_init_sem;

SDL_Window* m_window        = nullptr;
int         want_screenshot = 0;
DecalShader blit;

#define SDL2_REFRESH_INTERVAL_BUSY 16
#define SDL2_MAX_IDLE_COUNT        (2 * GUI_REFRESH_INTERVAL_DEFAULT / SDL2_REFRESH_INTERVAL_BUSY + 1)

static SDL2_Console* get_scon_from_window(uint32_t window_id)
{
	int i;
	for (i = 0; i < sdl2_num_outputs; i++)
	{
		if (sdl2_console[i].real_window == SDL_GetWindowFromID(window_id))
			return &sdl2_console[i];
	}
	return nullptr;
}

void sdl2_window_resize(SDL2_Console* scon)
{
	if (!scon->real_window)
		return;

	SDL_SetWindowSize(scon->real_window, surface_width(scon->surface), surface_height(scon->surface));
}

static void sdl2_redraw(SDL2_Console* scon)
{
	if (scon->opengl)
		sdl2_gl_redraw(scon);
}

static void sdl_update_caption(SDL2_Console* scon)
{
}

static void sdl_hide_cursor(SDL2_Console* scon)
{
	if (scon->opts->has_show_cursor && scon->opts->show_cursor)
		return;

	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetCursor(sdl_cursor_hidden);

	if (!qemu_input_is_absolute())
		SDL_SetRelativeMouseMode(SDL_TRUE);
}

static void sdl_show_cursor(SDL2_Console* scon)
{
	if (scon->opts->has_show_cursor && scon->opts->show_cursor)
		return;

	if (!qemu_input_is_absolute())
		SDL_SetRelativeMouseMode(SDL_FALSE);

	if (guest_cursor && (gui_grab || qemu_input_is_absolute() || absolute_enabled))
		SDL_SetCursor(guest_sprite);
	else
		SDL_SetCursor(sdl_cursor_normal);

	SDL_ShowCursor(SDL_ENABLE);
}

static void sdl_grab_start(SDL2_Console* scon)
{
}

static void sdl_grab_end(SDL2_Console* scon)
{
	SDL_SetWindowGrab(scon->real_window, SDL_FALSE);
	gui_grab = 0;
	sdl_show_cursor(scon);
	sdl_update_caption(scon);
}

static void absolute_mouse_grab(SDL2_Console* scon)
{
	int mouse_x, mouse_y;
	int scr_w, scr_h;
	SDL_GetMouseState(&mouse_x, &mouse_y);
	SDL_GetWindowSize(scon->real_window, &scr_w, &scr_h);
	if (mouse_x > 0 && mouse_x < scr_w - 1 && mouse_y > 0 && mouse_y < scr_h - 1)
		sdl_grab_start(scon);
}

static void sdl_mouse_mode_change(Notifier* notify, void* data)
{
	if (qemu_input_is_absolute())
	{
		if (!absolute_enabled)
		{
			absolute_enabled = 1;
			SDL_SetRelativeMouseMode(SDL_FALSE);
			absolute_mouse_grab(&sdl2_console[0]);
		}
	}
	else if (absolute_enabled)
	{
		if (!gui_fullscreen)
			sdl_grab_end(&sdl2_console[0]);
		absolute_enabled = 0;
	}
}

static void sdl_send_mouse_event(SDL2_Console* scon, int dx, int dy, int x, int y, uint32_t state)
{
	static uint32_t bmap[INPUT_BUTTON__MAX] = {
		[INPUT_BUTTON_LEFT]   = SDL_BUTTON(SDL_BUTTON_LEFT),
		[INPUT_BUTTON_MIDDLE] = SDL_BUTTON(SDL_BUTTON_MIDDLE),
		[INPUT_BUTTON_RIGHT]  = SDL_BUTTON(SDL_BUTTON_RIGHT),
	};
	static uint32_t prev_state;

	if (prev_state != state)
	{
		qemu_input_update_buttons(scon->dcl.con, bmap, prev_state, state);
		prev_state = state;
	}

	if (qemu_input_is_absolute())
	{
		qemu_input_queue_abs(scon->dcl.con, INPUT_AXIS_X, x, 0, surface_width(scon->surface));
		qemu_input_queue_abs(scon->dcl.con, INPUT_AXIS_Y, y, 0, surface_height(scon->surface));
	}
	else
	{
		if (guest_cursor)
		{
			x -= guest_x;
			y -= guest_y;
			guest_x += x;
			guest_y += y;
			dx = x;
			dy = y;
		}
		qemu_input_queue_rel(scon->dcl.con, INPUT_AXIS_X, dx);
		qemu_input_queue_rel(scon->dcl.con, INPUT_AXIS_Y, dy);
	}
	qemu_input_event_sync();
}

static void set_full_screen(SDL2_Console* scon, bool set)
{
	static int gui_saved_grab;
	gui_fullscreen = set;

	if (gui_fullscreen)
	{
		SDL_SetWindowFullscreen(scon->real_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		gui_saved_grab = gui_grab;
		sdl_grab_start(scon);
	}
	else
	{
		if (!gui_saved_grab)
			sdl_grab_end(scon);
		SDL_SetWindowFullscreen(scon->real_window, 0);
	}
}

static void toggle_full_screen(SDL2_Console* scon) { set_full_screen(scon, !gui_fullscreen); }
int         xemu_is_fullscreen() { return gui_fullscreen; }
void        xemu_toggle_fullscreen() { toggle_full_screen(&sdl2_console[0]); }

static int get_mod_state()
{
	static int gui_grab_code = KMOD_LALT; // | KMOD_LCTRL;
	SDL_Keymod mod           = SDL_GetModState();

	if (alt_grab)
		return (mod & gui_grab_code) == gui_grab_code;
	else if (ctrl_grab)
		return (mod & KMOD_RCTRL) == KMOD_RCTRL;
	else
		return (mod & gui_grab_code) == gui_grab_code;
}

static void handle_keydown(SDL_Event* ev)
{
	// int win;
	SDL2_Console* scon                     = get_scon_from_window(ev->key.windowID);
	int           gui_key_modifier_pressed = get_mod_state();
	int           gui_keysym               = 0;

	if (!scon->ignore_hotkeys && gui_key_modifier_pressed && !ev->key.repeat)
	{
		switch (ev->key.keysym.scancode)
		{
		// case SDL_SCANCODE_2:
		// case SDL_SCANCODE_3:
		// case SDL_SCANCODE_4:
		// case SDL_SCANCODE_5:
		// case SDL_SCANCODE_6:
		// case SDL_SCANCODE_7:
		// case SDL_SCANCODE_8:
		// case SDL_SCANCODE_9:
		//     if (gui_grab) {
		//         sdl_grab_end(scon);
		//     }

		// win = ev->key.keysym.scancode - SDL_SCANCODE_1;
		// if (win < sdl2_num_outputs) {
		//     sdl2_console[win].hidden = !sdl2_console[win].hidden;
		//     if (sdl2_console[win].real_window) {
		//         if (sdl2_console[win].hidden) {
		//             SDL_HideWindow(sdl2_console[win].real_window);
		//         } else {
		//             SDL_ShowWindow(sdl2_console[win].real_window);
		//         }
		//     }
		//     gui_keysym = 1;
		// }
		// break;
		case SDL_SCANCODE_RETURN:
			toggle_full_screen(scon);
			gui_keysym = 1;
			break;
		// case SDL_SCANCODE_G:
		//     gui_keysym = 1;
		//     if (!gui_grab) {
		//         sdl_grab_start(scon);
		//     } else if (!gui_fullscreen) {
		//         sdl_grab_end(scon);
		//     }
		//     break;
		// case SDL_SCANCODE_U:
		//     sdl2_window_resize(scon);
		//     gui_keysym = 1;
		//     break;
		default:
			break;
		}
	}
	if (!gui_keysym)
		sdl2_process_key(scon, &ev->key);
}

static void handle_keyup(SDL_Event* ev)
{
	SDL2_Console* scon = get_scon_from_window(ev->key.windowID);

	scon->ignore_hotkeys = false;
	sdl2_process_key(scon, &ev->key);
}

static void handle_textinput(SDL_Event* ev)
{
	SDL2_Console* scon = get_scon_from_window(ev->text.windowID);
	QemuConsole*  con  = scon ? scon->dcl.con : nullptr;

	if (qemu_console_is_graphic(con))
		return;
	kbd_put_string_console(con, ev->text.text, strlen(ev->text.text));
}

static void handle_mousemotion(SDL_Event* ev)
{
	int           max_x, max_y;
	SDL2_Console* scon = get_scon_from_window(ev->motion.windowID);

	if (!scon || !qemu_console_is_graphic(scon->dcl.con))
		return;

	if (qemu_input_is_absolute() || absolute_enabled)
	{
		int scr_w, scr_h;
		SDL_GetWindowSize(scon->real_window, &scr_w, &scr_h);
		max_x = scr_w - 1;
		max_y = scr_h - 1;

		if (gui_grab && !gui_fullscreen && (ev->motion.x == 0 || ev->motion.y == 0 || ev->motion.x == max_x || ev->motion.y == max_y))
			sdl_grab_end(scon);

		if (!gui_grab && (ev->motion.x > 0 && ev->motion.x < max_x && ev->motion.y > 0 && ev->motion.y < max_y))
			sdl_grab_start(scon);
	}

	if (gui_grab || qemu_input_is_absolute() || absolute_enabled)
		sdl_send_mouse_event(scon, ev->motion.xrel, ev->motion.yrel, ev->motion.x, ev->motion.y, ev->motion.state);
}

static void handle_mousebutton(SDL_Event* ev)
{
	uint32_t              buttonstate = SDL_GetMouseState(nullptr, nullptr);
	SDL_MouseButtonEvent* bev;
	SDL2_Console*         scon = get_scon_from_window(ev->button.windowID);

	if (!scon || !qemu_console_is_graphic(scon->dcl.con))
		return;

	bev = &ev->button;
	if (!gui_grab && !qemu_input_is_absolute())
	{
		if (ev->type == SDL_MOUSEBUTTONUP && bev->button == SDL_BUTTON_LEFT)
		{
			// start grabbing all events
			sdl_grab_start(scon);
		}
	}
	else
	{
		if (ev->type == SDL_MOUSEBUTTONDOWN)
			buttonstate |= SDL_BUTTON(bev->button);
		else
			buttonstate &= ~SDL_BUTTON(bev->button);
		sdl_send_mouse_event(scon, 0, 0, bev->x, bev->y, buttonstate);
	}
}

static void handle_mousewheel(SDL_Event* ev)
{
	SDL2_Console*        scon = get_scon_from_window(ev->wheel.windowID);
	SDL_MouseWheelEvent* wev  = &ev->wheel;
	InputButton          btn;

	if (!scon || !qemu_console_is_graphic(scon->dcl.con))
		return;

	if (wev->y > 0) btn = INPUT_BUTTON_WHEEL_UP;
	else if (wev->y < 0) btn = INPUT_BUTTON_WHEEL_DOWN;
	else return;

	qemu_input_queue_btn(scon->dcl.con, btn, true);
	qemu_input_event_sync();
	qemu_input_queue_btn(scon->dcl.con, btn, false);
	qemu_input_event_sync();
}

static void handle_windowevent(SDL_Event* ev)
{
	SDL2_Console* scon        = get_scon_from_window(ev->window.windowID);
	bool          allow_close = true;

	if (!scon)
		return;

	switch (ev->window.event)
	{
	case SDL_WINDOWEVENT_RESIZED:
	{
		QemuUIInfo info;
		memset(&info, 0, sizeof(info));
		info.width  = ev->window.data1;
		info.height = ev->window.data2;
		dpy_set_ui_info(scon->dcl.con, &info);
		sdl2_redraw(scon);
		break;
	}
	case SDL_WINDOWEVENT_EXPOSED:
		sdl2_redraw(scon);
		break;
	case SDL_WINDOWEVENT_FOCUS_GAINED:
	case SDL_WINDOWEVENT_ENTER:
		if (!gui_grab && (qemu_input_is_absolute() || absolute_enabled))
			absolute_mouse_grab(scon);
		/* If a new console window opened using a hotkey receives the
		 * focus, SDL sends another KEYDOWN event to the new window,
		 * closing the console window immediately after.
		 *
		 * Work around this by ignoring further hotkey events until a
		 * key is released.
		 */
		scon->ignore_hotkeys = get_mod_state();
		break;
	case SDL_WINDOWEVENT_FOCUS_LOST:
		if (gui_grab && !gui_fullscreen)
			sdl_grab_end(scon);
		break;
	case SDL_WINDOWEVENT_RESTORED: break;
	case SDL_WINDOWEVENT_MINIMIZED: break;
	case SDL_WINDOWEVENT_CLOSE:
		if (qemu_console_is_graphic(scon->dcl.con))
		{
			if (scon->opts->has_window_close && !scon->opts->window_close)
				allow_close = false;
			if (allow_close)
			{
				shutdown_action = SHUTDOWN_ACTION_POWEROFF;
				qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
			}
		}
		else
		{
			SDL_HideWindow(scon->real_window);
			scon->hidden = true;
		}
		break;
	case SDL_WINDOWEVENT_SHOWN: scon->hidden = false; break;
	case SDL_WINDOWEVENT_HIDDEN: scon->hidden = true; break;
	}
}

void sdl2_poll_events(SDL2_Console* scon)
{
	SDL_Event ev1, *ev = &ev1;
	bool      allow_close = true;

	if (scon->last_vm_running != runstate_is_running())
	{
		scon->last_vm_running = runstate_is_running();
		sdl_update_caption(scon);
	}

	int kbd = 0, mouse = 0;
	xemu_hud_should_capture_kbd_mouse(&kbd, &mouse);

	while (SDL_PollEvent(ev))
	{
		xemu_input_process_sdl_events(ev);
		ui::ProcessSDL(ev);

		switch (ev->type)
		{
		case SDL_KEYDOWN:
			if (kbd)
				break;
			handle_keydown(ev);
			break;
		case SDL_KEYUP:
			if (kbd)
				break;
			handle_keyup(ev);
			break;
		case SDL_TEXTINPUT:
			if (kbd)
				break;
			handle_textinput(ev);
			break;
		case SDL_QUIT:
			ui::LoadedGame("");
			if (scon->opts->has_window_close && !scon->opts->window_close)
				allow_close = false;
			if (allow_close)
			{
				shutdown_action = SHUTDOWN_ACTION_POWEROFF;
				qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
			}
			break;
		case SDL_MOUSEMOTION:
			if (mouse)
				break;
			handle_mousemotion(ev);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (mouse)
				break;
			handle_mousebutton(ev);
			break;
		case SDL_MOUSEWHEEL:
			if (mouse)
				break;
			handle_mousewheel(ev);
			break;
		case SDL_WINDOWEVENT:
			handle_windowevent(ev);
			break;
		default:
			break;
		}
	}

	xemu_input_update_controllers();

	scon->idle_counter        = 0;
	scon->dcl.update_interval = 16; // Ignored
}

static void sdl_mouse_warp(DisplayChangeListener* dcl, int x, int y, int on)
{
	SDL2_Console* scon = container_of(dcl, SDL2_Console, dcl);

	if (!qemu_console_is_graphic(scon->dcl.con))
		return;

	if (on)
	{
		if (!guest_cursor)
			sdl_show_cursor(scon);

		if (gui_grab || qemu_input_is_absolute() || absolute_enabled)
		{
			SDL_SetCursor(guest_sprite);
			if (!qemu_input_is_absolute() && !absolute_enabled)
				SDL_WarpMouseInWindow(scon->real_window, x, y);
		}
	}
	else if (gui_grab)
		sdl_hide_cursor(scon);

	guest_cursor = on;
	guest_x = x, guest_y = y;
}

static void sdl_mouse_define(DisplayChangeListener* dcl, QEMUCursor* c)
{
	static SDL_Surface* guest_sprite_surface;

	if (guest_sprite)
		SDL_FreeCursor(guest_sprite);

	if (guest_sprite_surface)
		SDL_FreeSurface(guest_sprite_surface);

	guest_sprite_surface = SDL_CreateRGBSurfaceFrom(c->data, c->width, c->height, 32, c->width * 4, 0xff0000, 0x00ff00, 0xff, 0xff000000);
	if (!guest_sprite_surface)
	{
		ui::LogError("Failed to make rgb surface from %p", c);
		return;
	}
	guest_sprite = SDL_CreateColorCursor(guest_sprite_surface, c->hot_x, c->hot_y);
	if (!guest_sprite)
	{
		ui::LogError("Failed to make color cursor from %p", c);
		return;
	}
	if (guest_cursor && (gui_grab || qemu_input_is_absolute() || absolute_enabled))
		SDL_SetCursor(guest_sprite);
}

static bool xb_console_gl_check_format(DisplayChangeListener* dcl, pixman_format_code_t format)
{
	switch (format)
	{
	case PIXMAN_BE_b8g8r8x8:
	case PIXMAN_BE_b8g8r8a8:
	case PIXMAN_r5g6b5:
		return true;
	default:
		return false;
	}
}

static const DisplayChangeListenerOps dcl_gl_ops = {
	.dpy_name                = "sdl2-gl",
	.dpy_gfx_update          = sdl2_gl_update,
	.dpy_gfx_switch          = sdl2_gl_switch,
	.dpy_gfx_check_format    = xb_console_gl_check_format,
	// .dpy_refresh             = sdl2_gl_refresh,
	.dpy_mouse_set           = sdl_mouse_warp,
	.dpy_cursor_define       = sdl_mouse_define,
	.dpy_gl_ctx_create       = sdl2_gl_create_context,
	.dpy_gl_ctx_destroy      = sdl2_gl_destroy_context,
	.dpy_gl_ctx_make_current = sdl2_gl_make_context_current,
	.dpy_gl_scanout_disable  = sdl2_gl_scanout_disable,
	.dpy_gl_scanout_texture  = sdl2_gl_scanout_texture,
	.dpy_gl_update           = sdl2_gl_scanout_flush,
};

static void sdl2_display_very_early_init(DisplayOptions* o)
{
#ifdef __linux__
	/* on Linux, SDL may use fbcon|directfb|svgalib when run without
	 * accessible $DISPLAY to open X11 window.  This is often the case
	 * when qemu is run using sudo.  But in this case, and when actually
	 * run in X11 environment, SDL fights with X11 for the video card,
	 * making current display unavailable, often until reboot.
	 * So make x11 the default SDL video driver if this variable is unset.
	 * This is a bit hackish but saves us from bigger problem.
	 * Maybe it's a good idea to fix this in SDL instead.
	 */
	setenv("SDL_VIDEODRIVER", "x11", 0);
#endif

	if (SDL_Init(SDL_INIT_VIDEO))
	{
		ui::LogError("Failed to initialize SDL video subsystem: %s", SDL_GetError());
		exit(1);
	}

#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR /* only available since SDL 2.0.8 */
	SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
	SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

	// Initialize rendering context
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	// Create main window
	{
		int      winWidth  = xsettings.resize_on_boot ? xsettings.resize_width : 1024;
		int      winHeight = xsettings.resize_on_boot ? xsettings.resize_height : 768;
		uint32_t flags     = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
		if (xsettings.start_fullscreen)
			flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

		m_window = SDL_CreateWindow("Shuriken", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winWidth, winHeight, flags);
		if (!m_window)
		{
			ui::LogError("Failed to create main window");
			SDL_Quit();
			exit(1);
		}
	}

	m_context = SDL_GL_CreateContext(m_window);
	if (m_context && epoxy_gl_version() < 40)
	{
		SDL_GL_MakeCurrent(nullptr, nullptr);
		SDL_GL_DeleteContext(m_context);
		m_context = nullptr;
	}

	if (!m_context)
	{
		SDL_ShowSimpleMessageBox(
		    SDL_MESSAGEBOX_ERROR, "Unable to create OpenGL context",
		    "Unable to create OpenGL context. This usually means the\r\n"
		    "graphics device on this system does not support OpenGL 4.0.\r\n"
		    "\r\n"
		    "xemu cannot continue and will now exit.",
		    m_window);
		SDL_DestroyWindow(m_window);
		SDL_Quit();
		exit(1);
	}

	int width, height, channels = 0;
	stbi_set_flip_vertically_on_load(0);
	unsigned char* icon_data = stbi_load_from_memory(shuriken_64_data, shuriken_64_size, &width, &height, &channels, 4);
	if (icon_data)
	{
		SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(icon_data, width, height, 32, width * 4, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
		if (icon)
			SDL_SetWindowIcon(m_window, icon);
		// Note: Retaining the memory allocated by stbi_load. It's used in place by the SDL surface.
	}

	ui::LogInfo("OS_Version: %s", xemu_get_os_info());
	ui::LogInfo("GL_VENDOR: %s", glGetString(GL_VENDOR));
	ui::LogInfo("GL_RENDERER: %s", glGetString(GL_RENDERER));
	ui::LogInfo("GL_VERSION: %s", glGetString(GL_VERSION));
	ui::LogInfo("GL_SHADING_LANGUAGE_VERSION: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

	// Initialize offscreen rendering context now
	nv2a_gl_context_init();
	SDL_GL_MakeCurrent(nullptr, nullptr);

	// FIXME: atexit(sdl_cleanup);
}

static void sdl2_display_early_init(DisplayOptions* o)
{
	assert(o->type == DISPLAY_TYPE_SHURIKEN);
	display_opengl = 1;

	SDL_GL_MakeCurrent(m_window, m_context);
	SDL_GL_SetSwapInterval(0);
	xemu_hud_init(m_window, m_context);

	create_decal_shader(blit, SHADER_TYPE_BLIT_GAMMA);
}

static void sdl2_display_init(DisplayState* ds, DisplayOptions* o)
{
	uint8_t       data = 0;
	int           i;
	SDL_SysWMinfo info;

	assert(o->type == DISPLAY_TYPE_SHURIKEN);
	SDL_GL_MakeCurrent(m_window, m_context);

	memset(&info, 0, sizeof(info));
	SDL_VERSION(&info.version);

	gui_fullscreen = (o->has_full_screen && o->full_screen) || xsettings.start_fullscreen;

#if 1
	// Explicitly set number of outputs to 1 for a single screen.
	// We don't need multiple for now, but maybe in the future debug stuff can go on a second screen.
	sdl2_num_outputs = 1;
#else
	for (i = 0;; i++)
	{
		QemuConsole* con = qemu_console_lookup_by_index(i);
		if (!con)
			break;
	}
	sdl2_num_outputs = i;
	if (sdl2_num_outputs == 0)
		return;
#endif

	sdl2_console = g_new0(SDL2_Console, sdl2_num_outputs);
	for (i = 0; i < sdl2_num_outputs; i++)
	{
		QemuConsole* con = qemu_console_lookup_by_index(i);
		assert(con != nullptr);
		if (!qemu_console_is_graphic(con) && qemu_console_get_index(con) != 0)
			sdl2_console[i].hidden = true;

		sdl2_console[i].idx     = i;
		sdl2_console[i].opts    = o;
		sdl2_console[i].opengl  = 1;
		sdl2_console[i].dcl.ops = &dcl_gl_ops;
		sdl2_console[i].dcl.con = con;
		sdl2_console[i].kbd     = qkbd_state_init(con);
		register_displaychangelistener(&sdl2_console[i].dcl);

#if defined(SDL_VIDEO_DRIVER_WINDOWS) || defined(SDL_VIDEO_DRIVER_X11)
		if (SDL_GetWindowWMInfo(sdl2_console[i].real_window, &info))
		{
#	if defined(SDL_VIDEO_DRIVER_WINDOWS)
			qemu_console_set_window_id(con, (uintptr_t)info.info.win.window);
#	elif defined(SDL_VIDEO_DRIVER_X11)
			qemu_console_set_window_id(con, info.info.x11.window);
#	endif
		}
#endif
	}

	sdl2_console[0].real_window = m_window;
	sdl2_console[0].winctx      = m_context;

	static Notifier mouse_mode_notifier;
	mouse_mode_notifier.notify = sdl_mouse_mode_change;
	qemu_add_mouse_mode_change_notifier(&mouse_mode_notifier);

	sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
	sdl_cursor_normal = SDL_GetCursor();

	/* Tell main thread to go ahead and create the app and enter the run loop */
	SDL_GL_MakeCurrent(nullptr, nullptr);
	qemu_sem_post(&display_init_sem);
}

static QemuDisplay qemu_display_sdl2 = {
	.type       = DISPLAY_TYPE_SHURIKEN,
	.early_init = sdl2_display_early_init,
	.init       = sdl2_display_init,
};

static void register_sdl1()
{
	qemu_display_register(&qemu_display_sdl2);
}

type_init(register_sdl1);

void xb_surface_gl_create_texture(DisplaySurface* surface)
{
	assert(QEMU_IS_ALIGNED(surface_stride(surface), surface_bytes_per_pixel(surface)));

	switch (surface->format)
	{
	case PIXMAN_BE_b8g8r8x8:
	case PIXMAN_BE_b8g8r8a8:
		surface->glformat = GL_BGRA_EXT;
		surface->gltype   = GL_UNSIGNED_BYTE;
		break;
	case PIXMAN_BE_x8r8g8b8:
	case PIXMAN_BE_a8r8g8b8:
		surface->glformat = GL_RGBA;
		surface->gltype   = GL_UNSIGNED_BYTE;
		break;
	case PIXMAN_r5g6b5:
		surface->glformat = GL_RGB;
		surface->gltype   = GL_UNSIGNED_SHORT_5_6_5;
		break;
	default:
		g_assert_not_reached();
	}

	if (!surface->texture)
		glGenTextures(1, &surface->texture);

	glBindTexture(GL_TEXTURE_2D, surface->texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, surface_stride(surface) / surface_bytes_per_pixel(surface));
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, surface_width(surface), surface_height(surface), 0, surface->glformat, surface->gltype, surface_data(surface));
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	// nearest not working?
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, xsettings.scale_nearest ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, xsettings.scale_nearest ? GL_NEAREST : GL_LINEAR);
}

void xb_surface_gl_destroy_texture(DisplaySurface* surface)
{
	if (!surface || !surface->texture)
		return;

	glDeleteTextures(1, &surface->texture);
	surface->texture = 0;
}

void sdl2_gl_update(DisplayChangeListener* dcl, int x, int y, int w, int h)
{
	SDL2_Console* scon = container_of(dcl, SDL2_Console, dcl);
	assert(scon->opengl);

	SDL_GL_MakeCurrent(scon->real_window, scon->winctx);
}

void sdl2_gl_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface)
{
	SDL2_Console* scon = container_of(dcl, SDL2_Console, dcl);
	assert(scon->opengl);
	SDL_GL_MakeCurrent(scon->real_window, scon->winctx);
	xb_surface_gl_destroy_texture(scon->surface);
	scon->surface = new_surface;
	if (!new_surface)
		return;

	if (!scon->real_window)
	{
		scon->real_window = m_window;
		scon->winctx      = m_context;
		SDL_GL_MakeCurrent(scon->real_window, scon->winctx);
	}
}

/**
 * Save screenshot + create Icon
 * - want_screenshot: 1 = screenshot      , 2 = icon
 *                    4 = force screenshot, 8 = force icon
 */
void SaveScreenshot(int texId, int width, int height)
{
	static uint8_t              iconPixels[320 * 176 * 3];
	static std::vector<uint8_t> buffer;

	if ((int)buffer.size() < width * height * 3)
		buffer.reserve(width * height * 3);

	glGetTextureImage(texId, 0, GL_RGB, GL_UNSIGNED_BYTE, width * height * 3, buffer.data());

	for (int i = 1; i <= 2; ++i)
	{
		if (!(want_screenshot & i))
			continue;

		auto folder = xsettingsFolder() / ((i & 2) ? "icons" : "screenshots");
		if (!std::filesystem::is_directory(folder))
			std::filesystem::create_directory(folder);

		auto filename = folder / (gameInfo.uid + ".png");
		if (!(want_screenshot & (1 << (i + 1))) && std::filesystem::exists(filename))
			continue;

		stbi_flip_vertically_on_write(1);
		if (i & 2)
		{
			stbir_resize_uint8(buffer.data(), width, height, width * 3, iconPixels, 320, 176, 320 * 3, 3);
			stbi_write_png(filename.string().c_str(), 320, 176, 3, iconPixels, 320 * 3);
		}
		else
			stbi_write_png(filename.string().c_str(), width, height, 3, buffer.data(), width * 3);
	}

	ui::CheckIcon(gameInfo.uid);
	want_screenshot = 0;
}

/**
 * Calculate the scale to apply on the framebuffer texture to render on the window
 * - supports stretch + vertical integer scaling
 */
static void calculateScale(double scale[2], int num, int den, int ww, int wh, int tw, int th)
{
	if (xsettings.stretch)
	{
		bool isWider = (ww * den >= wh * num);
		scale[0]     = isWider ? (wh * num * 1.0) / (ww * den * 1.0) : 1.0;
		scale[1]     = isWider ? 1.0 : (ww * den * 1.0) / (wh * num * 1.0);

		if (xsettings.integer_scaling && wh != th)
		{
			double vertical  = (wh > th) ? floor(wh * 1.0 / th) : ceil(th * 1.0 / wh);
			double newScale1 = (wh > th) ? vertical * th / wh : (th * 1.0 / wh) / vertical;
			scale[0] *= newScale1 / scale[1];
			scale[1] = newScale1;
		}
	}
	else
	{
		scale[0] = (th * num * 1.0) / (ww * den * 1.0);
		scale[1] = (th * 1.0 / wh);
	}
}

// Note: only supports millisecond resolution on Windows
static void sleep_ns(int64_t ns)
{
#ifndef _WIN32
	struct timespec sleep_delay, rem_delay;
	sleep_delay.tv_sec  = ns / 1000000000LL;
	sleep_delay.tv_nsec = ns % 1000000000LL;
	nanosleep(&sleep_delay, &rem_delay);
#else
	Sleep(ns / SCALE_MS);
#endif
}

void WaitForVSync()
{
	// TODO: when going to sub 60Hz, we should just pause the emulator a bit, so the GUI can run at full speed ...
	if (xsettings.vblank_frequency < 1)
		return;

	// throttle to make sure swaps happen at 60Hz
	static int64_t last_update = 0;
	int64_t        deadline    = last_update + 1e9 / xsettings.vblank_frequency;

	int64_t sleep_acc = 0;
	int64_t spin_acc  = 0;

#ifndef _WIN32
	const int64_t sleep_threshold = 2000000;
#else
	const int64_t sleep_threshold = 250000;
#endif

	while (true)
	{
		int64_t now            = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
		int64_t time_remaining = deadline - now;
		if (now < deadline)
		{
			if (time_remaining > sleep_threshold)
			{
				// try to sleep until the until reaching the sleep threshold.
				sleep_ns(time_remaining - sleep_threshold);
				sleep_acc += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - now;
			}
			else
			{
				// Simply spin to avoid extra delays incurred with swapping to another process
				// and back in the event of being within threshold to desired event.
				++spin_acc;
			}
		}
		else
		{
			DPRINTF("zzZz %g %ld\n", (double)sleep_acc / 1000000.0, spin_acc);
			last_update = now;
			break;
		}
	}
}

void sdl2_gl_refresh(DisplayChangeListener* dcl)
{
	SDL2_Console* scon = container_of(dcl, SDL2_Console, dcl);
	assert(scon->opengl);
	bool flip_required = false;

	SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

	/* XXX: Note that this bypasses the usual VGA path in order to quickly
	 * get the surface. This is simple and fast, at the cost of accuracy.
	 * Ideally, this should go through the VGA code and opportunistically pull
	 * the surface like this, but handle the VGA logic as well. For now, just
	 * use this fast path to handle the common case.
	 *
	 * In the event the surface is not found in the surface cache, e.g. when
	 * the guest code isn't using HW accelerated rendering, but just blitting
	 * to the framebuffer, fall back to the VGA path.
	 */
	GLuint tex = nv2a_get_framebuffer_surface();
	if (tex == 0)
	{
		xb_surface_gl_create_texture(scon->surface);
		scon->updates++;
		tex           = scon->surface->texture;
		flip_required = true;
	}

	// FIXME: Finer locking. Event handlers in segments of the code expect to be running on the main thread with the BQL.
	// For now, acquire the lock and perform rendering, but release before swap to avoid possible lengthy blocking (for vsync).
	qemu_mutex_lock_main_loop();
	qemu_mutex_lock_iothread();
	sdl2_poll_events(scon);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);

	// Get texture dimensions
	int tw, th;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);

	if (want_screenshot)
		SaveScreenshot(tex, tw, th);

	// Get window dimensions
	int ww, wh;
	SDL_GL_GetDrawableSize(scon->real_window, &ww, &wh);

	// Calculate scaling factors
	double scale[2];

	switch (xsettings.aspect_ratio)
	{
	case ASPECT_RATIO_169: calculateScale(scale, 16, 9, ww, wh, tw, th); break;
	case ASPECT_RATIO_43: calculateScale(scale, 4, 3, ww, wh, tw, th); break;
	case ASPECT_RATIO_NATIVE: calculateScale(scale, tw, th, ww, wh, tw, th); break;
	case ASPECT_RATIO_WINDOW: calculateScale(scale, ww, wh, ww, wh, tw, th); break;
	default:
		scale[0] = 1.0;
		scale[1] = 1.0;
		break;
	}

	// update title
	{
		static int         frame = 0;
		static str256      title;
		static std::string uid;
		auto&              io = ImGui::GetIO();
		stbsp_sprintf(title, "FPS: %.2f | %s | %d x %d | %s | %s", io.Framerate, sRenderers[xsettings.renderer], tw, th, shuriken_version, gameInfo.buffer);
		SDL_SetWindowTitle(m_window, title);

		// new game
		if (uid != gameInfo.uid)
		{
			uid   = gameInfo.uid;
			frame = 0;
		}
		// game is loaded at 600 frames + update every 18 minutes (at 60Hz)
		else if (frame == 600 || !(frame & 65535))
			ui::LoadedGame(uid);

		++frame;
	}

	// Render framebuffer and GUI
	auto& s = blit;
	s.flip  = flip_required;
	glViewport(0, 0, ww, wh);
	glUseProgram(s.prog);
	glBindVertexArray(s.vao);
	glUniform1i(s.FlipY_loc, s.flip);
	glUniform4f(s.ScaleOffset_loc, scale[0], scale[1], 0, 0);
	glUniform4f(s.TexScaleOffset_loc, 1.0, 1.0, 0, 0);
	glUniform1i(s.tex_loc, 0);

	const uint8_t* palette = nv2a_get_dac_palette();
	for (int i = 0; i < 256; i++)
	{
		uint32_t e = (palette[i * 3 + 2] << 16) | (palette[i * 3 + 1] << 8) | palette[i * 3];
		glUniform1ui(s.palette_loc[i], e);
	}

	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_INT, nullptr);

	if (!want_screenshot)
		xemu_hud_render();

	// Release BQL before swapping (which may sleep if swap interval is not immediate)
	qemu_mutex_unlock_iothread();
	qemu_mutex_unlock_main_loop();

	glFinish();
	SDL_GL_SwapWindow(scon->real_window);

	// VGA update (see note above) + vblank
	qemu_mutex_lock_main_loop();
	qemu_mutex_lock_iothread();
	graphic_hw_update(scon->dcl.con);
	if (scon->updates && scon->surface)
		scon->updates = 0;

	qemu_mutex_unlock_iothread();
	qemu_mutex_unlock_main_loop();

	WaitForVSync();
}

void sdl2_gl_redraw(SDL2_Console* scon)
{
	assert(scon->opengl);

	if (scon->scanout_mode)
	{
		assert(0);
		/* sdl2_gl_scanout_flush actually only care about
		 * the first argument. */
		// return sdl2_gl_scanout_flush(&scon->dcl, 0, 0, 0, 0);
	}
	// if (scon->surface)
	// xemu_sdl2_gl_render_surface(scon);
}

QEMUGLContext sdl2_gl_create_context(DisplayChangeListener* dcl, QEMUGLParams* params)
{
	SDL2_Console* scon = container_of(dcl, SDL2_Console, dcl);
	SDL_GLContext ctx;

	assert(0);

	assert(scon->opengl);

	SDL_GL_MakeCurrent(scon->real_window, scon->winctx);

	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
	if (scon->opts->gl == DISPLAYGL_MODE_ON || scon->opts->gl == DISPLAYGL_MODE_CORE)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	else if (scon->opts->gl == DISPLAYGL_MODE_ES)
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, params->major_ver);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, params->minor_ver);

	ctx = SDL_GL_CreateContext(scon->real_window);

	/* If SDL fail to create a GL context and we use the "on" flag,
	 * then try to fallback to GLES.
	 */
	if (!ctx && scon->opts->gl == DISPLAYGL_MODE_ON)
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		ctx = SDL_GL_CreateContext(scon->real_window);
	}
	return (QEMUGLContext)ctx;
}

void sdl2_gl_destroy_context(DisplayChangeListener* dcl, QEMUGLContext ctx)
{
	SDL_GLContext sdlctx = (SDL_GLContext)ctx;
	SDL_GL_DeleteContext(sdlctx);
}

int sdl2_gl_make_context_current(DisplayChangeListener* dcl, QEMUGLContext ctx)
{
	SDL2_Console* scon   = container_of(dcl, SDL2_Console, dcl);
	SDL_GLContext sdlctx = (SDL_GLContext)ctx;

	assert(scon->opengl);
	return SDL_GL_MakeCurrent(scon->real_window, sdlctx);
}

QEMUGLContext sdl2_gl_get_current_context(DisplayChangeListener* dcl)
{
	SDL_GLContext sdlctx = SDL_GL_GetCurrentContext();
	return (QEMUGLContext)sdlctx;
}

void sdl2_gl_scanout_disable(DisplayChangeListener* dcl)
{
	assert(0);
}

void sdl2_gl_scanout_texture(DisplayChangeListener* dcl, uint32_t backing_id, bool backing_y_0_top, uint32_t backing_width, uint32_t backing_height, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	assert(0);
}

void sdl2_gl_scanout_flush(DisplayChangeListener* dcl, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	assert(0);
}

// sdl2-input.c
void sdl2_process_key(SDL2_Console* scon, SDL_KeyboardEvent* ev)
{
	int          qcode;
	QemuConsole* con = scon->dcl.con;

	if (ev->keysym.scancode >= qemu_input_map_usb_to_qcode_len)
		return;

	qcode = qemu_input_map_usb_to_qcode[ev->keysym.scancode];
	qkbd_state_key_event(scon->kbd, (QKeyCode)qcode, ev->type == SDL_KEYDOWN);

	if (!qemu_console_is_graphic(con))
	{
		bool ctrl = qkbd_state_modifier_get(scon->kbd, QKBD_MOD_CTRL);
		if (ev->type == SDL_KEYDOWN)
		{
			switch (qcode)
			{
			case Q_KEY_CODE_RET:
				kbd_put_keysym_console(con, '\n');
				break;
			default:
				kbd_put_qcode_console(con, qcode, ctrl);
				break;
			}
		}
	}
}

const std::string defaultIni =
R"([Window][DockSpaceViewport_11111111]
Pos=0,22
Size=1280,778
Collapsed=0

[Window][Debug##Default]
Pos=60,60
Size=400,400
Collapsed=0

[Window][Controls]
Pos=0,22
Size=1280,79
Collapsed=0
DockId=0x0000000B,0

[Window][Game List]
Pos=0,103
Size=1280,458
Collapsed=0
DockId=0x00000002,0

[Window][Log]
Pos=0,563
Size=1280,237
Collapsed=0
DockId=0x00000003,0

[Window][Settings]
Pos=259,146
Size=900,600
Collapsed=0

[Window][Error]
Pos=404,350
Size=471,100
Collapsed=0

[Window][Update]
Pos=60,60
Size=566,108
Collapsed=0

[Window][First Boot]
Pos=440,250
Size=400,300
Collapsed=0

[Table][0xB572EADC,9]
RefScale=20.736
Column 0  Width=126
Column 1  Width=208
Column 2  Width=78
Column 3  Width=74
Column 4  Width=116
Column 5  Width=116
Column 6  Width=140
Column 7  Width=110
Column 8  Weight=1.0000

[Docking][Data]
DockSpace             ID=0x8B93E3BD Window=0xA787BDB4 Pos=0,22 Size=1280,778 Split=Y
  DockNode            ID=0x0000000B Parent=0x8B93E3BD SizeRef=1920,79 HiddenTabBar=1 Selected=0x039BEE69
  DockNode            ID=0x0000000C Parent=0x8B93E3BD SizeRef=1920,697 Split=Y
    DockNode          ID=0x00000009 Parent=0x0000000C SizeRef=1920,99 HiddenTabBar=1 Selected=0x039BEE69
    DockNode          ID=0x0000000A Parent=0x0000000C SizeRef=1920,1068 Split=Y
      DockNode        ID=0x00000007 Parent=0x0000000A SizeRef=1920,96 Selected=0x039BEE69
      DockNode        ID=0x00000008 Parent=0x0000000A SizeRef=1920,1071 Split=Y
        DockNode      ID=0x00000001 Parent=0x00000008 SizeRef=1920,97 HiddenTabBar=1 Selected=0x039BEE69
        DockNode      ID=0x00000004 Parent=0x00000008 SizeRef=1920,1070 Split=Y
          DockNode    ID=0x00000005 Parent=0x00000004 SizeRef=1280,127 HiddenTabBar=1 Selected=0x039BEE69
          DockNode    ID=0x00000006 Parent=0x00000004 SizeRef=1280,671 Split=Y
            DockNode  ID=0x00000002 Parent=0x00000006 SizeRef=1280,458 CentralNode=1 Selected=0xF69CCEB7
            DockNode  ID=0x00000003 Parent=0x00000006 SizeRef=1280,237 Selected=0xB7722E25
)";

int    gArgc;
char** gArgv;

static void* call_qemu_main(void* opaque)
{
	int status;
	DPRINTF("Second thread: calling qemu_main()\n");
	status = qemu_main(gArgc, gArgv, nullptr);
	DPRINTF("Second thread: qemu_main() returned, exiting\n");
	exit(status);
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		// Launched with a console. If stdout and stderr are not associated with
		// an output stream, redirect to parent console.
		if (_fileno(stdout) == -2) freopen("CONOUT$", "w+", stdout);
		if (_fileno(stderr) == -2) freopen("CONOUT$", "w+", stderr);
	}
	else
	{
		// Launched without a console. Redirect stdout and stderr to a log file.
		const char* logFile = "shuriken.log";
		HANDLE logfile = CreateFileA(logFile, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (logfile != INVALID_HANDLE_VALUE)
		{
			freopen(logFile, "a", stdout);
			freopen(logFile, "a", stderr);
		}
	}
#endif

	ui::Log("shuriken_version: %s", shuriken_version);
	ui::Log("shuriken_branch: %s", shuriken_branch);
	ui::Log("shuriken_commit: %s", shuriken_commit);
	ui::Log("shuriken_date: %s", shuriken_date);

	DPRINTF("Entered main()\n");
	gArgc = argc;
	gArgv = argv;

	xsettingsInit();
	xsettingsLoad();
	ui::LoadingGame(xsettings.dvd_path);

	sdl2_display_very_early_init(nullptr);

	qemu_sem_init(&display_init_sem, 0);

	QemuThread thread;
	qemu_thread_create(&thread, "qemu_main", call_qemu_main, nullptr, QEMU_THREAD_DETACHED);

	DPRINTF("Main thread: waiting for display_init_sem\n");
	qemu_sem_wait(&display_init_sem);

	gui_grab = 0;
	if (gui_fullscreen)
	{
		sdl_grab_start(0);
		set_full_screen(&sdl2_console[0], gui_fullscreen);
	}

	// FIXME: May want to create a callback mechanism for main QEMU thread to just run functions to avoid TLS bugs and locking issues.
	tcg_register_init_ctx();
	// rcu_register_thread();
	qemu_set_current_aio_context(qemu_get_aio_context());

	DPRINTF("Main thread: initializing app\n");
	auto iniPath = xsettingsFolder() / "imgui.ini";
	if (!std::filesystem::exists(iniPath))
	{
		std::ofstream out(iniPath.string());
		out.write(defaultIni.c_str(), defaultIni.size());
		out.close();
	}

	str2k iniFilename;
	strcpy(iniFilename, iniPath.string().c_str());
	auto& io = ImGui::GetIO();
	io.IniFilename = iniFilename;
	ImGui::LoadIniSettingsFromDisk(iniFilename);

	while (true)
		sdl2_gl_refresh(&sdl2_console[0].dcl);

	// rcu_unregister_thread();
}