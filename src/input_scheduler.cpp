#include "input_scheduler.h"

#include <algorithm>
#include <exception>
#include <utility>

#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {

InputScheduler::InputScheduler(fcitx::EventLoop &eventLoop,
                               EngineResolver engineResolver,
                               TimingProvider timingProvider,
                               DebugProvider debugProvider,
                               RewriteBackendSelector rewriteBackendSelector)
    : eventLoop_(eventLoop), engineResolver_(std::move(engineResolver)),
      timingProvider_(std::move(timingProvider)),
      debugProvider_(std::move(debugProvider)),
      rewriteBackendSelector_(std::move(rewriteBackendSelector)) {}

void InputScheduler::enqueue(fcitx::InputContext &inputContext,
                             const fcitx::Key &originalKey, uint32_t codepoint,
                             std::string utf8Text) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.enqueuedAtUsec = fcitx::now(CLOCK_MONOTONIC);
  key.codepoint = codepoint;
  key.utf8Text = std::move(utf8Text);
  key.originalKey = originalKey;
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    FCITX_INFO() << "areca: queue push kind=text seq=" << key.sequence
                 << " text=" << key.utf8Text
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

void InputScheduler::resetContext(fcitx::InputContext &inputContext) {
  // The active timer was calculated from the old queue head. Re-arm it after
  // removal so a later key can never inherit an earlier key's deadline.
  timer_.reset();
  queue_.removeFor(inputContext);
  if (auto *engine = engineResolver_(inputContext)) {
    engine->reset();
  }
  scheduleNext();
}

void InputScheduler::scheduleNext() {
  if (processing_ || timer_ || queue_.empty() || stalled_) {
    return;
  }

  // A key is never processed inline. It must spend keyIntervalMs in the
  // queue, and consecutive Bamboo calls are also separated by that interval.
  const auto timing = timingProvider_();
  const uint64_t intervalUsec =
      static_cast<uint64_t>(timing.keyIntervalMs) * 1000;
  const uint64_t nowUsec = fcitx::now(CLOCK_MONOTONIC);
  const uint64_t queuedDeadline = queue_.front().enqueuedAtUsec + intervalUsec;
  const uint64_t spacingDeadline =
      lastProcessedAtUsec_ ? lastProcessedAtUsec_ + intervalUsec : 0;
  const uint64_t deadline =
      std::max(nowUsec, std::max(queuedDeadline, spacingDeadline));
  if (debugProvider_()) {
    FCITX_INFO() << "areca: scheduler arm now=" << nowUsec
                 << " deadline=" << deadline << " depth=" << queue_.size();
  }

  timer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0,
      [this](fcitx::EventSourceTime *, uint64_t nowUsec) {
        auto completedTimer = std::move(timer_);
        processNext(nowUsec);
        return false;
      });
  timer_->setOneShot();
}

void InputScheduler::processNext(uint64_t nowUsec) {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }

  auto key = queue_.pop();
  auto *inputContext = key.inputContext.get();
  if (!inputContext) {
    scheduleNext();
    return;
  }
  auto *engine = engineResolver_(*inputContext);
  if (!engine) {
    scheduleNext();
    return;
  }

  processing_ = true;
  lastProcessedAtUsec_ = nowUsec;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: scheduler process seq=" << key.sequence
                 << " kind=text"
                 << " depth_after=" << queue_.size();
  }
  try {
    applyResult(*inputContext, *engine,
                engine->process(key.codepoint, key.utf8Text), key.utf8Text,
                key.originalKey);
  } catch (const std::exception &error) {
    FCITX_ERROR() << "areca: Bamboo processing failed: " << error.what();
    engine->reset();
    finishKey();
  }
}

