/*
 * xemu Input Management
 *
 * This is the main input abstraction layer for xemu, which is basically just a
 * wrapper around SDL2 GameController/Keyboard API to map specifically to an
 * Xbox gamepad and support automatic binding, hotplugging, and removal at
 * runtime.
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

#include <SDL2/SDL.h>
#include "qemu/queue.h"

enum controller_state_buttons_mask
{
	PAD_BUTTON_A          = (1 << 0),
	PAD_BUTTON_B          = (1 << 1),
	PAD_BUTTON_X          = (1 << 2),
	PAD_BUTTON_Y          = (1 << 3),
	PAD_BUTTON_DPAD_LEFT  = (1 << 4),
	PAD_BUTTON_DPAD_UP    = (1 << 5),
	PAD_BUTTON_DPAD_RIGHT = (1 << 6),
	PAD_BUTTON_DPAD_DOWN  = (1 << 7),
	PAD_BUTTON_BACK       = (1 << 8),
	PAD_BUTTON_START      = (1 << 9),
	PAD_BUTTON_WHITE      = (1 << 10),
	PAD_BUTTON_BLACK      = (1 << 11),
	PAD_BUTTON_LSTICK     = (1 << 12),
	PAD_BUTTON_RSTICK     = (1 << 13),
	// extensions
	PAD_BUTTON_GUIDE      = (1 << 14),
	PAD_BUTTON_TOUCHPAD   = (1 << 15),
	PAD_BUTTON_MISC1      = (1 << 16),
	PAD_BUTTON_PADDLE1    = (1 << 17),
	PAD_BUTTON_PADDLE2    = (1 << 18),
	PAD_BUTTON_PADDLE3    = (1 << 19),
	PAD_BUTTON_PADDLE4    = (1 << 20),
};

#define CONTROLLER_STATE_BUTTON_ID_TO_MASK(x) (1 << x)

enum controller_state_axis_index
{
	PAD_AXIS_LTRIG,
	PAD_AXIS_RTRIG,
	PAD_AXIS_LSTICK_X,
	PAD_AXIS_LSTICK_Y,
	PAD_AXIS_RSTICK_X,
	PAD_AXIS_RSTICK_Y,
	PAD_AXIS__COUNT,
};

enum controller_input_device_type
{
	INPUT_DEVICE_SDL_KEYBOARD,
	INPUT_DEVICE_SDL_GAMECONTROLLER,
};

typedef struct ControllerState
{
	QTAILQ_ENTRY(ControllerState)
	entry;

	int64_t last_input_updated_ts;
	int64_t last_haptic_updated_ts;

	// Input state
	int     buttons;
	int16_t axis[PAD_AXIS__COUNT];
	int     raw_inputs[32];

	// Rendering state hacked on here for convenience but needs to be moved (FIXME)
	uint32_t animate_guide_button_end;
	uint32_t animate_trigger_end;

	// Rumble state
	uint16_t rumble_l;
	uint16_t rumble_r;

	enum controller_input_device_type type;
	const char*                       name;
	SDL_GameController*               sdl_gamecontroller; // if type == INPUT_DEVICE_SDL_GAMECONTROLLER
	SDL_Haptic*                       sdl_haptic;
	SDL_HapticEffect                  sdl_haptic_effect;
	int                               sdl_haptic_effect_id;
	SDL_Joystick*                     sdl_joystick;
	SDL_JoystickID                    sdl_joystick_id;
	SDL_JoystickGUID                  sdl_joystick_guid;

	// pad/key mapping
	char pad_smapping[256];
	int  pad_mapping[32];
	char key_smapping[256];
	int  key_mapping[32];

	int   bound;  // Which port this input device is bound to
	void* device; // DeviceState opaque
} ControllerState;

typedef QTAILQ_HEAD(, ControllerState) ControllerStateList;
extern ControllerStateList available_controllers;
extern ControllerState*    bound_controllers[4];

#ifdef __cplusplus
extern "C" {
#endif

void             xemu_input_init(void);
void             xemu_input_process_sdl_events(const SDL_Event* event); // SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED
void             xemu_input_update_controllers(void);
void             xemu_input_update_controller(ControllerState* state);
void             xemu_input_update_sdl_kbd_controller_state(ControllerState* state);
void             xemu_input_update_sdl_controller_state(ControllerState* state);
void             xemu_input_update_rumble(ControllerState* state);
ControllerState* xemu_input_get_bound(int index);
void             xemu_input_bind(int index, ControllerState* state, int save);
int              xemu_input_get_controller_default_bind_port(ControllerState* state, int start, int end);
//
void             xemu_input_set_test_mode(int enabled);
int              xemu_input_get_test_mode(void);
//
void             ParseMappingString(char* mapping, int* vector, const char* defaultMapping);
void             StringifyMapping(int* mapping, char* text, const char* defaultMapping);

#ifdef __cplusplus
}
#endif
