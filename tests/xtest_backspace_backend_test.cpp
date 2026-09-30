#include <cassert>
#include <iostream>
#include <string>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "adaptive_wait.h"
#include "xtest_backspace_backend.h"
#include "xtest_device.h"

namespace {

class DummyInputContext final : public fcitx::InputContext {
public:
  explicit DummyInputContext(fcitx::InputContextManager &manager)
      : fcitx::InputContext(manager, "xterm") {}
  ~DummyInputContext() override { destroy(); }

  const char *frontend() const override { return "xim"; }
  void commitStringImpl(const std::string &text) override {
    committedText += text;
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const fcitx::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::string committedText;
};

} // namespace

int main() {
  fcitx::EventLoop eventLoop;
  areca::XTestDevice device([]() { return false; });
  areca::AdaptiveWait adaptiveWait;
  areca::XTestBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                       []() { return false; });

  assert(std::string(backend.name()) == "xtest-backspace");
  assert(!backend.hasPending());

  const bool available = backend.isAvailable();
  std::cout << "XTestBackspaceBackend availability: "
            << (available ? "true" : "false") << "\n";

  fcitx::InputContextManager manager;
  DummyInputContext ic(manager);

  if (available) {
    areca::RewritePlan plan;
    plan.transactionId = 42;
    plan.backspaceCount = 1;
    plan.xtestBackspaceDelayMs = 0;
    plan.afterXTestBackspaceWaitMs = 0;
    plan.commitText = "test";

    bool doneCalled = false;
    uint64_t doneTx = 0;
    auto status = backend.apply(ic, plan, [&](uint64_t tx) {
      doneCalled = true;
      doneTx = tx;
      eventLoop.exit();
    });

    assert(status == areca::ApplyStatus::Pending);
    assert(backend.hasPending());

    eventLoop.exec();

    assert(doneCalled);
    assert(doneTx == 42);
    assert(!backend.hasPending());
    assert(ic.committedText == "test");
  } else {
    areca::RewritePlan plan;
    plan.transactionId = 100;
    auto status = backend.apply(ic, plan, [](uint64_t) {});
    assert(status == areca::ApplyStatus::Failed);
  }

  std::cout << "XTestBackspaceBackend test passed successfully\n";
  return 0;
}
