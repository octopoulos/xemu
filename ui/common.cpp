// common.cpp
// @2021 octopoulos

#include "common.h"
#include "xemu-shaders.h"   // load_texture_from_file

extern "C" {
#include "qemui/noc_file_dialog.h"

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
}

void xemu_load_disc(const char* path, bool saveSetting);

namespace ui
{

std::map<std::string, uint32_t> textures;

bool ImageTextButton(std::string name)
{
	static ImVec2 buttonSize(32.0f, 32.0f);

	bool click;
	if (textures.contains(name))
		click = ImGui::ImageButton((void*)(intptr_t)textures[name], buttonSize);
	else
		click = ImGui::Button(name.c_str());

	return click;
}

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

uint32_t LoadTexture(std::filesystem::path path, std::string name)
{
	if (!std::filesystem::exists(path))
		return 0;

	auto texId = load_texture_from_file(path.string().c_str(), 0);
    if (texId)
	    textures[name] = texId;
	return texId;
}

bool LoadTextures(std::string folder, std::vector<std::string> names)
{
	bool success = true;

	std::filesystem::path basePath = xsettingsFolder(nullptr);
	basePath /= folder;

	for (auto& name : names)
	{
		std::filesystem::path path = basePath / (name + ".png");
        if (!LoadTexture(path, name))
            success = false;
	}

	return success;
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

} // namespace ui
