#include "surrounding_text_backend.h"

#include <utility>

#include <fcitx-utils/event.h>
#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

SurroundingTextBackend::SurroundingTextBackend(
    fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
    ProbeSucceededHandler onProbeSucceeded, ProbeFailedHandler onProbeFailed,
    FallbackBackendProvider fallbackBackendProvider)
    : eventLoop_(eventLoop), commitPost_(eventLoop),
      debugProvider_(std::move(debugProvider)),
      onProbeSucceeded_(std::move(onProbeSucceeded)),
      onProbeFailed_(std::move(onProbeFailed)),
      fallbackBackendProvider_(std::move(fallbackBackendProvider)) {}

SurroundingTextBackend::~SurroundingTextBackend() { clearPending(); }

ApplyStatus SurroundingTextBackend::apply(fcitx::InputContext &inputContext,
                                          const RewritePlan &plan,
                                          RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  if (plan.backspaceCount == 0) {
    if (!plan.commitText.empty()) {
      inputContext.commitString(plan.commitText);
      updateSurroundingCacheAfterCommit(inputContext, plan.commitText);
    }
    if (onDone) {
      onDone(plan.transactionId, RewriteOutcome::Succeeded);
    }
    return ApplyStatus::Completed;
  }

  const bool isProbe = plan.probeSurrounding;
  if (isProbe) {
    auto expected = inputContext.surroundingText();
    expected.deleteText(-static_cast<int>(plan.backspaceCount),
                        plan.backspaceCount);
    expectedProbeText_ = expected.text();
    expectedProbeCursor_ = expected.cursor();
    expectedProbeAnchor_ = expected.anchor();
  }

  // Arm the transaction before asking the frontend to delete. Some frontends
  // can publish SurroundingTextUpdated synchronously from this call.
  transactionId_ = plan.transactionId;
  inputContext_ = inputContext.watch();
  onDone_ = std::move(onDone);
  waitMs_ = plan.surroundingWaitMs;
  timerAccuracyUsec_ = plan.timerAccuracyUsec;
  commitText_ = plan.commitText;
  isProbe_ = isProbe;
  savedPlan_ = plan;

  if (!isProbe) {
    inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount),
                                       plan.backspaceCount);
    updateSurroundingCacheAfterDelete(
        inputContext, -static_cast<int>(plan.backspaceCount),
        plan.backspaceCount);
  } else {
    inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount),
                                       plan.backspaceCount);
  }

  if (debugProvider_()) {
    const char *frontend = inputContext.frontend();
    FCITX_INFO() << "areca: surrounding-text start tx=" << transactionId_
                 << " delete=" << plan.backspaceCount
                 << " wait_ms=" << waitMs_
                 << " probe=" << isProbe_
                 << " frontend=" << (frontend ? frontend : "");
  }

  // A synchronous update may already have confirmed the probe and scheduled
  // the commit while deleteSurroundingText() was on the stack.
  if (isProbe_) {
    scheduleProbeDeadline();
  } else if (!isProbe) {
    scheduleCommit();
  }
  return ApplyStatus::Pending;
}

void SurroundingTextBackend::notifySurroundingTextUpdated(
    fcitx::InputContext &inputContext) {
  if (!isProbe_) {
    return;
  }
  auto *ic = inputContext_.get();
  if (!ic || ic != &inputContext) {
    return;
  }

  const auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid() || surrounding.text() != expectedProbeText_ ||
      surrounding.cursor() != expectedProbeCursor_ ||
      surrounding.anchor() != expectedProbeAnchor_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: surrounding probe ignored unmatched update"
                   << " tx=" << transactionId_
                   << " expected_text=" << expectedProbeText_
                   << " actual_text=" << surrounding.text()
                   << " expected_cursor=" << expectedProbeCursor_
                   << " actual_cursor=" << surrounding.cursor()
                   << " expected_anchor=" << expectedProbeAnchor_
                   << " actual_anchor=" << surrounding.anchor();
    }
    return;
  }

  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding probe succeeded via SurroundingTextUpdated event"
                 << " tx=" << transactionId_;
  }

  isProbe_ = false;
  timer_.reset();

  if (onProbeSucceeded_) {
    onProbeSucceeded_(inputContext);
  }
  // The event already installed the confirmed surrounding snapshot in the
  // input context. Mutating it again here would delete the same text twice.
  scheduleCommit();
}

