#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <fcitx-utils/event.h>

namespace areca {

// Đưa một callback sang pha post của event loop thay vì chạy ngay bên trong
// callback timer. Pha này cho các event đang chờ được dispatch trước khi commit.
// Không dùng timer accuracy=0 để giả lập vì sd-event có thể coalesce tới 250 ms.
class EventLoopPostTask {
public:
  explicit EventLoopPostTask(fcitx::EventLoop &eventLoop)
      : eventLoop_(eventLoop) {}

  void schedule(std::function<void()> callback) {
    source_.reset();
    source_ = eventLoop_.addPostEvent(
        [this, callback](fcitx::EventSource *) mutable {
          auto completedSource = std::move(source_);
          callback();
          return false;
        });
    if (!source_) {
      callback();
      return;
    }
    source_->setOneShot();
  }

  void cancel() { source_.reset(); }

private:
  fcitx::EventLoop &eventLoop_;
  std::unique_ptr<fcitx::EventSource> source_;
};

} // namespace areca
