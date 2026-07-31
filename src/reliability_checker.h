#pragma once

#include <cstdint>
#include <string>

namespace fcitx {
class InputContext;
}

namespace areca {

struct SurroundingReliabilityState {
  bool known = false;
  bool reliable = false;

  void reset() {
    known = false;
    reliable = false;
  }
};

struct ReliabilityDecision {
  bool useSurrounding = false;
  bool browserAutocomplete = false;
  uint32_t additionalFallbackBackspaces = 0;
};

class ReliabilityChecker {
public:
  ReliabilityDecision evaluate(fcitx::InputContext &inputContext,
                               const std::string &shownText,
                               SurroundingReliabilityState &state,
                               bool debug) const;
};

} // namespace areca
