#include "areca_engine.h"

#include <exception>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

namespace areca {
namespace {

constexpr const char *kMacroConfigPath = "conf/areca-macro-table.conf";
constexpr const char *kAdvancedConfigPath = "conf/areca-advanced.conf";
constexpr const char *kLegacyDefaultSocketPath = "/tmp/openkey-nonpreedit.sock";
constexpr const char *kDefaultSocketPath = "/tmp/areca-uinput.sock";

} // namespace

ArecaEngine::ArecaEngine(fcitx::Instance *instance)
    : instance_(instance), rewriteStateFactory_([this](fcitx::InputContext &) {
        return new RewriteInputState(
            config_.bambooInputMethod.value(), config_.spellCheck.value(),
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions());
      }),
      preeditStateFactory_([this](fcitx::InputContext &) {
        return new PreeditInputState(
            config_.bambooInputMethod.value(), config_.spellCheck.value(),
            config_.modernStyle.value(), config_.outputCharset.value(),
            config_.enableMacro.value(), config_.capitalizeMacro.value(),
            macroRevision_, macroDefinitions());
      }),
      uinputBackend_(instance_->eventLoop(),
                     advancedConfig_.socketPath.value()),
      scheduler_(
          instance_->eventLoop(),
          [this](fcitx::InputContext &inputContext) -> VietnameseEngine * {
            auto *state = inputContext.propertyFor(&rewriteStateFactory_);
            return state ? state->engine.get() : nullptr;
          },
          [this]() { return timing(); }, [this]() { return debugEnabled(); },
          [this](fcitx::InputContext &inputContext,
                 const BambooResult &result) -> RewriteBackendSelection {
            auto *state = inputContext.propertyFor(&rewriteStateFactory_);
            if (!state) {
              return {&uinputBackend_, 0};
            }
            const auto decision = reliabilityChecker_.evaluate(
                inputContext, result.currentText, state->surroundingReliability,
                debugEnabled());
            if (decision.useSurrounding) {
              return {&surroundingBackend_, 0};
            }
            return {&uinputBackend_, decision.additionalFallbackBackspaces};
          }),
      rewriteHandler_(
          instance_->eventLoop(), rewriteStateFactory_, scheduler_,
          uinputBackend_,
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); },
          [this]() { return debugEnabled(); },
          [this]() {
            return static_cast<uint32_t>(advancedConfig_.resetDelayMs.value());
          }),
      preeditHandler_(
          instance_->eventLoop(), preeditStateFactory_,
          [this]() { return debugEnabled(); },
          [this]() { return config_.autoCapitalizeAfterPunctuation.value(); },
          [this]() {
            return static_cast<uint32_t>(advancedConfig_.resetDelayMs.value());
          }) {
  instance_->inputContextManager().registerProperty("arecaRewriteState",
                                                    &rewriteStateFactory_);
  instance_->inputContextManager().registerProperty("arecaPreeditState",
                                                    &preeditStateFactory_);
  config_.bambooInputMethod.annotation().setList(
      BambooEngineAdapter::inputMethodNames());
  config_.outputCharset.annotation().setList(
      BambooEngineAdapter::charsetNames());
  reloadConfig();
}

ArecaEngine::~ArecaEngine() = default;

InputModeHandler &ArecaEngine::activeHandler() {
  return activePresentationMode_ == PresentationMode::Preedit
             ? static_cast<InputModeHandler &>(preeditHandler_)
             : static_cast<InputModeHandler &>(rewriteHandler_);
}

const char *ArecaEngine::presentationModeName() const {
  return activePresentationMode_ == PresentationMode::Preedit ? "Preedit"
                                                              : "Rewrite";
}

std::string ArecaEngine::subMode(const fcitx::InputMethodEntry &,
                                 fcitx::InputContext &) {
  return presentationModeName();
}

std::string ArecaEngine::subModeIconImpl(const fcitx::InputMethodEntry &,
                                         fcitx::InputContext &) {
  return "org.fcitx.Fcitx5.fcitx-areca";
}

std::string ArecaEngine::subModeLabelImpl(const fcitx::InputMethodEntry &,
                                          fcitx::InputContext &) {
  return "Ă  " + config_.bambooInputMethod.value() + " \xC2\xB7 " +
         presentationModeName();
}

