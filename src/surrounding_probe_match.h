#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace areca {

inline bool surroundingProbeMatches(
    bool actualValid, std::string_view expectedText, uint32_t expectedCursor,
    uint32_t expectedAnchor, std::string_view actualText,
    uint32_t actualCursor, uint32_t actualAnchor, bool fromUpdateEvent) {
  if (actualCursor != expectedCursor || actualAnchor != expectedAnchor) {
    return false;
  }

  if (actualValid && actualText == expectedText) {
    return true;
  }

  // Some clients change the number of trailing newline sentinels when the
  // final visible character is deleted. Treat only a real update event where
  // both snapshots represent an otherwise empty field as confirmation.
  const auto containsOnlyLineEndings = [](std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return c == '\n' || c == '\r'; });
  };
  return fromUpdateEvent && expectedCursor == 0 && expectedAnchor == 0 &&
         containsOnlyLineEndings(expectedText) &&
         containsOnlyLineEndings(actualText);
}

} // namespace areca
