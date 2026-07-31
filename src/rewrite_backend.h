#pragma once

#include <functional>

#include <fcitx/inputcontext.h>

#include "types.h"

namespace areca {

enum class ApplyStatus {
  Completed,
  Pending,
  Failed,
};

using RewriteDone = std::function<void(uint64_t transactionId)>;

class RewriteBackend {
public:
  virtual ~RewriteBackend() = default;
  virtual const char *name() const = 0;
  virtual ApplyStatus apply(fcitx::InputContext &inputContext,
                            const RewritePlan &plan, RewriteDone onDone) = 0;
  virtual bool recoverUnsentFailure() { return false; }
};

} // namespace areca
