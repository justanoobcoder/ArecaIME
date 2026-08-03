#include "redirect_mode.h"

namespace areca {

void RedirectModeHandler::handleKeyEvent(fcitx::KeyEvent &event) {
  // Match password-field behavior: presses are forwarded unchanged; release
  // events remain unfiltered and naturally continue to the application.
  if (!event.isRelease()) {
    event.forward();
  }
}

} // namespace areca
