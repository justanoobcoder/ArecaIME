#pragma once

#include <algorithm>
#include <cstdint>

namespace areca {

class AdaptiveWait {
public:
  enum class Adjustment { None, Increased, Decreased };

  static constexpr uint32_t StepMs = 10;
  static constexpr uint32_t MaxWaitMs = 50;
  static constexpr uint64_t LagThresholdUsec = 5000;
  static constexpr uint32_t StableTransactionsToDecay = 5;

  // Đánh dấu một lượt xóa mới. Nếu probe toàn cục phát hiện lag trong lúc lượt
  // này đang chạy thì mức wait không được giảm khi commit hoàn tất.
  void beginTransaction() {
    transactionActive_ = true;
    lagObservedInTransaction_ = false;
  }

  // Hủy trạng thái transaction khi input context biến mất giữa chừng.
  void cancelTransaction() {
    transactionActive_ = false;
    lagObservedInTransaction_ = false;
  }

  uint32_t effectiveWaitMs(uint32_t baseWaitMs) const {
    if (baseWaitMs >= MaxWaitMs) {
      return baseWaitMs;
    }
    return std::min(MaxWaitMs, baseWaitMs + extraWaitMs_);
  }

  uint32_t effectiveExtraWaitMs(uint32_t baseWaitMs) const {
    return effectiveWaitMs(baseWaitMs) - baseWaitMs;
  }

  // Timer thuộc chính backend Backspace. Lag ở đây được tính cho transaction
  // hiện tại để giữ nguyên mức wait sau khi transaction kết thúc.
  Adjustment observeTimer(uint64_t deadlineUsec, uint64_t firedAtUsec) {
    return observeLateness(deadlineUsec, firedAtUsec, true);
  }

  // Probe chạy liên tục trên event loop, kể cả khi chưa có rewrite. Nhờ đó một
  // transaction chỉ có một Backspace vẫn biết máy đang chậm ngay từ đầu.
  Adjustment observeSystemTimer(uint64_t deadlineUsec, uint64_t firedAtUsec) {
    return observeLateness(deadlineUsec, firedAtUsec, transactionActive_);
  }

  // Chỉ giảm 10 ms sau nhiều transaction ổn định. Không đếm từng timer con vì
  // một transaction nhiều Backspace có thể làm mức wait tụt quá nhanh.
  Adjustment completeTransaction() {
    transactionActive_ = false;
    if (lagObservedInTransaction_) {
      lagObservedInTransaction_ = false;
      return Adjustment::None;
    }
    if (extraWaitMs_ == 0) {
      stableTransactions_ = 0;
      return Adjustment::None;
    }

    if (++stableTransactions_ < StableTransactionsToDecay) {
      return Adjustment::None;
    }
    stableTransactions_ = 0;
    extraWaitMs_ -= std::min(StepMs, extraWaitMs_);
    return Adjustment::Decreased;
  }

  uint32_t extraWaitMs() const { return extraWaitMs_; }

private:
  Adjustment observeLateness(uint64_t deadlineUsec, uint64_t firedAtUsec,
                             bool markTransaction) {
    const uint64_t latenessUsec =
        firedAtUsec > deadlineUsec ? firedAtUsec - deadlineUsec : 0;
    if (latenessUsec >= LagThresholdUsec) {
      if (markTransaction) {
        lagObservedInTransaction_ = true;
      }
      stableTransactions_ = 0;
      const uint32_t previous = extraWaitMs_;
      // Đổi độ trễ sang ms rồi làm tròn lên bước 10 ms. Ví dụ 12.9 ms thành
      // 20 ms để lần phát hiện đầu tiên có thêm biên an toàn.
      const uint64_t latenessMs = std::min<uint64_t>(
          MaxWaitMs, latenessUsec / 1000 + (latenessUsec % 1000 != 0));
      const uint32_t roundedLatenessMs =
          static_cast<uint32_t>(std::min<uint64_t>(
              MaxWaitMs, ((latenessMs + StepMs - 1) / StepMs) * StepMs));
      // Nếu lag lặp lại, tiếp tục học thêm tối thiểu một bước 10 ms ngay cả khi
      // độ trễ mới không lớn hơn mức đã ghi nhận.
      const uint32_t steppedWaitMs = std::min(MaxWaitMs, extraWaitMs_ + StepMs);
      extraWaitMs_ = std::max(steppedWaitMs, roundedLatenessMs);
      return extraWaitMs_ != previous ? Adjustment::Increased
                                      : Adjustment::None;
    }

    return Adjustment::None;
  }
  uint32_t extraWaitMs_ = 0;
  uint32_t stableTransactions_ = 0;
  bool transactionActive_ = false;
  bool lagObservedInTransaction_ = false;
};

} // namespace areca
