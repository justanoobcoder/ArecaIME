#include "uinput_socket_backend.h"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <fcitx-utils/log.h>

#include "surrounding_text_cache.h"

namespace areca {
namespace {

uint64_t makeSessionId() {
  const uint64_t pid = static_cast<uint64_t>(::getpid());
  return (pid << 32U) ^ fcitx::now(CLOCK_MONOTONIC);
}

} // namespace

UinputSocketBackend::UinputSocketBackend(fcitx::EventLoop &eventLoop,
                                         std::string socketPath)
    : eventLoop_(eventLoop), socketPath_(std::move(socketPath)),
      sessionId_(makeSessionId()) {}

UinputSocketBackend::~UinputSocketBackend() { closeSocket(false); }

ApplyStatus UinputSocketBackend::apply(fcitx::InputContext &inputContext,
                                       const RewritePlan &plan,
                                       RewriteDone onDone) {
  if (pending_.active()) {
    return ApplyStatus::Failed;
  }

  pending_.transactionId = plan.transactionId;
  pending_.inputContext = inputContext.watch();
  pending_.commitText = plan.commitText;
  pending_.cacheDeleteCount = plan.cacheDeleteCount;
  pending_.expectedBackspaceEvents = plan.backspaceCount + 1;
  pending_.seenBackspaceEvents = 0;
  pending_.ackFullWaitUsec =
      static_cast<uint64_t>(plan.ackFullWaitMs) * 1000;
  onDone_ = std::move(onDone);

  const auto delayUsec = static_cast<uint64_t>(plan.backspaceDelayMs) * 1000;
  const auto waitUsec = static_cast<uint64_t>(plan.afterBackspaceWaitMs) * 1000;
  outputBuffer_ = "PLAN " + std::to_string(sessionId_) + " " +
                  std::to_string(plan.transactionId) + " " +
                  std::to_string(plan.backspaceCount) + " " +
                  std::to_string(delayUsec) + " " + std::to_string(waitUsec) +
                  "\n";
  if (debug_) {
    FCITX_INFO() << "areca: uinput prepare " << outputBuffer_;
  }

  if (fd_ < 0 && !connectSocket()) {
    markTransportFailure();
    return ApplyStatus::Failed;
  }
  if (!connecting_ && !flushOutput()) {
    markTransportFailure();
    return ApplyStatus::Failed;
  }
  updateEventFlags();
  return ApplyStatus::Pending;
}

void UinputSocketBackend::setSocketPath(std::string socketPath) {
  if (socketPath == socketPath_ || pending_.active()) {
    return;
  }
  closeSocket(false);
  socketPath_ = std::move(socketPath);
}

bool UinputSocketBackend::recoverUnsentFailure() {
  if (!pending_.active() || pending_.planSent) {
    return false;
  }
  // A partial, unterminated PLAN must never remain on a reusable connection.
  closeSocket(false);
  pending_.clear();
  onDone_ = {};
  if (debug_) {
    FCITX_INFO() << "areca: uinput abandoned unsent transaction safely";
  }
  return true;
}

UinputSocketBackend::InjectedBackspaceAction
UinputSocketBackend::handleInjectedBackspacePress() {
  if (!pending_.active()) {
    return InjectedBackspaceAction::NotPending;
  }
  ++pending_.seenBackspaceEvents;
  const bool ackFull =
      pending_.seenBackspaceEvents == pending_.expectedBackspaceEvents;
  const bool sentinel =
      pending_.seenBackspaceEvents >= pending_.expectedBackspaceEvents;
  if (debug_) {
    FCITX_INFO() << "areca: uinput Backspace ack seen="
                 << pending_.seenBackspaceEvents
                 << " expected=" << pending_.expectedBackspaceEvents
                 << " ack_full=" << ackFull
                 << " sentinel=" << sentinel;
  }
  if (sentinel) {
    filterSentinelRelease_ = true;
    if (ackFull) {
      sendWaitAfterAck();
    }
    maybeCompletePending();
    return InjectedBackspaceAction::Filter;
  }
  return InjectedBackspaceAction::PassToApplication;
}

bool UinputSocketBackend::handleInjectedBackspaceRelease() {
  if (!filterSentinelRelease_) {
    return false;
  }
  filterSentinelRelease_ = false;
  if (debug_) {
    FCITX_INFO() << "areca: filtered uinput sentinel Backspace release";
  }
  return true;
}

bool UinputSocketBackend::sendWaitAfterAck() {
  if (!pending_.active() || pending_.serverDone) {
    return true;
  }

  const std::string waitLine =
      "WAIT " + std::to_string(sessionId_) + " " +
      std::to_string(pending_.transactionId) + " " +
      std::to_string(pending_.ackFullWaitUsec) + "\n";
  outputBuffer_.append(waitLine);
  if (debug_) {
    FCITX_INFO() << "areca: uinput ACK full, schedule " << waitLine;
  }

  if (fd_ < 0 || connecting_) {
    updateEventFlags();
    return fd_ >= 0;
  }
  if (!flushOutput()) {
    closeSocket(true);
    return false;
  }
  updateEventFlags();
  return true;
}

bool UinputSocketBackend::connectSocket() {
  if (socketPath_.empty() ||
      socketPath_.size() >= sizeof(sockaddr_un::sun_path)) {
    FCITX_ERROR() << "areca: invalid uinput socket path=" << socketPath_;
    return false;
  }
  fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd_ < 0) {
    FCITX_ERROR() << "areca: socket() failed errno=" << errno << " "
                  << std::strerror(errno);
    return false;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, socketPath_.c_str(), socketPath_.size() + 1);
  if (::connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
      0) {
    connecting_ = false;
  } else if (errno == EINPROGRESS) {
    connecting_ = true;
  } else {
    FCITX_ERROR() << "areca: connect failed path=" << socketPath_
                  << " errno=" << errno << " " << std::strerror(errno);
    closeSocket(false);
    return false;
  }

