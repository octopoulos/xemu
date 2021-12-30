/*
 * xemu Input Management
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "monitor/qdev.h"
#include "qapi/qmp/qdict.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "qemu/config-file.h"

#include "xemu-input.h"
#include "xemu-notifications.h"
#include "xsettings.h"

#define DEBUG_INPUT

#ifdef DEBUG_INPUT
#	define DPRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#	define DPRINTF(fmt, ...) do { } while (0)
#endif

#define XEMU_INPUT_MIN_INPUT_UPDATE_INTERVAL_US  2500
#define XEMU_INPUT_MIN_HAPTIC_UPDATE_INTERVAL_US 2500

ControllerStateList available_controllers = QTAILQ_HEAD_INITIALIZER(available_controllers);
ControllerState*    bound_controllers[4]  = { NULL, NULL, NULL, NULL };
int                 test_mode;

const int axis_mapping[10][3] = {
	{ CONTROLLER_AXIS_LTRIG, 32767, 0 },
	{ CONTROLLER_AXIS_RTRIG, 32767, 0 },
	{ CONTROLLER_AXIS_LSTICK_X, -32768, 0 },
	{ CONTROLLER_AXIS_LSTICK_Y, 32767, 1 },
	{ CONTROLLER_AXIS_LSTICK_X, 32767, 0 },
	{ CONTROLLER_AXIS_LSTICK_Y, -32768, 1 },
	{ CONTROLLER_AXIS_RSTICK_X, -32768, 0 },
	{ CONTROLLER_AXIS_RSTICK_Y, 32767, 1 },
	{ CONTROLLER_AXIS_RSTICK_X, 32767, 0 },
	{ CONTROLLER_AXIS_RSTICK_Y, -32768, 1 },
};

void xemu_input_init(void)
{
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

	if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL gamecontroller subsystem: %s\n", SDL_GetError());
		exit(1);
	}

	if (SDL_Init(SDL_INIT_HAPTIC) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL haptic subsystem: %s\n", SDL_GetError());
		exit(1);
	}

	// Create the keyboard input (always first)
	ControllerState* new_con = (ControllerState*)malloc(sizeof(ControllerState));
	memset(new_con, 0, sizeof(ControllerState));
	new_con->type  = INPUT_DEVICE_SDL_KEYBOARD;
	new_con->name  = "Keyboard";
	new_con->bound = -1;

	// Create USB Daughterboard for 1.0 Xbox. This is connected to Port 1 of the Root hub.
	QDict* usbhub_qdict = qdict_new();
	qdict_put_str(usbhub_qdict, "driver", "usb-hub");
	qdict_put_int(usbhub_qdict, "port", 1);
	qdict_put_int(usbhub_qdict, "ports", 4);
	QemuOpts*    usbhub_opts = qemu_opts_from_qdict(qemu_find_opts("device"), usbhub_qdict, &error_fatal);
	DeviceState* usbhub_dev  = qdev_device_add(usbhub_opts, &error_fatal);
	assert(usbhub_dev);

	// Check to see if we should auto-bind the keyboard
	int port = xemu_input_get_controller_default_bind_port(new_con, 0, 4);
	if (port >= 0)
	{
		xemu_input_bind(port, new_con, 0);
		char buf[128];
		snprintf(buf, sizeof(buf), "Connected '%s' to port %d", new_con->name, port + 1);
		xemu_queue_notification(buf, false);
	}

	QTAILQ_INSERT_TAIL(&available_controllers, new_con, entry);
}

/**
 * Convert a mapping string to an array of int
 * - if the mapping is empty => overridden by default
 */
void ParseMappingString(char* mapping, int* vector, const char* defaultMapping)
{
	int   i      = 0;
	int   length = 0;
	int   number = 0;
	char* text   = *mapping ? mapping : (char*)defaultMapping;

	for (char* c = text; *c; ++c)
	{
		if (*c >= '0' && *c <= '9')
		{
			number = number * 10 + (*c - '0');
			++length;
		}
		else if (*c == ',')
		{
			vector[i] = length ? number : -1;
			length    = 0;
			number    = 0;
			++i;
		}
	}

	if (number)
		vector[i] = number;
}

/**
 * Convert an array of int to a mapping string
 * - if the mapping is the same as default => ""
 */
void StringifyMapping(int* vector, char* mapping, const char* defaultMapping)
{
	char* text = mapping;
	*text      = 0;
	char buffer[8];

	for (int i = 0; i < 32; ++i)
	{
		if (vector[i] > -1)
		{
			itoa(vector[i], buffer, 10);
			for (char* c = buffer; *c; ++c)
				*text++ = *c;
		}
		*text++ = ',';
	}

	*text = 0;
	if (!strcmp(mapping, defaultMapping))
		*mapping = 0;
}

