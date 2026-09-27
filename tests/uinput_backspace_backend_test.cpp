#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_backspace_backend.h"
#include "uinput_device.h"
#include "uinput_key_ack_tracker.h"

int main() {
  areca::UinputKeyAckTracker tracker;
  tracker.reset(2);
  assert(tracker.expectedPresses() == 3);
  for (uint32_t i = 0; i < 2; ++i) {
    assert(tracker.observePress() ==
           areca::UinputKeyAckTracker::PressAction::Forward);
    assert(!tracker.shouldFilterRelease());
  }
  assert(tracker.observePress() ==
         areca::UinputKeyAckTracker::PressAction::FilterAndAcknowledge);
  assert(tracker.shouldFilterRelease());

  fcitx::EventLoop eventLoop;
  areca::UinputDevice device([]() { return false; });
  areca::AdaptiveWait adaptiveWait;
  areca::UinputBackspaceBackend backend(eventLoop, device, adaptiveWait,
                                        []() { return false; });

  assert(std::string(backend.name()) == "uinput-backspace");
  assert(!backend.hasPending());

  bool available = backend.isAvailable();
  std::cout << "UinputBackspaceBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
