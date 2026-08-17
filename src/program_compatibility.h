#pragma once

#include <string>

namespace areca {

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

// Terminal applications across all Linux desktop environments (GNOME, MATE, Mint, XFCE, KDE, etc.).
bool isTerminalProgram(const std::string &program);

} // namespace areca
