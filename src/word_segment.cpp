#include "word_segment.h"

#include <cstdint>
#include <vector>

#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

bool isWordChar(uint32_t codepoint) {
  if (codepoint == '_') {
    return true;
  }
  if (codepoint <= 0x7f) {
    return (codepoint >= '0' && codepoint <= '9') ||
           (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z');
  }

  // Combining marks and the Unicode blocks containing Vietnamese letters.
  return (codepoint >= 0x0300 && codepoint <= 0x036f) ||
         (codepoint >= 0x00c0 && codepoint <= 0x024f) ||
         (codepoint >= 0x1e00 && codepoint <= 0x1eff);
}

} // namespace

bool extractWordBeforeCursor(const std::string &text, unsigned int cursorChar,
                             WordSegment &out) {
  out = {};
  if (!fcitx::utf8::validate(text)) {
    return false;
  }

  const auto totalLength = fcitx::utf8::length(text);
  if (cursorChar > totalLength) {
    return false;
  }

  std::vector<uint32_t> codepoints;
  codepoints.reserve(totalLength);
  for (auto it = fcitx::utf8::UTF8CharIterator(text.begin(), text.end()),
            end = fcitx::utf8::UTF8CharIterator(text.end(), text.end());
       it != end; ++it) {
    codepoints.push_back(*it);
  }

  unsigned int startChar = cursorChar;
  while (startChar > 0 && isWordChar(codepoints[startChar - 1])) {
    --startChar;
  }

  // Avoid dereferencing end() in ncharByteLength for an empty string.
  if (text.empty()) {
    return false;
  }
  const auto startByte = fcitx::utf8::ncharByteLength(text.begin(), startChar);
  const auto endByte = fcitx::utf8::ncharByteLength(text.begin(), cursorChar);
  if (startByte < 0 || endByte <= startByte) {
    return false;
  }

  out.word = text.substr(static_cast<size_t>(startByte),
                         static_cast<size_t>(endByte - startByte));
  out.startChar = startChar;
  out.endChar = cursorChar;
  return true;
}

} // namespace areca
