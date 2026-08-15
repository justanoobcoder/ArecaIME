#pragma once

#include <string>

#include <fcitx-utils/capabilityflags.h>

namespace areca {

bool isVSCodeFamilyProgram(const std::string &program);
bool requiresForwardBackspaceForCapabilityMask(fcitx::CapabilityFlags flags,
                                               const std::string &program);

} // namespace areca
