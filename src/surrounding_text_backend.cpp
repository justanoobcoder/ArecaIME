#include "surrounding_text_backend.h"

namespace areca {

ApplyStatus SurroundingTextBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone) {
  if (plan.backspaceCount) {
    inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount),
                                       plan.backspaceCount);
  }
  if (!plan.commitText.empty()) {
    inputContext.commitString(plan.commitText);
  }
  return ApplyStatus::Completed;
}

} // namespace areca
