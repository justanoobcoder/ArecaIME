#include "adaptive_wait_monitor.h"

#include <utility>

#include <fcitx-utils/log.h>

namespace areca {

AdaptiveWaitMonitor::AdaptiveWaitMonitor(fcitx::EventLoop &eventLoop,
                                         AdaptiveWait &adaptiveWait,
                                         DebugProvider debugProvider)
    : eventLoop_(eventLoop), adaptiveWait_(adaptiveWait),
      debugProvider_(std::move(debugProvider)) {
  // Dùng deadline tuyệt đối để đo phần callback bị chạy muộn, thay vì đo thời
  // gian thực thi của callback.
  deadlineUsec_ = fcitx::now(CLOCK_MONOTONIC) + ProbeIntervalUsec;
  timer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadlineUsec_, TimerAccuracyUsec,
      [this](fcitx::EventSourceTime *source, uint64_t) {
        const uint64_t firedAtUsec = fcitx::now(CLOCK_MONOTONIC);
        const auto adjustment =
            adaptiveWait_.observeSystemTimer(deadlineUsec_, firedAtUsec);
        if (debugProvider_() &&
            adjustment == AdaptiveWait::Adjustment::Increased) {
          FCITX_INFO() << "areca: system lag detected lateness_us="
                       << (firedAtUsec > deadlineUsec_
                               ? firedAtUsec - deadlineUsec_
                               : 0)
                       << " adaptive_extra_ms=" << adaptiveWait_.extraWaitMs();
        }
        // Lấy thời điểm hiện tại làm mốc mới để một lần stall dài chỉ tạo một
        // mẫu lag, không tạo chuỗi callback chạy bù liên tiếp.
        deadlineUsec_ = firedAtUsec + ProbeIntervalUsec;
        source->setTime(deadlineUsec_);
        return true;
      });
}

} // namespace areca
