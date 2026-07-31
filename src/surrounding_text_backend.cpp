#include "surrounding_text_backend.h"

#include "surrounding_text_cache.h"

namespace areca {

ApplyStatus SurroundingTextBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone) {
  if (plan.backspaceCount) {
    inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount),
                                       plan.backspaceCount);
    updateSurroundingCacheAfterDelete(
        inputContext, -static_cast<int>(plan.cacheDeleteCount),
        plan.cacheDeleteCount);
  }
  if (!plan.commitText.empty()) {
    inputContext.commitString(plan.commitText);
    updateSurroundingCacheAfterCommit(inputContext, plan.commitText);
  }
  return ApplyStatus::Completed;
}

} // namespace areca
