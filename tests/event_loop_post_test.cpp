#include <cassert>
#include <vector>

#include <fcitx-utils/event.h>

#include "event_loop_post.h"

int main() {
  fcitx::EventLoop eventLoop;
  areca::EventLoopPostTask postTask(eventLoop);
  std::vector<int> order;

  // Post event phải chạy sau một event bình thường dù được đăng ký trước.
  postTask.schedule([&]() {
    order.push_back(2);
    eventLoop.exit();
  });
  auto normalEvent = eventLoop.addDeferEvent([&](fcitx::EventSource *) {
    order.push_back(1);
    return false;
  });
  assert(normalEvent);
  normalEvent->setOneShot();

  assert(eventLoop.exec());
  assert((order == std::vector<int>{1, 2}));
}
