#pragma once

#include <string>

namespace areca {

// Programs that need the forward-Backspace compatibility path when they
// expose the otherwise reliable 0x72 capability mask.
bool isVSCodeFamilyProgram(const std::string &program);

} // namespace areca
