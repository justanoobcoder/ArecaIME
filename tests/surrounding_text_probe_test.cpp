#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "rewrite_backend.h"
#include "surrounding_text_backend.h"
#include "types.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  DummyInputContext(fcitx::InputContextManager &manager,
                    std::vector<std::string> &events,
                    std::string program = "test-app")
      : fcitx::InputContext(manager, std::move(program)), events_(events) {}
  ~DummyInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    events_.push_back("commit:" + text);
    committedText += text;
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    events_.push_back("deleteSurrounding:" + std::to_string(offset) + ":" +
                      std::to_string(count));
    if (onDelete) {
      onDelete();
    }
  }
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::string committedText;
  std::function<void()> onDelete;

private:
  std::vector<std::string> &events_;
};

class FakeShiftSelectBackend final : public areca::RewriteBackend {
public:
  explicit FakeShiftSelectBackend(std::vector<std::string> &events)
      : events_(events) {}

  const char *name() const override { return "fake-shift-select"; }

  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    events_.push_back("shiftSelectApply:tx=" +
                      std::to_string(plan.transactionId) +
                      ":commit=" + plan.commitText);
    inputContext.commitString(plan.commitText);
    if (onDone) {
      onDone(plan.transactionId, areca::RewriteOutcome::Succeeded);
    }
    return areca::ApplyStatus::Completed;
  }

private:
  std::vector<std::string> &events_;
};

class FakeBackspaceBackend final : public areca::RewriteBackend {
public:
  explicit FakeBackspaceBackend(std::vector<std::string> &events)
      : events_(events) {}

  const char *name() const override { return "fake-backspace"; }

  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    events_.push_back("backspaceApply:tx=" +
                      std::to_string(plan.transactionId) +
                      ":bs=" + std::to_string(plan.backspaceCount) +
                      ":commit=" + plan.commitText);
    inputContext.commitString(plan.commitText);
    if (onDone) {
      onDone(plan.transactionId, areca::RewriteOutcome::Succeeded);
    }
    return areca::ApplyStatus::Completed;
  }

private:
  std::vector<std::string> &events_;
};

areca::RewritePlan makePlan(uint64_t transactionId, uint32_t backspaceCount,
                            std::string commitText, bool probeSurrounding) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.surroundingWaitMs = 1;
  plan.commitText = std::move(commitText);
  plan.probeSurrounding = probeSurrounding;
  return plan;
}

// Probe deadline expires without receiving event -> fallback to shift-select for browser.
void testProbeFailureFallbackToShiftSelect() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events, "google-chrome");
  inputContext.surroundingText().setText("vietnam", 7, 7);

  FakeShiftSelectBackend fakeShift(events);
  FakeBackspaceBackend fakeBackspace(events);
  bool probeSucceededCalled = false;
  bool probeFailedCalled = false;
  bool doneCalled = false;

  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceededCalled = true; },
      [&](fcitx::InputContext &) {
        probeFailedCalled = true;
        events.push_back("probeFailed");
      },
      [&](fcitx::InputContext &ic) -> areca::RewriteBackend * {
        if (ic.program() == "google-chrome") {
          return &fakeShift;
        }
        return &fakeBackspace;
      });

  const auto status = backend.apply(
      inputContext, makePlan(101, 3, "trieu", true),
      [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
        assert(transactionId == 101);
        assert(outcome == areca::RewriteOutcome::Succeeded);
        events.push_back("done");
        doneCalled = true;
        eventLoop.exit();
      });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());

  // Không gửi event -> deadline 150ms cháy -> fallback sang shift-select.
  eventLoop.exec();

  assert(!probeSucceededCalled);
  assert(probeFailedCalled);
  assert(doneCalled);
  assert(inputContext.committedText == "trieu");
}