int xemu_input_get_controller_default_bind_port(ControllerState* state, int start, int end)
{
	char guid[35] = { 0 };
	if (state->type == INPUT_DEVICE_SDL_GAMECONTROLLER)
		SDL_JoystickGetGUIDString(state->sdl_joystick_guid, guid, sizeof(guid));
	else if (state->type == INPUT_DEVICE_SDL_KEYBOARD)
		snprintf(guid, sizeof(guid), "keyboard");

	for (int i = start; i < end; i++)
	{
		if (!strcmp(guid, xsettings.input_guid[i]))
		{
			strcpy(state->key_smapping, xsettings.input_keyb[i]);
			ParseMappingString(state->key_smapping, state->key_mapping, DEFAULT_KEYB_MAPPING);

			strcpy(state->pad_smapping, xsettings.input_pad[i]);
			ParseMappingString(state->pad_smapping, state->pad_mapping, DEFAULT_PAD_MAPPING);

			DPRINTF("i=%d guid=%s mapping=%s : %s\n", i, guid, state->pad_smapping, state->key_smapping);
			return i;
		}
	}

	return -1;
}

void xemu_input_process_sdl_events(const SDL_Event* event)
{
	if (event->type == SDL_CONTROLLERDEVICEADDED)
	{
		DPRINTF("Controller Added: %d\n", event->cdevice.which);

		// Attempt to open the added controller
		SDL_GameController* sdl_con;
		sdl_con = SDL_GameControllerOpen(event->cdevice.which);
		if (sdl_con == NULL)
		{
			DPRINTF("Could not open joystick %d as a game controller\n", event->cdevice.which);
			return;
		}

		// Success! Create a new node to track this controller and continue init
		ControllerState* new_con = (ControllerState*)malloc(sizeof(ControllerState));
		memset(new_con, 0, sizeof(ControllerState));
		new_con->type                 = INPUT_DEVICE_SDL_GAMECONTROLLER;
		new_con->name                 = SDL_GameControllerName(sdl_con);
		new_con->sdl_gamecontroller   = sdl_con;
		new_con->sdl_joystick         = SDL_GameControllerGetJoystick(new_con->sdl_gamecontroller);
		new_con->sdl_joystick_id      = SDL_JoystickInstanceID(new_con->sdl_joystick);
		new_con->sdl_joystick_guid    = SDL_JoystickGetGUID(new_con->sdl_joystick);
		new_con->sdl_haptic           = SDL_HapticOpenFromJoystick(new_con->sdl_joystick);
		new_con->sdl_haptic_effect_id = -1;
		new_con->bound                = -1;

		char guid_buf[35] = { 0 };
		SDL_JoystickGetGUIDString(new_con->sdl_joystick_guid, guid_buf, sizeof(guid_buf));
		DPRINTF("Opened %s (%s)\n", new_con->name, guid_buf);

		QTAILQ_INSERT_TAIL(&available_controllers, new_con, entry);

		// Do not replace binding for a currently bound device. In the case that
		// the same GUID is specified multiple times, on different ports, allow
		// any available port to be bound.
		//
		// This can happen naturally with X360 wireless receiver, in which each
		// controller gets the same GUID (go figure). We cannot remember which
		// controller is which in this case, but we can try to tolerate this
		// situation by binding to any previously bound port with this GUID. The
		// upside in this case is that a person can use the same GUID on all
		// ports and just needs to bind to the receiver and never needs to hit
		// this dialog.
		int port = 0;
		while (1)
		{
			port = xemu_input_get_controller_default_bind_port(new_con, port, 4);
			if (port < 0)
			{
				// No (additional) default mappings
				break;
			}
			if (xemu_input_get_bound(port) != NULL)
			{
				// Something already bound here, try again for another port
				port++;
				continue;
			}
			xemu_input_bind(port, new_con, 0);
			char buf[128];
			snprintf(buf, sizeof(buf), "Connected '%s' to port %d", new_con->name, port + 1);
			xemu_queue_notification(buf, false);
			break;
		}
	}
	else if (event->type == SDL_CONTROLLERDEVICEREMOVED)
	{
		DPRINTF("Controller Removed: %d\n", event->cdevice.which);
		int              handled = 0;
		ControllerState* iter;
		ControllerState* next;
		QTAILQ_FOREACH_SAFE(iter, &available_controllers, entry, next)
		{
			if (iter->type != INPUT_DEVICE_SDL_GAMECONTROLLER)
				continue;

			if (iter->sdl_joystick_id == event->cdevice.which)
			{
				DPRINTF("Device removed: %s\n", iter->name);

				// Disconnect
				if (iter->bound >= 0)
				{
					// Queue a notification to inform user controller disconnected
					// FIXME: Probably replace with a callback registration thing,
					// but this works well enough for now.
					char buf[128];
					snprintf(buf, sizeof(buf), "Port %d disconnected", iter->bound + 1);
					xemu_queue_notification(buf, false);

					// Unbind the controller, but don't save the unbinding in
					// case the controller is reconnected
					xemu_input_bind(iter->bound, NULL, 0);
				}

				// Unlink
				QTAILQ_REMOVE(&available_controllers, iter, entry);

				// Deallocate
				if (iter->sdl_haptic)
					SDL_HapticClose(iter->sdl_haptic);
				if (iter->sdl_gamecontroller)
					SDL_GameControllerClose(iter->sdl_gamecontroller);
				free(iter);

				handled = 1;
				break;
			}
		}
		if (!handled)
			DPRINTF("Could not find handle for joystick instance\n");
	}
	else if (event->type == SDL_CONTROLLERDEVICEREMAPPED)
		DPRINTF("Controller Remapped: %d\n", event->cdevice.which);
}

