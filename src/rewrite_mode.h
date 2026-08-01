#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>

#include "bamboo_engine_adapter.h"
#include "input_scheduler.h"
#include "mode_handler.h"
#include "reliability_checker.h"
#include "sentence_capitalizer.h"
#include "uinput_socket_backend.h"

namespace areca {

struct RewriteInputState final : public fcitx::InputContextProperty {
  explicit RewriteInputState(std::string inputMethod, bool spellCheck,
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
  SentenceCapitalizationState sentenceCapitalization;
  SurroundingReliabilityState surroundingReliability;
  std::unique_ptr<fcitx::EventSourceTime> delayedResetTimer;
};

class RewriteModeHandler final : public InputModeHandler {
public:
  using StateFactory = fcitx::FactoryFor<RewriteInputState>;
  using BoolProvider = std::function<bool()>;
  using ResetDelayProvider = std::function<uint32_t()>;

  RewriteModeHandler(fcitx::EventLoop &eventLoop, StateFactory &stateFactory,
                     InputScheduler &scheduler,
                     UinputSocketBackend &uinputBackend,
                     BoolProvider autoCapitalizeProvider,
                     BoolProvider debugProvider,
                     ResetDelayProvider resetDelayProvider);
  ~RewriteModeHandler();

  RewriteInputState *stateFor(fcitx::InputContext &inputContext) const;
  void activate(fcitx::InputContext &inputContext) override;
  void deactivate(fcitx::InputContext &inputContext) override;
  void handleKeyEvent(fcitx::KeyEvent &event) override;
  void requestProtectedReset(fcitx::InputContext &inputContext) override;
  void cancelProtectedReset(fcitx::InputContext &inputContext);
  void resetContext(fcitx::InputContext &inputContext) override;

private:
  void scheduleProtectedReset(fcitx::InputContext &inputContext,
                              RewriteInputState &state);

  fcitx::EventLoop &eventLoop_;
  StateFactory &stateFactory_;
  InputScheduler &scheduler_;
  UinputSocketBackend &uinputBackend_;
  BoolProvider autoCapitalizeProvider_;
  BoolProvider debugProvider_;
  ResetDelayProvider resetDelayProvider_;
  std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
};

} // namespace areca