  ioEvent_ = eventLoop_.addIOEvent(
      fd_,
      fcitx::IOEventFlags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Out,
                          fcitx::IOEventFlag::Err, fcitx::IOEventFlag::Hup},
      [this](fcitx::EventSourceIO *source, int fd, fcitx::IOEventFlags flags) {
        return handleIO(source, fd, flags);
      });
  if (debug_) {
    FCITX_INFO() << "areca: uinput socket fd=" << fd_
                 << " connecting=" << connecting_ << " path=" << socketPath_;
  }
  return true;
}

void UinputSocketBackend::closeSocket(bool failed) {
  ioEvent_.reset();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  connecting_ = false;
  inputBuffer_.clear();
  outputBuffer_.clear();
  if (failed) {
    markTransportFailure();
  }
}

bool UinputSocketBackend::handleIO(fcitx::EventSourceIO *, int,
                                   fcitx::IOEventFlags flags) {
  if (flags.test(fcitx::IOEventFlag::Err) ||
      flags.test(fcitx::IOEventFlag::Hup)) {
    FCITX_ERROR() << "areca: uinput socket error/hup flags="
                  << flags.toInteger();
    closeSocket(true);
    return false;
  }

  if (connecting_ && flags.test(fcitx::IOEventFlag::Out)) {
    int error = 0;
    socklen_t length = sizeof(error);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &length) != 0 ||
        error != 0) {
      FCITX_ERROR() << "areca: async connect failed error=" << error << " "
                    << std::strerror(error);
      closeSocket(true);
      return false;
    }
    connecting_ = false;
  }
  if (!connecting_ && !outputBuffer_.empty() && !flushOutput()) {
    closeSocket(true);
    return false;
  }
  if (flags.test(fcitx::IOEventFlag::In) && !readInput()) {
    closeSocket(true);
    return false;
  }
  updateEventFlags();
  return true;
}

bool UinputSocketBackend::flushOutput() {
  while (!outputBuffer_.empty()) {
    const auto written =
        ::send(fd_, outputBuffer_.data(), outputBuffer_.size(), MSG_NOSIGNAL);
    if (written > 0) {
      if (debug_) {
        FCITX_INFO() << "areca: uinput socket wrote bytes=" << written;
      }
      outputBuffer_.erase(0, static_cast<size_t>(written));
      const bool planWasSent = pending_.planSent;
      pending_.planSent = outputBuffer_.empty();
      if (!planWasSent && pending_.planSent &&
          !pending_.cacheDeleteApplied) {
        if (auto *inputContext = pending_.inputContext.get()) {
          updateSurroundingCacheAfterDelete(
              *inputContext, -static_cast<int>(pending_.cacheDeleteCount),
              pending_.cacheDeleteCount);
        }
        pending_.cacheDeleteApplied = true;
        if (debug_) {
          FCITX_INFO() << "areca: uinput cache delete="
                       << pending_.cacheDeleteCount
                       << " tx=" << pending_.transactionId;
        }
      }
      continue;
    }
    FCITX_ERROR() << "areca: send PLAN failed errno=" << errno << " "
                  << std::strerror(errno);
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    return false;
  }
  return true;
}

