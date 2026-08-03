#include "input_capabilities.h"

#include <cassert>

int main() {
  using areca::isVSCodeFamilyProgram;
  using areca::requiresUinputForCapabilityMask;

  assert(isVSCodeFamilyProgram("code"));
  assert(isVSCodeFamilyProgram("code-oss"));
  assert(isVSCodeFamilyProgram("/usr/bin/Code.desktop"));
  assert(isVSCodeFamilyProgram("antigravity-ide"));
  assert(isVSCodeFamilyProgram("cursor"));
  assert(isVSCodeFamilyProgram("com.vscodium.codium"));
  assert(!isVSCodeFamilyProgram("decode"));
  assert(!isVSCodeFamilyProgram("firefox"));
  assert(!isVSCodeFamilyProgram(""));

  assert(requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x72),
                                         "code-oss"));
  assert(requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x72),
                                         "antigravity-ide"));
  assert(!requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x72),
                                          "firefox"));
  assert(!requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x72), ""));
  assert(!requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x90072),
                                          "code-oss"));
  assert(!requiresUinputForCapabilityMask(
      fcitx::CapabilityFlags(0xE001800072ULL), "code-oss"));
  assert(
      !requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0), "code-oss"));
  return 0;
}