void InputScheduler::applyResult(fcitx::InputContext &inputContext,
                                 VietnameseEngine &engine,
                                 const BambooResult &result,
                                 const std::string &rawText,
                                 const fcitx::Key &originalKey) {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: bamboo result current=" << result.currentText
                 << " new=" << result.newText
                 << " delete=" << result.deleteCount
                 << " commit=" << result.commitText
                 << " macro=" << result.macroExpanded;
  }
  if (!result.deleteCount) {
    if (result.commitText == rawText) {
      // Preserve the application's native key path when Bamboo did not
      // transform the physical key.
      forwardOriginalKey(inputContext, originalKey);
      updateSurroundingCacheAfterCommit(inputContext, rawText);
      if (debugProvider_()) {
        FCITX_INFO() << "areca: apply unchanged no-delete by forward key="
                     << originalKey.toString() << " text=" << rawText;
      }
    } else {
      // Some output tables can transform a key without replacing an earlier
      // character. Unicode combining marks are the common example.
      if (!result.commitText.empty()) {
        inputContext.commitString(result.commitText);
        updateSurroundingCacheAfterCommit(inputContext, result.commitText);
      }
      if (debugProvider_()) {
        FCITX_INFO() << "areca: apply transformed no-delete by commit text="
                     << result.commitText << " raw=" << rawText;
      }
    }
    // Retain the settling barrier before the next key.
    finishKeyAfterCommit();
    return;
  }

  const auto timing = timingProvider_();
  RewritePlan plan;
  plan.transactionId = nextTransactionId_++;
  plan.backspaceDelayMs = timing.backspaceDelayMs;
  plan.afterBackspaceWaitMs = timing.afterBackspaceWaitMs;
  plan.commitText = result.commitText;
  plan.cacheDeleteCount = result.deleteCount;

  const auto selection = rewriteBackendSelector_(inputContext, result);
  if (!selection.backend) {
    FCITX_ERROR() << "areca: rewrite backend selector returned null";
    engine.reset();
    activeTransactionId_ = 0;
    finishKey();
    return;
  }
  auto &backend = *selection.backend;
  plan.backspaceCount =
      result.deleteCount + selection.additionalBackspaces;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: rewrite select backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " bamboo_delete=" << result.deleteCount
                 << " additional_backspaces="
                 << selection.additionalBackspaces
                 << " plan_backspaces=" << plan.backspaceCount;
  }

  activeTransactionId_ = plan.transactionId;
  const auto status =
      backend.apply(inputContext, plan, [this](uint64_t transactionId) {
        remoteDone(transactionId);
      });
  if (debugProvider_()) {
    FCITX_INFO() << "areca: rewrite apply backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " status=" << static_cast<int>(status);
  }
  if (status == ApplyStatus::Completed) {
    activeTransactionId_ = 0;
    finishKeyAfterCommit();
  } else if (status == ApplyStatus::Failed) {
    if (backend.recoverUnsentFailure()) {
      // Nothing reached the server, so no deletion can have happened. Preserve
      // usability by forwarding the literal key and dropping stale Bamboo
      // composition. A failure after PLAN was sent remains fail-closed.
      forwardOriginalKey(inputContext, originalKey);
      engine.reset();
      activeTransactionId_ = 0;
      finishKeyAfterCommit();
      if (debugProvider_()) {
        FCITX_INFO()
            << "areca: recovered unsent uinput failure by forwarding raw key tx="
            << plan.transactionId;
      }
      return;
    }
    // Fail closed. Retrying could duplicate a partially executed uinput
    // plan; committing or running the next key would violate ordering.
    stalled_ = true;
  }
}

void InputScheduler::forwardOriginalKey(fcitx::InputContext &inputContext,
                                        const fcitx::Key &key) {
  inputContext.forwardKey(key);
  inputContext.forwardKey(key, true);
}

void InputScheduler::remoteDone(uint64_t transactionId) {
  if (debugProvider_()) {
    FCITX_INFO() << "areca: remote DONE tx=" << transactionId
                 << " active=" << activeTransactionId_;
  }
  if (!processing_ || transactionId != activeTransactionId_) {
    return;
  }
  activeTransactionId_ = 0;
  // Uinput may take longer than keyIntervalMs. Start the settle delay from
  // the actual commit/DONE barrier, not from the earlier Bamboo call.
  finishKeyAfterCommit();
}

void InputScheduler::finishKey() {
  processing_ = false;
  scheduleNext();
}

void InputScheduler::finishKeyAfterCommit() {
  const uint64_t delayUsec =
      static_cast<uint64_t>(timingProvider_().postCommitDelayMs) * 1000;
  if (debugProvider_()) {
    FCITX_INFO() << "areca: post-commit barrier delay_us=" << delayUsec
                 << " queue_depth=" << queue_.size();
  }
  if (delayUsec == 0) {
    finishKey();
    return;
  }

  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC) + delayUsec;
  postCommitTimer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0,
      [this](fcitx::EventSourceTime *, uint64_t) {
        auto completedTimer = std::move(postCommitTimer_);
        finishKey();
        return false;
      });
  if (!postCommitTimer_) {
    finishKey();
    return;
  }
  postCommitTimer_->setOneShot();
}

} // namespace areca
