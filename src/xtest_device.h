#pragma once

#include <cstdint>
#include <functional>
#include <string>

typedef struct _XDisplay Display;

namespace areca {

class XTestDevice {
public:
  using DebugProvider = std::function<bool()>;

  explicit XTestDevice(DebugProvider debugProvider);
  ~XTestDevice();

  bool isAvailable();
  bool ensureDevice(const char *displayName = nullptr);
  void closeDevice();
  bool sendKey(uint32_t keysym, bool press);
  bool sendBackspace();

private:
  DebugProvider debugProvider_;
  Display *display_ = nullptr;
  std::string activeDisplayName_;
  bool initialized_ = false;
  bool xtestSupported_ = false;
  uint32_t backspaceKeycode_ = 0;
};

} // namespace areca
