#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "surrounding_text_v2_backend.h"
#include "types.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  DummyInputContext(fcitx::InputContextManager &manager,
                    std::vector<std::string> &events)
      : fcitx::InputContext(manager, "test-app"), events_(events) {}
  ~DummyInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }
  void commitStringImpl(const std::string &text) override {
    events_.push_back("commit:" + text);
  }
  void deleteSurroundingTextImpl(int offset, unsigned int count) override {
    events_.push_back("delete:" + std::to_string(offset) + ":" +
                      std::to_string(count));
    if (onDelete) {
      onDelete();
    }
  }
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::function<void()> onDelete;

private:
  std::vector<std::string> &events_;
};

class FakeFallbackBackend final : public areca::RewriteBackend {
public:
  explicit FakeFallbackBackend(bool &called) : called_(called) {}

  const char *name() const override { return "fake-fallback"; }
  areca::ApplyStatus apply(fcitx::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    called_ = true;
    inputContext.commitString(plan.commitText);
    if (onDone) {
      onDone(plan.transactionId, areca::RewriteOutcome::Succeeded);
    }
    return areca::ApplyStatus::Completed;
  }

private:
  bool &called_;
};

areca::RewritePlan makeProbePlan(uint64_t transactionId,
                                 uint32_t backspaceCount = 1) {
  areca::RewritePlan plan;
  plan.transactionId = transactionId;
  plan.backspaceCount = backspaceCount;
  plan.surroundingDeleteDelayMs = 0;
  plan.waylandSurroundingDeleteDelayMs = 0;
  plan.afterSurroundingDeleteWaitMs = 1;
  plan.commitText = "õ";
  plan.probeSurrounding = true;
  return plan;
}

void testProbeSuccess() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool probeSucceeded = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceeded = true; }, nullptr,
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });
  inputContext.onDelete = [&]() {
    inputContext.surroundingText().setText("g", 1, 1);
    backend.notifySurroundingTextUpdated(inputContext);
  };

  const auto status =
      backend.apply(inputContext, makeProbePlan(301),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 301);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeSucceeded);
  assert(!fallbackCalled);
  assert(doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1", "commit:õ"}));
}

void testProbeFailureFallsBack() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("go", 2, 2);

  bool probeFailed = false;
  bool fallbackCalled = false;
  bool doneCalled = false;
  FakeFallbackBackend fallback(fallbackCalled);
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; }, nullptr,
      [&](fcitx::InputContext &) { probeFailed = true; },
      [&](fcitx::InputContext &) -> areca::RewriteBackend * {
        return &fallback;
      });

  const auto status =
      backend.apply(inputContext, makeProbePlan(302),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 302);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeFailed);
  assert(fallbackCalled);
  assert(doneCalled);
  assert(events == std::vector<std::string>({"delete:-1:1", "commit:õ"}));
}

void testProbeIgnoresIntermediateUpdates() {
  fcitx::EventLoop eventLoop;
  fcitx::InputContextManager manager;
  std::vector<std::string> events;
  DummyInputContext inputContext(manager, events);
  inputContext.surroundingText().setText("goo", 3, 3);

  bool probeSucceeded = false;
  bool doneCalled = false;
  unsigned int deletes = 0;
  areca::SurroundingTextV2Backend backend(
      eventLoop, []() { return false; },
      [&](fcitx::InputContext &) { probeSucceeded = true; });
  inputContext.onDelete = [&]() {
    ++deletes;
    inputContext.surroundingText().setText(deletes == 1 ? "go" : "g",
                                           3 - deletes, 3 - deletes);
    backend.notifySurroundingTextUpdated(inputContext);
    if (deletes == 1) {
      assert(backend.isProbing());
      assert(!probeSucceeded);
    }
  };

  const auto status =
      backend.apply(inputContext, makeProbePlan(303, 2),
                    [&](uint64_t transactionId, areca::RewriteOutcome outcome) {
                      assert(transactionId == 303);
                      assert(outcome == areca::RewriteOutcome::Succeeded);
                      doneCalled = true;
                      eventLoop.exit();
                    });
  assert(status == areca::ApplyStatus::Pending);
  eventLoop.exec();

  assert(probeSucceeded);
  assert(doneCalled);
  assert(events ==
         std::vector<std::string>({"delete:-1:1", "delete:-1:1", "commit:õ"}));
}

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  areca::SurroundingTextV2Backend backend(eventLoop, []() { return false; });

  assert(std::string(backend.name()) == "surrounding-text-v2");
  assert(!backend.hasPending());

  areca::RewritePlan plan;
  assert(plan.surroundingDeleteDelayMs == 10);
  assert(plan.waylandSurroundingDeleteDelayMs == 0);
  assert(plan.afterSurroundingDeleteWaitMs == 1);

  areca::AdaptiveWait adaptiveWait;
  areca::SurroundingTextV2Backend backendWithAdaptive(
      eventLoop, []() { return false; }, nullptr, nullptr, nullptr,
      &adaptiveWait);
  assert(std::string(backendWithAdaptive.name()) == "surrounding-text-v2");

  testProbeSuccess();
  testProbeFailureFallsBack();
  testProbeIgnoresIntermediateUpdates();

  std::cout << "SurroundingTextV2Backend test passed\n";
  return 0;
}
