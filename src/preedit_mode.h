#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>

#include "bamboo_engine_adapter.h"
#include "mode_handler.h"
#include "sentence_capitalizer.h"

namespace areca {

struct PreeditInputState final : public fcitx::InputContextProperty {
  explicit PreeditInputState(std::string inputMethod, bool spellCheck,
                             bool modernStyle, std::string outputCharset,
                             bool macroEnabled, bool capitalizeMacro,
                             uint64_t macroRevision,
                             std::vector<MacroDefinition> macros);

  std::string inputMethod;
  bool spellCheck;
  bool modernStyle;
  std::string outputCharset;
  bool macroEnabled;
  bool capitalizeMacro;
  uint64_t macroRevision;
  std::unique_ptr<VietnameseEngine> engine;
  std::string composing;
  SentenceCapitalizationState sentenceCapitalization;
  std::unique_ptr<fcitx::EventSourceTime> delayedResetTimer;
};

class PreeditModeHandler final : public InputModeHandler {
public:
  using StateFactory = fcitx::FactoryFor<PreeditInputState>;
  using DebugProvider = std::function<bool()>;
  using AutoCapitalizeProvider = std::function<bool()>;
  using ResetDelayProvider = std::function<uint32_t()>;

  PreeditModeHandler(fcitx::EventLoop &eventLoop, StateFactory &stateFactory,
                     DebugProvider debugProvider,
                     AutoCapitalizeProvider autoCapitalizeProvider,
                     ResetDelayProvider resetDelayProvider);
  ~PreeditModeHandler();

  void activate(fcitx::InputContext &inputContext) override;
  void deactivate(fcitx::InputContext &inputContext) override;
  void handleKeyEvent(fcitx::KeyEvent &event) override;
  void requestProtectedReset(fcitx::InputContext &inputContext) override;
  void resetContext(fcitx::InputContext &inputContext) override;

  PreeditInputState *stateFor(fcitx::InputContext &inputContext) const;

private:
  void cancelProtectedReset(fcitx::InputContext &inputContext);
  void updatePreedit(fcitx::InputContext &inputContext,
                     const PreeditInputState &state);
  void clearComposition(fcitx::InputContext &inputContext,
                        PreeditInputState &state);
  void commitComposition(fcitx::InputContext &inputContext,
                         PreeditInputState &state);

  fcitx::EventLoop &eventLoop_;
  StateFactory &stateFactory_;
  DebugProvider debugProvider_;
  AutoCapitalizeProvider autoCapitalizeProvider_;
  ResetDelayProvider resetDelayProvider_;
  std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
};

} // namespace areca
