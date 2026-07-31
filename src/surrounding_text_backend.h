#pragma once

#include "rewrite_backend.h"

namespace areca {

class SurroundingTextBackend final : public RewriteBackend {
public:
  const char *name() const override { return "surrounding-text"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;
};

} // namespace areca
