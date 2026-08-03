#include "input_capabilities.h"

#include <cassert>

int main() {
  using areca::requiresUinputForCapabilityMask;

  assert(requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x72)));
  assert(!requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0x90072)));
  assert(!requiresUinputForCapabilityMask(
      fcitx::CapabilityFlags(0xE001800072ULL)));
  assert(!requiresUinputForCapabilityMask(fcitx::CapabilityFlags(0)));
  return 0;
}
