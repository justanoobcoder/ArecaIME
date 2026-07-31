#pragma once

#include <cstdint>
#include <string>

namespace areca {

struct BambooResult {
  std::string currentText;
  std::string newText;
  uint32_t deleteCount = 0;
  std::string commitText;
};

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  // Number of characters represented in the Fcitx surrounding-text cache.
  // This excludes autocomplete cleanup and the server's sentinel Backspace.
  uint32_t cacheDeleteCount = 0;
  uint32_t backspaceDelayMs = 5;
  uint32_t afterBackspaceWaitMs = 10;
  std::string commitText;
};

} // namespace areca
