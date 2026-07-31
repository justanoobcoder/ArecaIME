#pragma once

#include <cstdint>
#include <string>

#include <fcitx-utils/trackableobject.h>
#include <fcitx/inputcontext.h>

namespace areca {

struct PendingRewriteState {
  uint64_t transactionId = 0;
  fcitx::TrackableObjectReference<fcitx::InputContext> inputContext;
  std::string commitText;
  uint32_t cacheDeleteCount = 0;
  bool cacheDeleteApplied = false;
  bool planSent = false;
  bool transportFailed = false;
  uint32_t expectedBackspaceEvents = 0;
  uint32_t seenBackspaceEvents = 0;
  bool serverDone = false;

  bool active() const { return transactionId != 0; }

  void clear() {
    transactionId = 0;
    inputContext.unwatch();
    commitText.clear();
    cacheDeleteCount = 0;
    cacheDeleteApplied = false;
    planSent = false;
    transportFailed = false;
    expectedBackspaceEvents = 0;
    seenBackspaceEvents = 0;
    serverDone = false;
  }
};

} // namespace areca
