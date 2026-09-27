#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_device.h"
#include "uinput_key_ack_tracker.h"
#include "uinput_shift_select_backend.h"

int main() {
  areca::UinputKeyAckTracker tracker;
  tracker.reset(3);
  assert(tracker.expectedPresses() == 4);

  // Ba Left đầu được đưa tới ứng dụng để chọn đúng ba ký tự.
  for (uint32_t i = 0; i < 3; ++i) {
    assert(tracker.observePress() ==
           areca::UinputKeyAckTracker::PressAction::Forward);
    assert(!tracker.shouldFilterRelease());
  }

  // Left thứ tư chỉ xác nhận hàng đợi đã đi hết. Cả press và release của nó
  // đều bị lọc; press là thời điểm backend được phép thả Shift.
  assert(tracker.observePress() ==
         areca::UinputKeyAckTracker::PressAction::FilterAndAcknowledge);
  assert(tracker.shouldFilterRelease());
  assert(tracker.pressesSeen() == 4);
  assert(tracker.releasesSeen() == 4);

  // Event lặp ngoài dự kiến cũng không được lọt tới ứng dụng hoặc thả Shift
  // lần thứ hai.
  assert(tracker.observePress() ==
         areca::UinputKeyAckTracker::PressAction::Filter);
  assert(tracker.shouldFilterRelease());

  fcitx::EventLoop eventLoop;
  areca::UinputDevice device([]() { return false; });
  areca::UinputShiftSelectBackend backend(eventLoop, device,
                                          []() { return false; });

  assert(std::string(backend.name()) == "uinput-shift-select");
  assert(!backend.hasPending());

  bool available = backend.isAvailable();
  std::cout << "UinputShiftSelectBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
