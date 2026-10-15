#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "event_loop_post.h"
#include "rewrite_backend.h"

namespace areca {

class SurroundingTextBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;
  using ProbeSucceededHandler = std::function<void(fcitx::InputContext &)>;
  using ProbeFailedHandler = std::function<void(fcitx::InputContext &)>;
  using FallbackBackendProvider =
      std::function<RewriteBackend *(fcitx::InputContext &)>;

  SurroundingTextBackend(
      fcitx::EventLoop &eventLoop, DebugProvider debugProvider,
      ProbeSucceededHandler onProbeSucceeded = nullptr,
      ProbeFailedHandler onProbeFailed = nullptr,
      FallbackBackendProvider fallbackBackendProvider = nullptr);
  ~SurroundingTextBackend() override;

  const char *name() const override { return "surrounding-text"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }
  bool isProbing() const { return isProbe_; }

  // Gọi từ ArecaEngine khi nhận InputContextSurroundingTextUpdated trong lúc probe.
  void notifySurroundingTextUpdated(fcitx::InputContext &inputContext);

private:
  void scheduleCommit();
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, std::function<void()> callback);
  void scheduleProbeDeadline();
  void handleProbeFailure();
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  EventLoopPostTask commitPost_;
  DebugProvider debugProvider_;
  ProbeSucceededHandler onProbeSucceeded_;
  ProbeFailedHandler onProbeFailed_;
  FallbackBackendProvider fallbackBackendProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t waitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;

  bool isProbe_ = false;
  RewritePlan savedPlan_;
  std::string expectedProbeText_;
  unsigned int expectedProbeCursor_ = 0;
  unsigned int expectedProbeAnchor_ = 0;
};

} // namespace areca
