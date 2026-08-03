#pragma once

#include "mode_handler.h"

namespace areca {

class RedirectModeHandler final : public InputModeHandler {
public:
  void activate(fcitx::InputContext &) override {}
  void deactivate(fcitx::InputContext &) override {}
  void handleKeyEvent(fcitx::KeyEvent &event) override;
  void requestProtectedReset(fcitx::InputContext &) override {}
  void resetContext(fcitx::InputContext &) override {}
};

} // namespace areca
