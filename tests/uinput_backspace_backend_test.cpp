#include <cassert>
#include <iostream>

#include <fcitx-utils/event.h>

#include "uinput_backspace_backend.h"

int main() {
  fcitx::EventLoop eventLoop;
  areca::UinputBackspaceBackend backend(eventLoop, []() { return false; });

  assert(std::string(backend.name()) == "uinput-backspace");
  assert(!backend.hasPending());

  // isAvailable() checks device initialization without throwing or crashing
  bool available = backend.isAvailable();
  std::cout << "UinputBackspaceBackend test passed, uinput available="
            << (available ? "true" : "false") << "\n";
  return 0;
}
