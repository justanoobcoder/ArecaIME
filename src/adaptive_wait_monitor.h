#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <fcitx-utils/event.h>

#include "adaptive_wait.h"

namespace areca {

// Đồng hồ thăm dò độ trễ chung của event loop Fcitx. Lớp này chỉ cập nhật
// AdaptiveWait; chỉ hai backend Backspace đọc và áp dụng mức wait đó.
class AdaptiveWaitMonitor {
public:
  using DebugProvider = std::function<bool()>;

  // Kiểm tra mỗi 10 ms. Callback trễ ít nhất 5 ms được xem là dấu hiệu toàn hệ
  // thống đang stall, không phụ thuộc ứng dụng đích đang dùng backend nào.
  static constexpr uint64_t ProbeIntervalUsec = 10'000;
  static constexpr uint64_t TimerAccuracyUsec = 1'000;

  AdaptiveWaitMonitor(fcitx::EventLoop &eventLoop, AdaptiveWait &adaptiveWait,
                      DebugProvider debugProvider);

private:
  fcitx::EventLoop &eventLoop_;
  AdaptiveWait &adaptiveWait_;
  DebugProvider debugProvider_;
  std::unique_ptr<fcitx::EventSourceTime> timer_;
  uint64_t deadlineUsec_ = 0;
};

} // namespace areca
