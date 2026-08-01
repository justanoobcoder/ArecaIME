#include "areca_engine.h"

#include <exception>
#include <utility>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>

namespace areca {
namespace {

fcitx::KeySym normalizeKeypadKeySym(fcitx::KeySym sym) {
  if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) {
    return static_cast<fcitx::KeySym>(FcitxKey_0 + (sym - FcitxKey_KP_0));
  }
  switch (sym) {
  case FcitxKey_KP_Add:
    return FcitxKey_plus;
  case FcitxKey_KP_Subtract:
    return FcitxKey_minus;
  case FcitxKey_KP_Divide:
    return FcitxKey_slash;
  case FcitxKey_KP_Multiply:
    return FcitxKey_asterisk;
  case FcitxKey_KP_Decimal:
    return FcitxKey_period;
  case FcitxKey_KP_Enter:
    return FcitxKey_Return;
  case FcitxKey_KP_Equal:
    return FcitxKey_equal;
  case FcitxKey_KP_Space:
    return FcitxKey_space;
  default:
    return sym;
  }
}

bool hasCtrlAltSuperMeta(const fcitx::Key &key) {
  const auto states = key.states();
  return states.test(fcitx::KeyState::Ctrl) ||
         states.test(fcitx::KeyState::Alt) ||
         states.test(fcitx::KeyState::Super) ||
         states.test(fcitx::KeyState::Meta) ||
         states.test(fcitx::KeyState::Hyper) ||
         states.test(fcitx::KeyState::Super2) ||
         states.test(fcitx::KeyState::Hyper2);
}

} // namespace

ArecaEngine::ArecaEngine(fcitx::Instance *instance)
    : instance_(instance), stateFactory_([this](fcitx::InputContext &) {
        return new InputState(config_.bambooInputMethod.value(),
                              config_.spellCheck.value());
      }),
      uinputBackend_(instance_->eventLoop(), config_.socketPath.value()),
      scheduler_(
          instance_->eventLoop(),
          [this](fcitx::InputContext &inputContext) -> VietnameseEngine * {
            auto *state = stateFor(inputContext);
            return state ? state->engine.get() : nullptr;
          },
          [this]() { return timing(); }, [this]() { return debugEnabled(); },
          surroundingBackend_,
          [this](fcitx::InputContext &inputContext,
                 const BambooResult &result) -> RewriteBackendSelection {
            auto *state = stateFor(inputContext);
            if (!state) {
              return {&uinputBackend_, 0};
            }
            const auto decision = reliabilityChecker_.evaluate(
                inputContext, result.currentText,
                state->surroundingReliability, debugEnabled());
            if (decision.useSurrounding) {
              return {&surroundingBackend_, 0};
            }
            return {&uinputBackend_,
                    decision.additionalFallbackBackspaces};
          }) {
  instance_->inputContextManager().registerProperty("arecaState",
                                                    &stateFactory_);
  inputMode_ = std::make_unique<QueuedRewriteMode>(scheduler_);
  reloadConfig();
}

ArecaEngine::~ArecaEngine() { lifetime_.reset(); }

void ArecaEngine::activate(const fcitx::InputMethodEntry &,
                           fcitx::InputContextEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  auto *state = stateFor(*inputContext);
  if (!state) {
    return;
  }

  // Match OpenKey's one-probe-per-input-field lifetime. No ordinary reset()
  // callback clears this verdict; noisy app resets remain protected below.
  state->surroundingReliability.reset();
  if (debugEnabled()) {
    FCITX_INFO() << "areca: activate; reliability verdict cleared program="
                 << inputContext->program();
  }
}

