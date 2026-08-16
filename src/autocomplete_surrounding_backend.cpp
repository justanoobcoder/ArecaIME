#include "autocomplete_surrounding_backend.h"

#include <utility>

#include <fcitx-utils/keysym.h>

namespace areca {

const char *AutocompleteForwardSurroundingBackend::name() const {
  return forwardedBackspaces_ == 2 ? "autocomplete-edge-forward2+surrounding"
                                   : "autocomplete-forward+surrounding";
}

ApplyStatus
AutocompleteForwardSurroundingBackend::apply(fcitx::InputContext &inputContext,
                                             const RewritePlan &plan,
                                             RewriteDone onDone) {
  const fcitx::Key backspace(FcitxKey_BackSpace);
  for (uint32_t i = 0; i < forwardedBackspaces_; ++i) {
    inputContext.forwardKey(backspace);
    inputContext.forwardKey(backspace, true);
  }

  return surroundingBackend_.apply(inputContext, plan, std::move(onDone));
}

} // namespace areca
