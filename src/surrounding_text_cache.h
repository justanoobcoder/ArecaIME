#pragma once

#include <cstdint>
#include <string>

namespace fcitx {
class InputContext;
}

namespace areca {

void updateSurroundingCacheAfterDelete(fcitx::InputContext &inputContext,
                                       int offset, uint32_t count);
void updateSurroundingCacheAfterSelectionDelete(
    fcitx::InputContext &inputContext);
void updateSurroundingCacheAfterCommit(fcitx::InputContext &inputContext,
                                       const std::string &committedText);

} // namespace areca