void ArecaEngine::keyEvent(const fcitx::InputMethodEntry &,
                           fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  auto *state = stateFor(*inputContext);
  const auto key = event.key();
  const auto normalizedKey = key.normalize();
  const auto rawKey = event.rawKey();
  const auto rawSym = event.rawKey().sym();
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const auto textSym = normalizeKeypadKeySym(normalizedKey.sym());
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return ||
                       rawSym == FcitxKey_KP_Enter;

  if (debugEnabled() && (!event.isRelease() || isBackspace || isEnter)) {
    FCITX_INFO() << "areca: key event key=" << key.toString()
                 << " release=" << event.isRelease()
                 << " backspace=" << isBackspace
                 << " enter=" << isEnter
                 << " rewrite_pending=" << uinputBackend_.hasPending()
                 << " queue_depth=" << scheduler_.queuedKeyCount();
  }

  if (event.isRelease()) {
    if (isBackspace && uinputBackend_.handleInjectedBackspaceRelease()) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: filter sentinel Backspace release";
      }
      event.filterAndAccept();
      return;
    }
    return;
  }

  if (rawKey.isModifier()) {
    return;
  }

  // Injected uinput Backspaces are the only special keys consumed here. Other
  // special keys follow the native forwarding policy below.
  if (isBackspace) {
    const auto injectedAction = uinputBackend_.handleInjectedBackspacePress();
    if (injectedAction ==
        UinputSocketBackend::InjectedBackspaceAction::PassToApplication) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: pass injected Backspace to application";
      }
      return;
    }
    if (injectedAction ==
        UinputSocketBackend::InjectedBackspaceAction::Filter) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: filter injected sentinel Backspace press";
      }
      event.filterAndAccept();
      return;
    }
  }

  // Match OpenKey's password policy: never expose password input to Bamboo or
  // either rewrite backend. Clear any queued/composition state belonging to
  // this input context, then forward the original event unchanged.
  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    cancelProtectedStateReset(*inputContext);
    inputMode_->reset(*inputContext);
    if (debugEnabled()) {
      FCITX_INFO() << "areca: password field; forward key directly key="
                   << event.rawKey().toString();
    }
    event.forward();
    return;
  }

  const bool resetAndForward =
      key.isCursorMove() || rawSym == FcitxKey_Tab ||
      rawSym == FcitxKey_KP_Tab || rawSym == FcitxKey_ISO_Left_Tab ||
      rawSym == FcitxKey_Escape || hasCtrlAltSuperMeta(rawKey);
  if (resetAndForward) {
    cancelProtectedStateReset(*inputContext);
    if (state && state->engine) {
      state->engine->reset();
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: reset Bamboo and forward special key="
                   << event.rawKey().toString();
    }
    event.forward();
    return;
  }

  // Delete is forwarded without changing the Bamboo history.
  if (textSym == FcitxKey_Delete || rawSym == FcitxKey_Delete) {
    event.forward();
    return;
  }

  // Handle Backspace and Return explicitly: update/reset Bamboo and forward
  // the original event instead of converting either key to text.
  if (isBackspace) {
    cancelProtectedStateReset(*inputContext);
    if (state && state->engine) {
      try {
        state->engine->backspace();
      } catch (const std::exception &error) {
        FCITX_ERROR() << "areca: Bamboo Backspace failed: " << error.what();
        state->engine->reset();
      }
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: update Bamboo and forward Backspace";
    }
    event.forward();
    return;
  }

  if (isEnter) {
    cancelProtectedStateReset(*inputContext);
    if (state && state->engine) {
      state->engine->reset();
    }
    if (debugEnabled()) {
      FCITX_INFO() << "areca: reset Bamboo and forward Enter key="
                   << event.rawKey().toString();
    }
    event.forward();
    return;
  }

  // Match OpenKey's text path: normalize the logical Fcitx key first, then
  // derive both the codepoint and UTF-8 from that same keysym. rawKey remains
  // reserved for special-key recognition and forwarding the original event.
  const uint32_t codepoint = fcitx::Key::keySymToUnicode(textSym);
  const auto utf8Text = fcitx::Key::keySymToUTF8(textSym);
  if (!codepoint || utf8Text.empty()) {
    event.forward();
    return;
  }

  if (debugEnabled()) {
    FCITX_INFO() << "areca: normalized text raw=" << rawKey.toString()
                 << " logical=" << key.toString()
                 << " normalized=" << normalizedKey.toString()
                 << " utf8=" << utf8Text << " codepoint=" << codepoint;
  }

  // A real text key proves that the app's reset was transient. Preserve the
  // Bamboo/queue state and cancel the lazy reset before enqueueing this key.
  cancelProtectedStateReset(*inputContext);

  // Accept synchronously so the frontend never forwards this text key. The
  // actual Bamboo processing and commit happen later through the queue.
  event.filterAndAccept();
  inputMode_->handleTextKey(*inputContext, rawKey, codepoint, utf8Text);
  if (debugEnabled()) {
    FCITX_INFO() << "areca: filter and queue text key text=" << utf8Text;
  }
}

void ArecaEngine::reset(const fcitx::InputMethodEntry &,
                        fcitx::InputContextEvent &event) {
  if (auto *inputContext = event.inputContext()) {
    if (auto *state = stateFor(*inputContext)) {
      if (debugEnabled()) {
        FCITX_INFO() << "areca: app reset requested; arm protected reset";
      }
      scheduleProtectedStateReset(*inputContext, *state);
    }
  }
}

