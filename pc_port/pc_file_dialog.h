#ifndef PC_FILE_DIALOG_H
#define PC_FILE_DIALOG_H

#include <stddef.h>

// Native "open file" dialog for the desktop builds: the shell dialog on
// Windows, zenity or kdialog on Linux/BSD. Blocks until the user answers.
// `patterns` is a space-separated glob list ("*.zip *.rar"). Returns false
// when cancelled or when no dialog is available (Linux without zenity/kdialog:
// `out` then receives a short reason for the menu to show).
bool pc_file_dialog_open(const char* title, const char* filterName, const char* patterns, char* out, size_t outSize);

#endif
