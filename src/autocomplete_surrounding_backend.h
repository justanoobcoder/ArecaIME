#pragma once

#include <cstdint>

#include "rewrite_backend.h"
#include "surrounding_text_backend.h"

namespace areca {

class AutocompleteForwardSurroundingBackend final : public RewriteBackend {
public:
  explicit AutocompleteForwardSurroundingBackend(
      uint32_t forwardedBackspaces = 1)
      : forwardedBackspaces_(forwardedBackspaces) {}

  const char *name() const override;
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

private:
  SurroundingTextBackend surroundingBackend_;
  uint32_t forwardedBackspaces_ = 1;
};

} // namespace areca