void ArecaEngine::switchPresentationMode(fcitx::InputContext &inputContext) {
  if (uinputBackend_.hasPending() || scheduler_.rewritePending()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: mode hotkey ignored while rewrite pending";
    }
    return;
  }

  const auto next = activePresentationMode_ == PresentationMode::Rewrite
                        ? PresentationMode::Preedit
                        : PresentationMode::Rewrite;
  config_.presentationMode.setValue(next);
  applyConfig();
  save();

  if (debugEnabled()) {
    FCITX_INFO() << "areca: switch mode hotkey mode=" << presentationModeName()
                 << " program=" << inputContext.program();
  }
  instance_->showInputMethodInformation(&inputContext);
}

void ArecaEngine::activate(const fcitx::InputMethodEntry &,
                           fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  activeHandler().activate(*inputContext);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: activate presentation_mode="
                 << (activePresentationMode_ == PresentationMode::Preedit
                         ? "preedit"
                         : "rewrite")
                 << " program=" << inputContext->program();
  }
}

void ArecaEngine::keyEvent(const fcitx::InputMethodEntry &,
                           fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  if (!event.isRelease()) {
    const auto key = event.key().normalize();
    if (key.sym() != FcitxKey_None &&
        key.checkKeyList(config_.switchModeKey.value())) {
      if (inputContext->capabilityFlags().test(
              fcitx::CapabilityFlag::Password)) {
        activeHandler().handleKeyEvent(event);
        return;
      }
      event.filterAndAccept();
      switchPresentationMode(*inputContext);
      return;
    }
  }
  activeHandler().handleKeyEvent(event);
}

void ArecaEngine::deactivate(const fcitx::InputMethodEntry &,
                             fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  activeHandler().deactivate(*inputContext);
}

void ArecaEngine::reset(const fcitx::InputMethodEntry &,
                        fcitx::InputContextEvent &event) {
  if (auto *inputContext = event.inputContext()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: app reset requested; arm protected reset";
    }
    activeHandler().requestProtectedReset(*inputContext);
  }
}

const fcitx::Configuration *ArecaEngine::getConfig() const { return &config_; }

const fcitx::Configuration *
ArecaEngine::getSubConfig(const std::string &path) const {
  if (path == "macro") {
    return &macroTable_;
  }
  if (path == "advanced") {
    return &advancedConfig_;
  }
  return nullptr;
}

void ArecaEngine::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  applyConfig();
  save();
}

