#include "areca_engine.h"

#include <exception>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#if __has_include(<fcitx-utils/standardpaths.h>)
#include <fcitx-utils/standardpaths.h>
#define ARECA_HAS_STANDARD_PATHS 1
#else
#include <fcitx-utils/standardpath.h>
#endif
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

#include "browser_autocomplete.h"
#include "input_capabilities.h"

namespace areca {
namespace {

constexpr const char *kMacroConfigPath = "conf/areca-macro-table.conf";
constexpr const char *kAdvancedConfigPath = "conf/areca-advanced.conf";
constexpr const char *kLegacyDefaultSocketPath = "/tmp/openkey-nonpreedit.sock";
constexpr const char *kDefaultSocketPath = "/tmp/areca-uinput.sock";
#if defined(ARECA_HAS_STANDARD_PATHS)
constexpr auto kPkgConfigPath = fcitx::StandardPathsType::PkgConfig;
#else
constexpr auto kPkgConfigPath = fcitx::StandardPath::Type::PkgConfig;
#endif

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
      autocompleteForwardBackend_(1), autocompleteEdgeForwardBackend_(2),
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
            return selectRewriteBackend(inputContext, result);
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

RewriteBackendSelection
ArecaEngine::selectRewriteBackend(fcitx::InputContext &inputContext,
                                  const BambooResult &result) {
  const auto capabilities = inputContext.capabilityFlags();
  const char *forcedUinputReason = nullptr;
  if (requiresUinputForCapabilityMask(capabilities)) {
    forcedUinputReason = "capability-mask-0x72";
  } else if (capabilities.test(fcitx::CapabilityFlag::Terminal)) {
    forcedUinputReason = "terminal-capability";
  }

  if (forcedUinputReason) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: force backend=uinput-socket reason="
                   << forcedUinputReason
                   << " program=" << inputContext.program();
    }
    return {&uinputBackend_};
  }

  auto *state = inputContext.propertyFor(&rewriteStateFactory_);
  if (!state) {
    return {&uinputBackend_};
  }

  const auto decision = reliabilityChecker_.evaluate(
      inputContext, result.currentText, state->surroundingReliability,
      debugEnabled());
  if (decision.browserAutocomplete) {
    const bool isUrl = capabilities.test(fcitx::CapabilityFlag::Url);
    RewriteBackend *backend = &autocompleteForwardBackend_;
    if (browserAutocompleteStrategy(inputContext.program(), isUrl) ==
        BrowserAutocompleteStrategy::EdgeUrlForwardTwo) {
      backend = &autocompleteEdgeForwardBackend_;
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: browser autocomplete strategy=" << backend->name()
                   << " is_url=" << isUrl
                   << " bamboo_delete=" << result.deleteCount;
    }
    return {backend};
  }

  if (decision.useSurrounding) {
    return {&surroundingBackend_};
  }
  return {&uinputBackend_};
}

InputModeHandler &ArecaEngine::activeHandler() {
  switch (activePresentationMode_) {
  case PresentationMode::Preedit:
    return preeditHandler_;
  case PresentationMode::Redirect:
    return redirectHandler_;
  case PresentationMode::Rewrite:
    return rewriteHandler_;
  }
  return rewriteHandler_;
}

const char *ArecaEngine::presentationModeName(PresentationMode mode) {
  switch (mode) {
  case PresentationMode::Rewrite:
    return "Rewrite";
  case PresentationMode::Preedit:
    return "Preedit";
  case PresentationMode::Redirect:
    return "Redirect (EN)";
  }
  return "Rewrite";
}

std::string ArecaEngine::subMode(const fcitx::InputMethodEntry &,
                                 fcitx::InputContext &) {
  return presentationModeName(activePresentationMode_);
}

std::string ArecaEngine::subModeIconImpl(const fcitx::InputMethodEntry &,
                                         fcitx::InputContext &) {
  return "org.fcitx.Fcitx5.fcitx-areca";
}

std::string ArecaEngine::subModeLabelImpl(const fcitx::InputMethodEntry &,
                                          fcitx::InputContext &) {
  return "Ă  " + config_.bambooInputMethod.value() + " \xC2\xB7 " +
         presentationModeName(activePresentationMode_);
}

void ArecaEngine::switchPresentationMode(fcitx::InputContext &inputContext) {
  if (uinputBackend_.hasPending() || scheduler_.rewritePending()) {
    if (debugEnabled()) {
      FCITX_INFO() << "areca: mode hotkey ignored while rewrite pending";
    }
    return;
  }

  const auto current = activePresentationMode_;
  PresentationMode next = PresentationMode::Rewrite;
  switch (current) {
  case PresentationMode::Rewrite:
    next = PresentationMode::Preedit;
    break;
  case PresentationMode::Preedit:
    next = PresentationMode::Redirect;
    break;
  case PresentationMode::Redirect:
    next = PresentationMode::Rewrite;
    break;
  }

  rewriteHandler_.resetContext(inputContext);
  preeditHandler_.resetContext(inputContext);
  config_.presentationMode.setValue(next);
  applyConfig();
  activeHandler().activate(inputContext);
  save();

  if (debugEnabled()) {
    FCITX_INFO() << "areca: switch mode hotkey mode="
                 << presentationModeName(next)
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
                 << presentationModeName(activePresentationMode_)
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
    fcitx::safeSaveAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
    applyConfig();
    return;
  }
  if (path != "macro") {
    return;
  }
  macroTable_.load(config, true);
  fcitx::safeSaveAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::reloadConfig() {
  fcitx::readAsIni(config_, kPkgConfigPath, "conf/areca.conf");
  // Seed the new advanced panel from the legacy fields before loading its own
  // file. Existing installations therefore retain their timing and socket;
  // once the advanced file exists, its values take precedence.
  advancedConfig_.backspaceDelayMs.setValue(
      config_.legacyBackspaceDelayMs.value());
  advancedConfig_.afterBackspaceWaitMs.setValue(
      config_.legacyAfterBackspaceWaitMs.value());
  advancedConfig_.postCommitDelayMs.setValue(
      config_.legacyPostCommitDelayMs.value());
  advancedConfig_.resetDelayMs.setValue(config_.legacyResetDelayMs.value());
  advancedConfig_.socketPath.setValue(config_.legacySocketPath.value());
  fcitx::readAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
  if (advancedConfig_.socketPath.value() == kLegacyDefaultSocketPath) {
    advancedConfig_.socketPath.setValue(kDefaultSocketPath);
  }
  fcitx::readAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  ++macroRevision_;
  applyConfig();
}

void ArecaEngine::save() {
  fcitx::safeSaveAsIni(config_, kPkgConfigPath, "conf/areca.conf");
  fcitx::safeSaveAsIni(macroTable_, kPkgConfigPath, kMacroConfigPath);
  fcitx::safeSaveAsIni(advancedConfig_, kPkgConfigPath, kAdvancedConfigPath);
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
  return {static_cast<uint32_t>(advancedConfig_.backspaceDelayMs.value()),
          static_cast<uint32_t>(advancedConfig_.afterBackspaceWaitMs.value()),
          static_cast<uint32_t>(advancedConfig_.ackFullWaitMs.value()),
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
