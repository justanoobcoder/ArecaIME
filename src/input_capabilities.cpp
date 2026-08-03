#include "input_capabilities.h"

namespace areca {
namespace {

constexpr uint64_t kUinputOnlyCapabilityMask = 0x72;

} // namespace

bool requiresUinputForCapabilityMask(fcitx::CapabilityFlags flags) {
  return flags.toInteger() == kUinputOnlyCapabilityMask;
}

} // namespace areca
