#include "browser_autocomplete.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <fcitx-utils/utf8.h>

namespace areca {
namespace {

size_t utf8ByteOffsetForCharIndex(const std::string &text, size_t charIndex) {
  const auto length = fcitx::utf8::length(text);
  if (charIndex >= length) {
    return text.size();
  }
  const auto it = fcitx::utf8::nextNChar(text.begin(), charIndex);
  return static_cast<size_t>(std::distance(text.begin(), it));
}

size_t utf8CharIndexForByteOffset(const std::string &text, size_t byteOffset) {
  byteOffset = std::min(byteOffset, text.size());
  return fcitx::utf8::length(
      std::string(text.begin(), text.begin() + byteOffset));
}

std::string normalizedProgramName(std::string program) {
  const auto slash = program.find_last_of('/');
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  for (char &c : program) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  constexpr const char suffix[] = ".desktop";
  constexpr size_t suffixLength = sizeof(suffix) - 1;
  if (program.size() >= suffixLength &&
      program.compare(program.size() - suffixLength, suffixLength, suffix) ==
          0) {
    program.resize(program.size() - suffixLength);
  }
  return program;
}

} // namespace

bool isBrowserLikeProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  if (program.empty()) {
    // OpenKey deliberately assumes browser-like when the frontend does not
    // expose a program name.
    return true;
  }

  static constexpr std::array<const char *, 33> patterns = {"chrome",
                                                            "google-chrome",
                                                            "chromium",
                                                            "chromium-browser",
                                                            "edge",
                                                            "msedge",
                                                            "brave",
                                                            "vivaldi",
                                                            "opera",
                                                            "opera-beta",
                                                            "opera-developer",
                                                            "coccoc",
                                                            "yandex",
                                                            "firefox",
                                                            "librewolf",
                                                            "waterfox",
                                                            "floorp",
                                                            "zen",
                                                            "tor-browser",
                                                            "torbrowser",
                                                            "epiphany",
                                                            "falkon",
                                                            "midori",
                                                            "qutebrowser",
                                                            "palemoon",
                                                            "basilisk",
                                                            "nyxt",
                                                            "otter",
                                                            "dooble",
                                                            "arc",
                                                            "helium",
                                                            "mullvad",
                                                            "window:"};
  return std::any_of(patterns.begin(), patterns.end(),
                     [&program](const char *pattern) {
                       return program.find(pattern) != std::string::npos;
                     });
}

BrowserAutocompleteStrategy
browserAutocompleteStrategy(const std::string &rawProgram, bool isUrl) {
  const std::string program = normalizedProgramName(rawProgram);
  if (isUrl && (program.find("microsoft-edge") != std::string::npos ||
                program.find("msedge") != std::string::npos)) {
    return BrowserAutocompleteStrategy::EdgeUrlForwardTwo;
  }
  return BrowserAutocompleteStrategy::ForwardOne;
}

bool looksLikeBrowserAutocomplete(const std::string &text, unsigned int cursor,
                                  unsigned int anchor,
                                  const std::string &shownText) {
  if (shownText.empty() || !fcitx::utf8::validate(text) ||
      !fcitx::utf8::validate(shownText)) {
    return false;
  }

  const size_t textLength = fcitx::utf8::length(text);
  const size_t shownLength = fcitx::utf8::length(shownText);
  if (cursor > textLength || anchor > textLength || shownLength == 0 ||
      shownLength > textLength) {
    return false;
  }

  unsigned int prefixCursor = cursor;
  if (cursor != anchor) {
    const unsigned int selectionStart = std::min(cursor, anchor);
    const unsigned int selectionEnd = std::max(cursor, anchor);
    if (selectionEnd == cursor) {
      prefixCursor = selectionStart;
    }
  }

  const size_t rangeStart =
      prefixCursor >= shownLength ? prefixCursor - shownLength : 0;
  bool samePrefix = false;
  for (size_t byte = text.find(shownText); byte != std::string::npos;
       byte = text.find(shownText, byte + 1)) {
    const size_t charIndex = utf8CharIndexForByteOffset(text, byte);
    if (charIndex >= rangeStart && charIndex + shownLength == prefixCursor) {
      samePrefix = true;
      break;
    }
  }
  if (!samePrefix || cursor == anchor) {
    return false;
  }

  const unsigned int selectionStart = std::min(cursor, anchor);
  const unsigned int selectionEnd = std::max(cursor, anchor);
  const bool selectionTouchesCursor =
      selectionStart == cursor || selectionEnd == cursor ||
      (selectionStart < cursor && selectionEnd > cursor);

  const size_t selectionStartByte =
      utf8ByteOffsetForCharIndex(text, selectionStart);
  const size_t nextLineBreak = text.find('\n', selectionStartByte);
  const size_t lineEnd = nextLineBreak == std::string::npos
                             ? textLength
                             : utf8CharIndexForByteOffset(text, nextLineBreak);
  const bool selectionGoesToLineEnd = selectionEnd == lineEnd;

  const size_t selectionEndByte =
      utf8ByteOffsetForCharIndex(text, selectionEnd);
  const size_t newline = text.find('\n', selectionStartByte);
  const bool hasNewline =
      newline != std::string::npos && newline < selectionEndByte;

  return selectionTouchesCursor && selectionGoesToLineEnd && !hasNewline;
}

} // namespace areca
