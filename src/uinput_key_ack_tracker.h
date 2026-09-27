#pragma once

#include <cstdint>

namespace areca {

// Đếm một loại phím quay lại Fcitx sau khi được phát qua uinput. N lần đầu
// được forward để thực hiện thao tác thật; lần N+1 chỉ xác nhận hàng đợi đã đi
// hết nên phải bị lọc.
class UinputKeyAckTracker {
public:
  enum class PressAction {
    Forward,
    Filter,
    FilterAndAcknowledge,
  };

  void reset(uint32_t forwardedPresses) {
    forwardedPresses_ = forwardedPresses;
    expectedPresses_ = forwardedPresses + 1;
    pressesSeen_ = 0;
    releasesSeen_ = 0;
    completionSignaled_ = false;
  }

  void clear() {
    forwardedPresses_ = 0;
    expectedPresses_ = 0;
    pressesSeen_ = 0;
    releasesSeen_ = 0;
    completionSignaled_ = false;
  }

  PressAction observePress() {
    if (pressesSeen_ < expectedPresses_) {
      ++pressesSeen_;
    }
    if (pressesSeen_ <= forwardedPresses_) {
      return PressAction::Forward;
    }
    if (!completionSignaled_ && pressesSeen_ == expectedPresses_) {
      completionSignaled_ = true;
      return PressAction::FilterAndAcknowledge;
    }
    return PressAction::Filter;
  }

  bool shouldFilterRelease() {
    if (releasesSeen_ < expectedPresses_) {
      ++releasesSeen_;
    }
    return releasesSeen_ > forwardedPresses_;
  }

  uint32_t expectedPresses() const { return expectedPresses_; }
  uint32_t pressesSeen() const { return pressesSeen_; }
  uint32_t releasesSeen() const { return releasesSeen_; }

private:
  uint32_t forwardedPresses_ = 0;
  uint32_t expectedPresses_ = 0;
  uint32_t pressesSeen_ = 0;
  uint32_t releasesSeen_ = 0;
  bool completionSignaled_ = false;
};

} // namespace areca
