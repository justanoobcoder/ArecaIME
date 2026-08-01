#include "preedit_logic.h"

#include <fcitx-utils/utf8.h>

namespace areca {

std::string buildPreeditCommit(const BambooResult &result) {
  if (!fcitx::utf8::validate(result.currentText)) {
    return result.commitText;
  }
  const auto length = fcitx::utf8::length(result.currentText);
  const auto keep =
      length > result.deleteCount ? length - result.deleteCount : 0;
  auto end = fcitx::utf8::nextNChar(result.currentText.begin(), keep);
  return std::string(result.currentText.begin(), end) + result.commitText;
}

} // namespace areca
