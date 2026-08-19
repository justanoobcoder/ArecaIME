#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx-utils/trackableobject.h>

#include "rewrite_backend.h"

namespace areca {

class SurroundingTextBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  SurroundingTextBackend(fcitx::EventLoop &eventLoop,
                         DebugProvider debugProvider);
  ~SurroundingTextBackend() override;

  const char *name() const override { return "surrounding-text"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }

private:
  void scheduleCommit();
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, std::function<void()> callback);
  void clearPending();

  fcitx::EventLoop &eventLoop_;
  DebugProvider debugProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t waitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;
};

} // namespace areca