void ArecaEngine::scheduleProtectedStateReset(fcitx::InputContext &inputContext,
                                              InputState &state) {
  // Repeated reset requests restart the quiet window. The actual state reset
  // therefore happens ResetDelayMs after the last reset request, provided no text
  // key arrives in between.
  state.delayedResetTimer.reset();
  const auto inputContextRef = inputContext.watch();
  const std::weak_ptr<void> lifetime = lifetime_;
  const uint64_t deadline =
      fcitx::now(CLOCK_MONOTONIC) +
      static_cast<uint64_t>(config_.resetDelayMs.value()) * 1000;

  state.delayedResetTimer = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, deadline, 0,
      [this, inputContextRef, lifetime](fcitx::EventSourceTime *, uint64_t) {
        if (lifetime.expired()) {
          return false;
        }
        auto *inputContext = inputContextRef.get();
        if (!inputContext) {
          return false;
        }
        auto *state = stateFor(*inputContext);
        if (!state) {
          return false;
        }
        auto completedTimer = std::move(state->delayedResetTimer);
        if (scheduler_.rewritePending()) {
          // Never tear down Bamboo/mode/queue state in the middle of a remote
          // transaction. Start a fresh quiet window after the pending rewrite.
          scheduleProtectedStateReset(*inputContext, *state);
          if (debugEnabled()) {
            FCITX_INFO()
                << "areca: protected reset deferred; rewrite still pending";
          }
          return false;
        }
        performContextStateReset(*inputContext, *state);
        if (debugEnabled()) {
          FCITX_INFO() << "areca: protected reset executed after quiet window";
        }
        return false;
      });
  state.delayedResetTimer->setOneShot();
}

void ArecaEngine::cancelProtectedStateReset(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    if (debugEnabled() && state->delayedResetTimer) {
      FCITX_INFO() << "areca: protected reset cancelled by input";
    }
    state->delayedResetTimer.reset();
  }
}

void ArecaEngine::performContextStateReset(fcitx::InputContext &inputContext,
                                           InputState &) {
  // This is the only reset entry point for mutable typing state. The active
  // mode owns all of its composition/queue/backend-facing state, so future
  // PreeditOnly or SurroundingOnly modes inherit the same protection barrier.
  inputMode_->reset(inputContext);

  // surroundingReliability deliberately survives: it is the cached verdict
  // for this input field, not transient composition state.
}

const fcitx::Configuration *ArecaEngine::getConfig() const { return &config_; }

void ArecaEngine::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  applyConfig();
  save();
}

void ArecaEngine::reloadConfig() {
  fcitx::readAsIni(config_, fcitx::StandardPathsType::PkgConfig,
                   "conf/areca.conf");
  applyConfig();
}

void ArecaEngine::save() {
  fcitx::safeSaveAsIni(config_, fcitx::StandardPathsType::PkgConfig,
                       "conf/areca.conf");
}

InputState *ArecaEngine::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

SchedulerTiming ArecaEngine::timing() const {
  return {static_cast<uint32_t>(config_.keyIntervalMs.value()),
          static_cast<uint32_t>(config_.backspaceDelayMs.value()),
          static_cast<uint32_t>(config_.afterBackspaceWaitMs.value()),
          static_cast<uint32_t>(config_.postCommitDelayMs.value())};
}

void ArecaEngine::applyConfig() {
  uinputBackend_.setSocketPath(config_.socketPath.value());
  uinputBackend_.setDebug(debugEnabled());
  if (uinputBackend_.hasPending()) {
    return;
  }

  const auto inputMethod = config_.bambooInputMethod.value();
  const bool spellCheck = config_.spellCheck.value();
  instance_->inputContextManager().foreach (
      [this, &inputMethod, spellCheck](fcitx::InputContext *inputContext) {
        auto *state = stateFor(*inputContext);
        if (state && (state->inputMethod != inputMethod ||
                      state->spellCheck != spellCheck)) {
          try {
            scheduler_.resetContext(*inputContext);
            state->engine = std::make_unique<BambooEngineAdapter>(
                inputMethod, spellCheck);
            state->inputMethod = inputMethod;
            state->spellCheck = spellCheck;
          } catch (const std::exception &error) {
            FCITX_ERROR() << "areca: cannot select Bamboo method: "
                          << error.what();
          }
        }
        return true;
      });
}

class ArecaEngineFactory final : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new ArecaEngine(manager->instance());
  }
};

} // namespace areca

FCITX_ADDON_FACTORY(areca::ArecaEngineFactory)