void xemu_input_update_controller(ControllerState* state)
{
	int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
	if (ABS(now - state->last_input_updated_ts) < XEMU_INPUT_MIN_INPUT_UPDATE_INTERVAL_US)
		return;

	if (state->type == INPUT_DEVICE_SDL_KEYBOARD)
		xemu_input_update_sdl_kbd_controller_state(state);
	else if (state->type == INPUT_DEVICE_SDL_GAMECONTROLLER)
		xemu_input_update_sdl_controller_state(state);

	state->last_input_updated_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
}

void xemu_input_update_controllers(void)
{
	ControllerState* iter;
	QTAILQ_FOREACH(iter, &available_controllers, entry) { xemu_input_update_controller(iter); }
	QTAILQ_FOREACH(iter, &available_controllers, entry) { xemu_input_update_rumble(iter); }
}

void xemu_input_update_sdl_kbd_controller_state(ControllerState* state)
{
	state->buttons = 0;
	memset(state->axis, 0, sizeof(state->axis));

	const uint8_t* kbd     = SDL_GetKeyboardState(NULL);
	int*           mapping = state->key_mapping;

	// buttons
	for (int i = 0; i < 21; ++i)
	{
		if (mapping[i] >= 0)
			state->buttons |= kbd[mapping[i]] << i;
	}

	// axes
	for (int i = 0; i < 10; ++i)
	{
		int key = mapping[i + 22];
		if (key >= 0 && kbd[key])
			state->axis[axis_mapping[i][0]] = axis_mapping[i][1];
	}
}

void xemu_input_update_sdl_controller_state(ControllerState* state)
{
	// get raw data
	// it's useful to detect which input was changed (controller remapping)
	int* raw_inputs = state->raw_inputs;
	memset(raw_inputs, 0, sizeof(state->raw_inputs));
	for (int i = 0; i < 21; ++i)
		raw_inputs[i] = (int)SDL_GameControllerGetButton(state->sdl_gamecontroller, (SDL_GameControllerButton)i);

	for (int i = 0; i < 6; ++i)
		raw_inputs[i + 22] = SDL_GameControllerGetAxis(state->sdl_gamecontroller, (SDL_GameControllerAxis)i);

	// buttons
	// note: axes can be assigned to buttons too
	int* mapping   = state->pad_mapping;
	state->buttons = 0;

	for (int i = 0; i < 21; ++i)
	{
		int key = mapping[i];
		if (key >= 32)
		{
			key -= 32;
			if (key < SDL_CONTROLLER_AXIS_MAX)
			{
				int value = (abs(raw_inputs[key + 22]) > 8000) ? 1 : 0;
				state->buttons |= value << i;
			}
		}
		else if (key >= 0 && key < SDL_CONTROLLER_BUTTON_MAX)
			state->buttons |= raw_inputs[key] << i;
	}

	// axes
	// note: buttons can be assigned to axes too
	memset(state->axis, 0, sizeof(state->axis));

	for (int i = 0; i < 10; ++i)
	{
		int key = mapping[i + 22];
		if (key >= 32)
		{
			key -= 32;
			if (key < SDL_CONTROLLER_AXIS_MAX)
			{
				int value = raw_inputs[key + 22];
				if (axis_mapping[i][2])
					value = -1 - value;
				state->axis[axis_mapping[i][0]] = value;
			}
		}
		else if (key >= 0 && key < SDL_CONTROLLER_BUTTON_MAX)
		{
			if (raw_inputs[key])
				state->axis[axis_mapping[i][0]] = axis_mapping[i][1];
		}
	}
}