bool UinputSocketBackend::readInput() {
  char buffer[4096];
  for (;;) {
    const auto count = ::recv(fd_, buffer, sizeof(buffer), 0);
    if (count > 0) {
      if (debug_) {
        FCITX_INFO() << "areca: uinput socket read bytes=" << count;
      }
      inputBuffer_.append(buffer, static_cast<size_t>(count));
    } else if (count == 0) {
      return false;
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else {
      FCITX_ERROR() << "areca: recv failed errno=" << errno << " "
                    << std::strerror(errno);
      return false;
    }
  }

  size_t newline = 0;
  while ((newline = inputBuffer_.find('\n')) != std::string::npos) {
    auto line = inputBuffer_.substr(0, newline);
    inputBuffer_.erase(0, newline + 1);
    handleLine(line);
  }
  return true;
}

void UinputSocketBackend::handleLine(const std::string &line) {
  if (debug_) {
    FCITX_INFO() << "areca: uinput recv line=" << line;
  }
  std::istringstream stream(line);
  std::string opcode;
  uint64_t session = 0;
  uint64_t transaction = 0;
  if (!(stream >> opcode >> session >> transaction) || opcode != "DONE" ||
      session != sessionId_ || !pending_.active() ||
      transaction != pending_.transactionId) {
    FCITX_WARN() << "areca: ignored invalid uinput response: " << line;
    return;
  }

  // DONE is authoritative. Some Wayland compositors route uinput events
  // directly to the client, so those events never return through this input
  // method and cannot be counted here. If DONE wins the race, stop waiting for
  // optional key-event acknowledgements so the pipeline cannot deadlock.
  pending_.serverDone = true;
  if (debug_) {
    FCITX_INFO() << "areca: uinput DONE observed tx=" << transaction
                 << " seen_backspaces=" << pending_.seenBackspaceEvents
                 << " expected=" << pending_.expectedBackspaceEvents
                 << " cancel_ack_wait="
                 << (pending_.seenBackspaceEvents <
                     pending_.expectedBackspaceEvents);
  }
  maybeCompletePending();
}

void UinputSocketBackend::maybeCompletePending() {
  if (!pending_.active() || !pending_.serverDone) {
    return;
  }

  const uint64_t transaction = pending_.transactionId;
  if (auto *inputContext = pending_.inputContext.get()) {
    if (!pending_.commitText.empty()) {
      inputContext->commitString(pending_.commitText);
      updateSurroundingCacheAfterCommit(*inputContext, pending_.commitText);
    }
  }
  auto callback = std::move(onDone_);
  if (debug_) {
    FCITX_INFO() << "areca: uinput barriers complete tx=" << transaction
                 << " seen_backspaces=" << pending_.seenBackspaceEvents
                 << " expected=" << pending_.expectedBackspaceEvents;
  }
  pending_.clear();
  if (callback) {
    callback(transaction);
  }
}

void UinputSocketBackend::markTransportFailure() {
  if (pending_.active()) {
    pending_.transportFailed = true;
    FCITX_ERROR()
        << "areca: uinput transaction failed; input pipeline is paused, tx="
        << pending_.transactionId;
  }
}

void UinputSocketBackend::updateEventFlags() {
  if (!ioEvent_) {
    return;
  }
  fcitx::IOEventFlags flags{fcitx::IOEventFlag::In, fcitx::IOEventFlag::Err,
                            fcitx::IOEventFlag::Hup};
  if (connecting_ || !outputBuffer_.empty()) {
    flags |= fcitx::IOEventFlag::Out;
  }
  ioEvent_->setEvents(flags);
}

} // namespace areca