void SurroundingTextBackend::scheduleCommit() {
  schedule(waitMs_, [this]() {
    // Không commit ngay trong callback timer. Đưa sang pha post để event loop
    // phản ánh backlog của hệ thống trước khi chèn text mới.
    commitPost_.schedule([this]() { commitAndComplete(); });
  });
}

void SurroundingTextBackend::commitAndComplete() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (!commitText_.empty()) {
    inputContext->commitString(commitText_);
    updateSurroundingCacheAfterCommit(*inputContext, commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text complete tx=" << transactionId
                 << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextBackend::completeWithoutCommit() {
  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    FCITX_INFO() << "areca: surrounding-text context lost tx=" << transactionId;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void SurroundingTextBackend::schedule(uint32_t delayMs,
                                      std::function<void()> callback) {
  timer_.reset();
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(delayMs) * 1000;
  timer_ =
      eventLoop_.addTimeEvent(CLOCK_MONOTONIC, deadline, timerAccuracyUsec_,
                              [this, callback = std::move(callback)](
                                  fcitx::EventSourceTime *, uint64_t) mutable {
                                auto completedTimer = std::move(timer_);
                                callback();
                                return false;
                              });
  if (!timer_) {
    callback();
    return;
  }
  timer_->setOneShot();
}

void SurroundingTextBackend::scheduleProbeDeadline() {
  // Đặt deadline 150ms. Nếu không nhận được SurroundingTextUpdated event
  // trong thời gian này thì đánh giá ứng dụng không hỗ trợ và chuyển backend.
  schedule(150, [this]() {
    if (!isProbe_) {
      return;
    }
    auto *inputContext = inputContext_.get();
    if (!inputContext) {
      completeWithoutCommit();
      return;
    }
    // The snapshot may already be current even when the corresponding event
    // was delayed in the frontend queue. Validate once more before falling
    // back to an input-emulation backend.
    notifySurroundingTextUpdated(*inputContext);
    if (!isProbe_) {
      return;
    }
    if (debugProvider_()) {
      FCITX_INFO()
          << "areca: surrounding delete unconfirmed after 150ms tx="
          << transactionId_;
    }
    handleProbeFailure();
  });
}

void SurroundingTextBackend::handleProbeFailure() {
  auto *inputContext = inputContext_.get();
  if (!inputContext) {
    completeWithoutCommit();
    return;
  }

  if (onProbeFailed_) {
    onProbeFailed_(*inputContext);
  }

  RewriteBackend *fallbackBackend =
      fallbackBackendProvider_ ? fallbackBackendProvider_(*inputContext)
                               : nullptr;

  RewritePlan plan = savedPlan_;
  plan.probeSurrounding = false;
  auto onDone = std::move(onDone_);
  fcitx::TrackableObjectReference<fcitx::InputContext> icWatch =
      inputContext->watch();

  clearPending();

  auto *ic = icWatch.get();
  if (!ic) {
    if (onDone) {
      onDone(plan.transactionId, RewriteOutcome::Failed);
    }
    return;
  }

  if (fallbackBackend) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: fallback from unconfirmed surrounding delete to backend="
                   << fallbackBackend->name() << " tx=" << plan.transactionId;
    }
    // Keep our callback until apply() confirms ownership. A backend returning
    // Failed is not required to retain or invoke the callback.
    const auto status = fallbackBackend->apply(*ic, plan, onDone);
    if (status == ApplyStatus::Failed) {
      if (debugProvider_()) {
        FCITX_INFO() << "areca: fallback backend failed tx="
                     << plan.transactionId;
      }
      if (onDone) {
        onDone(plan.transactionId, RewriteOutcome::Failed);
      }
    }
    return;
  }

  if (onDone) {
    onDone(plan.transactionId, RewriteOutcome::Failed);
  }
}

void SurroundingTextBackend::clearPending() {
  timer_.reset();
  commitPost_.cancel();
  inputContext_.unwatch();
  onDone_ = {};
  transactionId_ = 0;
  waitMs_ = 0;
  timerAccuracyUsec_ = 1;
  commitText_.clear();
  isProbe_ = false;
  savedPlan_ = {};
  expectedProbeText_.clear();
  expectedProbeCursor_ = 0;
  expectedProbeAnchor_ = 0;
}

} // namespace areca