void xemu_input_update_rumble(ControllerState* state)
{
	if (state->sdl_haptic == NULL)
	{
		// Haptic not supported for this joystick
		return;
	}

	int64_t now = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
	if (ABS(now - state->last_haptic_updated_ts) < XEMU_INPUT_MIN_HAPTIC_UPDATE_INTERVAL_US)
		return;

	memset(&state->sdl_haptic_effect, 0, sizeof(state->sdl_haptic_effect));
	state->sdl_haptic_effect.type                      = SDL_HAPTIC_LEFTRIGHT;
	state->sdl_haptic_effect.leftright.length          = SDL_HAPTIC_INFINITY;
	state->sdl_haptic_effect.leftright.large_magnitude = state->rumble_l >> 1;
	state->sdl_haptic_effect.leftright.small_magnitude = state->rumble_r >> 1;
	if (state->sdl_haptic_effect_id == -1)
	{
		state->sdl_haptic_effect_id = SDL_HapticNewEffect(state->sdl_haptic, &state->sdl_haptic_effect);
		SDL_HapticRunEffect(state->sdl_haptic, state->sdl_haptic_effect_id, 1);
	}
	else
		SDL_HapticUpdateEffect(state->sdl_haptic, state->sdl_haptic_effect_id, &state->sdl_haptic_effect);

	state->last_haptic_updated_ts = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
}

ControllerState* xemu_input_get_bound(int index)
{
	return bound_controllers[index];
}

void xemu_input_bind(int index, ControllerState* state, int save)
{
	// FIXME: Check if this works
	// Attempt to disable rumble when unbinding so it's not left in rumble mode
	if (state && state->sdl_haptic && state->sdl_haptic_effect_id >= 0)
		SDL_HapticStopEffect(state->sdl_haptic, state->sdl_haptic_effect_id);

	// Unbind existing controller
	if (bound_controllers[index])
	{
		assert(bound_controllers[index]->device != NULL);
		Error* err = NULL;
		qdev_unplug((DeviceState*)bound_controllers[index]->device, &err);
		assert(err == NULL);

		bound_controllers[index]->bound  = -1;
		bound_controllers[index]->device = NULL;
		bound_controllers[index]         = NULL;
	}

	// Save this controller's GUID in settings for auto re-connect
	if (save)
	{
		char guid_buf[35] = { 0 };
		if (state)
		{
			if (state->type == INPUT_DEVICE_SDL_GAMECONTROLLER)
				SDL_JoystickGetGUIDString(state->sdl_joystick_guid, guid_buf, sizeof(guid_buf));
			else if (state->type == INPUT_DEVICE_SDL_KEYBOARD)
				strcpy(guid_buf, "keyboard");
		}

		strcpy(xsettings.input_guid[index], guid_buf);
		xsettingsSave();
	}

	// Bind new controller
	if (state)
	{
		if (state->bound >= 0)
		{
			// Device was already bound to another port. Unbind it.
			xemu_input_bind(state->bound, NULL, 1);
		}
		xemu_input_get_controller_default_bind_port(state, index, index + 1);

		bound_controllers[index]        = state;
		bound_controllers[index]->bound = index;

		const int port_map[4] = { 3, 4, 1, 2 };
		char*     tmp;

		// Create controller's internal USB hub.
		QDict* usbhub_qdict = qdict_new();
		qdict_put_str(usbhub_qdict, "driver", "usb-hub");
		tmp = g_strdup_printf("1.%d", port_map[index]);
		qdict_put_str(usbhub_qdict, "port", tmp);
		qdict_put_int(usbhub_qdict, "ports", 3);
		QemuOpts*    usbhub_opts = qemu_opts_from_qdict(qemu_find_opts("device"), usbhub_qdict, &error_abort);
		DeviceState* usbhub_dev  = qdev_device_add(usbhub_opts, &error_abort);
		g_free(tmp);

		// Create XID controller. This is connected to Port 1 of the controller's internal USB Hub
		QDict* qdict = qdict_new();

		// Specify device driver
		qdict_put_str(qdict, "driver", "usb-xbox-gamepad");

		// Specify device identifier
		static int id_counter = 0;
		tmp                   = g_strdup_printf("gamepad_%d", id_counter++);
		qdict_put_str(qdict, "id", tmp);
		g_free(tmp);

		// Specify index/port
		qdict_put_int(qdict, "index", index);
		tmp = g_strdup_printf("1.%d.1", port_map[index]);
		qdict_put_str(qdict, "port", tmp);
		g_free(tmp);

		// Create the device
		QemuOpts*    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict, &error_abort);
		DeviceState* dev  = qdev_device_add(opts, &error_abort);
		assert(dev);

		// Unref for eventual cleanup
		qobject_unref(usbhub_qdict);
		object_unref(OBJECT(usbhub_dev));
		qobject_unref(qdict);
		object_unref(OBJECT(dev));

		state->device = usbhub_dev;
	}
}

void xemu_input_set_test_mode(int enabled)
{
	test_mode = enabled;
}

int xemu_input_get_test_mode(void)
{
	return test_mode;
}
