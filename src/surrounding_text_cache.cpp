#include "surrounding_text_cache.h"

#include <algorithm>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

namespace areca {

void updateSurroundingCacheAfterSelectionDelete(
    fcitx::InputContext &inputContext) {
  if (!inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText)) {
    return;
  }
  auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid() || surrounding.cursor() == surrounding.anchor()) {
    return;
  }

  const unsigned int selectionStart =
      std::min(surrounding.cursor(), surrounding.anchor());
  const unsigned int selectionEnd =
      std::max(surrounding.cursor(), surrounding.anchor());
  const auto &text = surrounding.text();
  const int startByte =
      fcitx::utf8::ncharByteLength(text.begin(), selectionStart);
  const int endByte = fcitx::utf8::ncharByteLength(text.begin(), selectionEnd);
  if (startByte < 0 || endByte < startByte) {
    surrounding.invalidate();
    return;
  }

  std::string updated;
  updated.reserve(text.size() - static_cast<size_t>(endByte - startByte));
  updated.append(text, 0, static_cast<size_t>(startByte));
  updated.append(text, static_cast<size_t>(endByte), std::string::npos);
  surrounding.setText(updated, selectionStart, selectionStart);
}

void updateSurroundingCacheAfterDelete(fcitx::InputContext &inputContext,
                                       int offset, uint32_t count) {
  if (!inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText)) {
    return;
  }
  auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid()) {
    return;
  }
  surrounding.deleteText(offset, count);
}

void updateSurroundingCacheAfterCommit(fcitx::InputContext &inputContext,
                                       const std::string &committedText) {
  if (committedText.empty() || !inputContext.capabilityFlags().test(
                                   fcitx::CapabilityFlag::SurroundingText)) {
    return;
  }
  auto &surrounding = inputContext.surroundingText();
  if (!surrounding.isValid()) {
    return;
  }

  const auto &text = surrounding.text();
  const unsigned int cursor = surrounding.cursor();
  const size_t cursorByte = fcitx::utf8::ncharByteLength(text.begin(), cursor);
  std::string updated;
  updated.reserve(text.size() + committedText.size());
  updated.append(text, 0, cursorByte);
  updated.append(committedText);
  updated.append(text, cursorByte);

  const auto committedCharacters =
      static_cast<unsigned int>(fcitx::utf8::length(committedText));
  const unsigned int newCursor = cursor + committedCharacters;
  surrounding.setText(updated, newCursor, newCursor);
}

} // namespace areca
