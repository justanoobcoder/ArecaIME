#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <fcitx/inputcontext.h>

#include "input_scheduler.h"

namespace areca {

// Input presentation is intentionally above the scheduler/backends. A future
// PreeditOnlyMode can implement this interface without adding preedit branches
// to BambooEngineAdapter or RewriteBackend.
class InputModeHandler {
public:
  virtual ~InputModeHandler() = default;
  virtual void handleTextKey(fcitx::InputContext &inputContext,
                             const fcitx::Key &originalKey,
                             uint32_t codepoint, std::string utf8Text) = 0;
  virtual void reset(fcitx::InputContext &inputContext) = 0;
};

class QueuedRewriteMode final : public InputModeHandler {
public:
  explicit QueuedRewriteMode(InputScheduler &scheduler)
      : scheduler_(scheduler) {}

  void handleTextKey(fcitx::InputContext &inputContext,
                     const fcitx::Key &originalKey, uint32_t codepoint,
                     std::string utf8Text) override {
    scheduler_.enqueue(inputContext, originalKey, codepoint,
                       std::move(utf8Text));
  }

  void reset(fcitx::InputContext &inputContext) override {
    scheduler_.resetContext(inputContext);
  }

private:
  InputScheduler &scheduler_;
};

} // namespace areca