// Probe deadline expires without receiving event -> fallback to backspace for non-browser.
void testProbeFailureFallbackToBackspace() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events, "code");
  inputContext.surroundingText().setText("vietnam", 7, 7);

  FakeShiftSelectBackend fakeShift(events);
  FakeBackspaceBackend fakeBackspace(events);
  bool probeSucceededCalled = false;
  bool probeFailedCalled = false;
  bool doneCalled = false;

  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceededCalled = true; },
      [&](fcitx::InputContext &) {
        probeFailedCalled = true;
        events.push_back("probeFailed");
      },
      [&](fcitx::InputContext &ic) -> areca::RewriteBackend * {
        if (ic.program() == "google-chrome") {
          return &fakeShift;
        }
        return &fakeBackspace;
      });

  const auto status = backend.apply(
      inputContext, makePlan(102, 3, "trieu", true),
      [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
        assert(transactionId == 102);
        assert(outcome == areca::RewriteOutcome::Succeeded);
        events.push_back("done");
        doneCalled = true;
        eventLoop.exit();
      });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());

  // Không gửi event -> deadline 150ms cháy -> fallback sang backspace.
  eventLoop.exec();

  assert(!probeSucceededCalled);
  assert(probeFailedCalled);
  assert(doneCalled);
  assert(inputContext.committedText == "trieu");
}

// SurroundingTextUpdated event arrives before deadline -> probe succeeds.
void testProbeSuccessViaEvent() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  inputContext.surroundingText().setText("vietnam", 7, 7);

  FakeShiftSelectBackend fakeShift(events);
  bool probeSucceededCalled = false;
  bool probeFailedCalled = false;
  bool doneCalled = false;

  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) {
        probeSucceededCalled = true;
        events.push_back("probeSucceeded");
      },
      [&](fcitx::InputContext &) { probeFailedCalled = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fakeShift;
      });

  const auto status = backend.apply(
      inputContext, makePlan(103, 3, "nammoi", true),
      [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
        assert(transactionId == 103);
        assert(outcome == areca::RewriteOutcome::Succeeded);
        events.push_back("done");
        doneCalled = true;
        eventLoop.exit();
      });

  assert(status == areca::ApplyStatus::Pending);
  assert(backend.hasPending());
  assert(backend.isProbing());

  // Event không phản ánh việc xoá (như LibreOffice có thể gửi) không được coi
  // là probe thành công.
  backend.notifySurroundingTextUpdated(inputContext);
  assert(backend.isProbing());
  assert(!probeSucceededCalled);

  // Giả lập client xác nhận đúng trạng thái sau khi xoá 3 ký tự.
  inputContext.surroundingText().setText("viet", 4, 4);
  backend.notifySurroundingTextUpdated(inputContext);
  assert(!backend.isProbing());

  eventLoop.exec();

  assert(probeSucceededCalled);
  assert(!probeFailedCalled);
  assert(doneCalled);
  assert(inputContext.committedText == "nammoi");
  assert(inputContext.surroundingText().text() == "vietnammoi");
}

// Frontend may publish the confirmation reentrantly from deleteSurroundingText.
void testProbeSuccessViaSynchronousEvent() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.setCapabilityFlags(fcitx::CapabilityFlag::SurroundingText);
  inputContext.surroundingText().setText("vietnam", 7, 7);

  FakeBackspaceBackend fallback(events);
  bool probeSucceededCalled = false;
  bool probeFailedCalled = false;
  bool doneCalled = false;

  areca::SurroundingTextBackend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceededCalled = true; },
      [&](fcitx::InputContext &) { probeFailedCalled = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });

  inputContext.onDelete = [&]() {
    inputContext.surroundingText().setText("viet", 4, 4);
    backend.notifySurroundingTextUpdated(inputContext);
  };

  const auto status = backend.apply(
      inputContext, makePlan(104, 3, "nam", true),
      [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
        assert(transactionId == 104);
        assert(outcome == areca::RewriteOutcome::Succeeded);
        doneCalled = true;
        eventLoop.exit();
      });

  assert(status == areca::ApplyStatus::Pending);
  assert(!backend.isProbing());
  eventLoop.exec();

  assert(probeSucceededCalled);
  assert(!probeFailedCalled);
  assert(doneCalled);
  assert(inputContext.committedText == "nam");
  assert(inputContext.surroundingText().text() == "vietnam");
}

} // namespace

int main() {
  testProbeFailureFallbackToShiftSelect();
  testProbeFailureFallbackToBackspace();
  testProbeSuccessViaEvent();
  testProbeSuccessViaSynchronousEvent();
  std::cout << "All surrounding text probe tests passed\n";
  return 0;
}
