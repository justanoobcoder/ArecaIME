#pragma once

#include <cstdint>
#include <string>

namespace areca {

struct BambooResult {
  std::string currentText;
  std::string newText;
  uint32_t deleteCount = 0;
  std::string commitText;
  bool macroExpanded = false;
};

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  // Number of characters represented in the Fcitx surrounding-text cache.
  // This excludes the uinput server's sentinel Backspace.
  uint32_t cacheDeleteCount = 0;
  uint32_t backspaceDelayMs = 5;
  uint32_t afterBackspaceWaitMs = 40;
  uint32_t ackFullWaitMs = 20;
  std::string commitText;
};

} // namespace areca
