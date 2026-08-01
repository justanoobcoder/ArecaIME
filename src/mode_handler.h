#pragma once

#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>

namespace areca {

// Lifecycle contract only. Implementations own independent state and queues.
class InputModeHandler {
public:
  virtual ~InputModeHandler() = default;

  virtual void activate(fcitx::InputContext &inputContext) = 0;
  virtual void deactivate(fcitx::InputContext &inputContext) = 0;
  virtual void handleKeyEvent(fcitx::KeyEvent &event) = 0;
  virtual void requestProtectedReset(fcitx::InputContext &inputContext) = 0;
  virtual void resetContext(fcitx::InputContext &inputContext) = 0;
};

} // namespace areca
