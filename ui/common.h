// common.h
// @2021 octopoulos

#pragma once

bool        IsRunning();
void        LoadDisc();
const char* PausedFileOpen(int flags, const char* filters, const char* default_path, const char* default_name);
void        TogglePause();
