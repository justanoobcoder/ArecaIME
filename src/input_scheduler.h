#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "bamboo_engine_adapter.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
  uint32_t keyIntervalMs = 20;
  uint32_t backspaceDelayMs = 5;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t postCommitDelayMs = 20;
};

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
  uint32_t additionalBackspaces = 0;
};

class InputScheduler {
public:
  using EngineResolver =
      std::function<VietnameseEngine *(fcitx::InputContext &)>;
  using TimingProvider = std::function<SchedulerTiming()>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector = std::function<RewriteBackendSelection(
      fcitx::InputContext &, const BambooResult &)>;

  InputScheduler(fcitx::EventLoop &eventLoop, EngineResolver engineResolver,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);

  void enqueue(fcitx::InputContext &inputContext,
               const fcitx::Key &originalKey, uint32_t codepoint,
               std::string utf8Text);
  void resetContext(fcitx::InputContext &inputContext);

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }

private:
  void scheduleNext();
  void processNext(uint64_t nowUsec);
  void applyResult(fcitx::InputContext &inputContext, VietnameseEngine &engine,
                   const BambooResult &result, const std::string &rawText,
                   const fcitx::Key &originalKey);
  void forwardOriginalKey(fcitx::InputContext &inputContext,
                          const fcitx::Key &key);
  void finishKey();
  void finishKeyAfterCommit();
  void remoteDone(uint64_t transactionId);

  fcitx::EventLoop &eventLoop_;
  EngineResolver engineResolver_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  KeyQueue queue_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  std::unique_ptr<fcitx::EventSourceTime> postCommitTimer_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t lastProcessedAtUsec_ = 0;
  uint64_t activeTransactionId_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
