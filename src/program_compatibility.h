#pragma once

#include <string>

namespace areca {

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

// Terminal applications across all Linux desktop environments (GNOME, MATE, Mint, XFCE, KDE, etc.).
bool isTerminalProgram(const std::string &program);

// Firefox and its forks (Zen, LibreWolf, etc.) which have broken SurroundingText
// deletion on Wayland and report the 0x72 capability mask.
bool isFirefoxFamilyProgram(const std::string &program);

} // namespace areca
