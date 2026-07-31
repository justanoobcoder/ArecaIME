#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <fcitx-utils/event.h>

#include "pending_rewrite_state.h"
#include "rewrite_backend.h"

namespace areca {

class UinputSocketBackend final : public RewriteBackend {
public:
  enum class InjectedBackspaceAction { NotPending, PassToApplication, Filter };

  UinputSocketBackend(fcitx::EventLoop &eventLoop, std::string socketPath);
  ~UinputSocketBackend() override;

  const char *name() const override { return "uinput-socket"; }
  ApplyStatus apply(fcitx::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  void setSocketPath(std::string socketPath);
  void setDebug(bool debug) { debug_ = debug; }
  bool hasPending() const { return pending_.active(); }
  const PendingRewriteState &pending() const { return pending_; }
  bool recoverUnsentFailure() override;
  InjectedBackspaceAction handleInjectedBackspacePress();
  bool handleInjectedBackspaceRelease();

private:
  bool connectSocket();
  void closeSocket(bool failed);
  bool flushOutput();
  bool handleIO(fcitx::EventSourceIO *, int, fcitx::IOEventFlags);
  bool readInput();
  void handleLine(const std::string &line);
  void maybeCompletePending();
  void markTransportFailure();
  void updateEventFlags();

  fcitx::EventLoop &eventLoop_;
  std::string socketPath_;
  int fd_ = -1;
  bool connecting_ = false;
  uint64_t sessionId_ = 0;
  std::string outputBuffer_;
  std::string inputBuffer_;
  std::unique_ptr<fcitx::EventSourceIO> ioEvent_;
  PendingRewriteState pending_;
  RewriteDone onDone_;
  bool debug_ = false;
  bool filterSentinelRelease_ = false;
};

} // namespace areca
