#include "xtest_device.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <fcitx-utils/log.h>

namespace areca {

XTestDevice::XTestDevice(DebugProvider debugProvider)
    : debugProvider_(std::move(debugProvider)) {}

XTestDevice::~XTestDevice() { closeDevice(); }

bool XTestDevice::isAvailable() { return ensureDevice(); }

bool XTestDevice::ensureDevice(const char *displayName) {
  const std::string name = displayName ? displayName : "";
  if (initialized_ && display_) {
    if (activeDisplayName_ == name) {
      return xtestSupported_;
    }
    closeDevice();
  }

  initialized_ = true;
  activeDisplayName_ = name;

  const char *openTarget = displayName && displayName[0] ? displayName : nullptr;
  display_ = XOpenDisplay(openTarget);
  if (!display_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: xtest failed to open X display "
                   << (openTarget ? openTarget : "default");
    }
    xtestSupported_ = false;
    return false;
  }

  int eventBase = 0;
  int errorBase = 0;
  int majorVersion = 0;
  int minorVersion = 0;
  if (!XTestQueryExtension(display_, &eventBase, &errorBase, &majorVersion,
                           &minorVersion)) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: XTest extension not supported on display "
                   << (openTarget ? openTarget : "default");
    }
    closeDevice();
    return false;
  }

  backspaceKeycode_ = XKeysymToKeycode(display_, XK_BackSpace);
  if (!backspaceKeycode_) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: xtest BackSpace keycode unavailable on display "
                   << (openTarget ? openTarget : "default");
    }
    closeDevice();
    return false;
  }
  xtestSupported_ = true;

  if (debugProvider_()) {
    FCITX_INFO() << "areca: xtest device initialized successfully display="
                 << (openTarget ? openTarget : "default") << " v"
                 << majorVersion << "." << minorVersion
                 << " backspace_keycode=" << backspaceKeycode_;
  }
  return true;
}

void XTestDevice::closeDevice() {
  if (display_) {
    XCloseDisplay(display_);
    display_ = nullptr;
  }
  initialized_ = false;
  xtestSupported_ = false;
  backspaceKeycode_ = 0;
  activeDisplayName_.clear();
}

bool XTestDevice::sendBackspace() {
  if (!ensureDevice() || !backspaceKeycode_) {
    return false;
  }

  const bool pressed =
      XTestFakeKeyEvent(display_, backspaceKeycode_, True, CurrentTime);
  const bool released =
      XTestFakeKeyEvent(display_, backspaceKeycode_, False, CurrentTime);
  XFlush(display_);
  return pressed && released;
}

bool XTestDevice::sendKey(uint32_t keysym, bool press) {
  if (!ensureDevice()) {
    return false;
  }

  const KeyCode code = XKeysymToKeycode(display_, keysym);
  if (!code) {
    return false;
  }

  const bool sent =
      XTestFakeKeyEvent(display_, code, press ? True : False, CurrentTime);
  XFlush(display_);
  return sent;
}

} // namespace areca
