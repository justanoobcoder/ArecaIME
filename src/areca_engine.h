#pragma once

#include <memory>
#include <string>

#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "areca_config.h"
#include "bamboo_engine_adapter.h"
#include "input_mode.h"
#include "input_scheduler.h"
#include "reliability_checker.h"
#include "surrounding_text_backend.h"
#include "uinput_socket_backend.h"

namespace areca {

struct InputState final : public fcitx::InputContextProperty {
  explicit InputState(std::string inputMethod, bool spellCheck,
                      bool modernStyle, std::string outputCharset)
      : inputMethod(std::move(inputMethod)),
        spellCheck(spellCheck),
        modernStyle(modernStyle),
        outputCharset(std::move(outputCharset)),
        engine(std::make_unique<BambooEngineAdapter>(this->inputMethod,
                                                     this->spellCheck,
                                                     this->modernStyle,
                                                     this->outputCharset)) {}

  std::string inputMethod;
  bool spellCheck;
  bool modernStyle;
  std::string outputCharset;
  std::unique_ptr<VietnameseEngine> engine;
  SurroundingReliabilityState surroundingReliability;
  std::unique_ptr<fcitx::EventSourceTime> delayedResetTimer;
};

class ArecaEngine final : public fcitx::InputMethodEngineV2 {
public:
  explicit ArecaEngine(fcitx::Instance *instance);
  ~ArecaEngine() override;

  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &event) override;
  void activate(const fcitx::InputMethodEntry &entry,
                fcitx::InputContextEvent &event) override;
  void reset(const fcitx::InputMethodEntry &entry,
             fcitx::InputContextEvent &event) override;

  const fcitx::Configuration *getConfig() const override;
  void setConfig(const fcitx::RawConfig &config) override;
  void reloadConfig() override;
  void save() override;

private:
  InputState *stateFor(fcitx::InputContext &inputContext) const;
  SchedulerTiming timing() const;
  bool debugEnabled() const { return config_.debug.value(); }
  void applyConfig();
  void scheduleProtectedStateReset(fcitx::InputContext &inputContext,
                                   InputState &state);
  void cancelProtectedStateReset(fcitx::InputContext &inputContext);
  void performContextStateReset(fcitx::InputContext &inputContext,
                                InputState &state);

  fcitx::Instance *instance_;
  std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
  ArecaConfig config_;
  fcitx::FactoryFor<InputState> stateFactory_;
  ReliabilityChecker reliabilityChecker_;
  SurroundingTextBackend surroundingBackend_;
  UinputSocketBackend uinputBackend_;
  InputScheduler scheduler_;
  std::unique_ptr<InputModeHandler> inputMode_;
};

} // namespace areca