void ArecaEngine::setSubConfig(const std::string &path,
                               const fcitx::RawConfig &config) {
  if (path == "advanced") {
    advancedConfig_.load(config, true);
    fcitx::safeSaveAsIni(advancedConfig_, fcitx::StandardPathsType::PkgConfig,
                         kAdvancedConfigPath);
    applyConfig();
    return;
  }
  if (path != "macro") {
    return;
  }
  macroTable_.load(config, true);
  fcitx::safeSaveAsIni(macroTable_, fcitx::StandardPathsType::PkgConfig,
                       kMacroConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::reloadConfig() {
  fcitx::readAsIni(config_, fcitx::StandardPathsType::PkgConfig,
                   "conf/areca.conf");
  // Seed the new advanced panel from the legacy fields before loading its own
  // file. Existing installations therefore retain their timing and socket;
  // once the advanced file exists, its values take precedence.
  advancedConfig_.keyIntervalMs.setValue(config_.legacyKeyIntervalMs.value());
  advancedConfig_.backspaceDelayMs.setValue(
      config_.legacyBackspaceDelayMs.value());
  advancedConfig_.afterBackspaceWaitMs.setValue(
      config_.legacyAfterBackspaceWaitMs.value());
  advancedConfig_.postCommitDelayMs.setValue(
      config_.legacyPostCommitDelayMs.value());
  advancedConfig_.resetDelayMs.setValue(config_.legacyResetDelayMs.value());
  advancedConfig_.socketPath.setValue(config_.legacySocketPath.value());
  fcitx::readAsIni(advancedConfig_, fcitx::StandardPathsType::PkgConfig,
                   kAdvancedConfigPath);
  if (advancedConfig_.socketPath.value() == kLegacyDefaultSocketPath) {
    advancedConfig_.socketPath.setValue(kDefaultSocketPath);
  }
  fcitx::readAsIni(macroTable_, fcitx::StandardPathsType::PkgConfig,
                   kMacroConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::save() {
  fcitx::safeSaveAsIni(config_, fcitx::StandardPathsType::PkgConfig,
                       "conf/areca.conf");
  fcitx::safeSaveAsIni(macroTable_, fcitx::StandardPathsType::PkgConfig,
                       kMacroConfigPath);
  fcitx::safeSaveAsIni(advancedConfig_, fcitx::StandardPathsType::PkgConfig,
                       kAdvancedConfigPath);
}

std::vector<MacroDefinition> ArecaEngine::macroDefinitions() const {
  std::vector<MacroDefinition> result;
  const auto &entries = macroTable_.macros.value();
  result.reserve(entries.size());
  for (const auto &entry : entries) {
    if (!entry.key.value().empty() && !entry.value.value().empty()) {
      result.push_back({entry.key.value(), entry.value.value()});
    }
  }
  return result;
}

SchedulerTiming ArecaEngine::timing() const {
  return {static_cast<uint32_t>(advancedConfig_.keyIntervalMs.value()),
          static_cast<uint32_t>(advancedConfig_.backspaceDelayMs.value()),
          static_cast<uint32_t>(advancedConfig_.afterBackspaceWaitMs.value()),
          static_cast<uint32_t>(advancedConfig_.postCommitDelayMs.value())};
}

void ArecaEngine::applyConfig() {
  uinputBackend_.setSocketPath(advancedConfig_.socketPath.value());
  uinputBackend_.setDebug(debugEnabled());
  if (uinputBackend_.hasPending()) {
    return;
  }

  const auto inputMethod = config_.bambooInputMethod.value();
  const bool spellCheck = config_.spellCheck.value();
  const bool modernStyle = config_.modernStyle.value();
  const auto outputCharset = config_.outputCharset.value();
  const bool macroEnabled = config_.enableMacro.value();
  const bool capitalizeMacro = config_.capitalizeMacro.value();
  const bool autoCapitalize = config_.autoCapitalizeAfterPunctuation.value();
  const auto requestedMode = config_.presentationMode.value();
  const bool modeChanged = requestedMode != activePresentationMode_;
  const auto macros = macroDefinitions();
  instance_->inputContextManager().foreach (
      [this, &inputMethod, spellCheck, modernStyle, &outputCharset,
       macroEnabled, capitalizeMacro, autoCapitalize, modeChanged,
       &macros](fcitx::InputContext *inputContext) {
        if (modeChanged) {
          rewriteHandler_.resetContext(*inputContext);
          preeditHandler_.resetContext(*inputContext);
        }

        auto updateEngine = [&](auto *state, auto resetMode) {
          if (!state) {
            return;
          }
          if (!autoCapitalize) {
            state->sentenceCapitalization.reset();
          }
          if (state->inputMethod == inputMethod &&
              state->spellCheck == spellCheck &&
              state->modernStyle == modernStyle &&
              state->outputCharset == outputCharset &&
              state->macroEnabled == macroEnabled &&
              state->capitalizeMacro == capitalizeMacro &&
              state->macroRevision == macroRevision_) {
            return;
          }
          try {
            resetMode();
            state->engine = std::make_unique<BambooEngineAdapter>(
                inputMethod, spellCheck, modernStyle, outputCharset,
                macroEnabled, capitalizeMacro, macros);
            state->inputMethod = inputMethod;
            state->spellCheck = spellCheck;
            state->modernStyle = modernStyle;
            state->outputCharset = outputCharset;
            state->macroEnabled = macroEnabled;
            state->capitalizeMacro = capitalizeMacro;
            state->macroRevision = macroRevision_;
          } catch (const std::exception &error) {
            FCITX_ERROR() << "areca: cannot select Bamboo method: "
                          << error.what();
          }
        };

        updateEngine(rewriteHandler_.stateFor(*inputContext),
                     [this, inputContext]() {
                       rewriteHandler_.resetContext(*inputContext);
                     });
        updateEngine(preeditHandler_.stateFor(*inputContext),
                     [this, inputContext]() {
                       preeditHandler_.resetContext(*inputContext);
                     });
        return true;
      });
  activePresentationMode_ = requestedMode;
}

class ArecaEngineFactory final : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new ArecaEngine(manager->instance());
  }
};

} // namespace areca

FCITX_ADDON_FACTORY(areca::ArecaEngineFactory)
