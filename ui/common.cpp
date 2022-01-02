// common.cpp
// @2021 octopoulos

#include "common.h"
#include "xsettings.h"

extern "C" {
#include "noc_file_dialog.h"

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
}

void xemu_load_disc(const char* path, bool saveSetting);

bool IsRunning()
{
    return runstate_is_running();
}

void LoadDisc()
{
	const char* filters  = ".iso Files\0*.iso\0All Files\0*.*\0";
	const char* current  = xsettings.dvd_path;
	const char* filename = PausedFileOpen(NOC_FILE_DIALOG_OPEN, filters, current, nullptr);
	xemu_load_disc(filename, true);
}

const char* PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name)
{
	bool is_running = runstate_is_running();
	if (is_running)
		vm_stop(RUN_STATE_PAUSED);

	const char* r = noc_file_dialog_open(flags, filters, default_path, default_name);
	if (is_running)
		vm_start();

	return r;
}

void TogglePause()
{
	if (IsRunning())
        vm_stop(RUN_STATE_PAUSED);
	else
        vm_start();
}
