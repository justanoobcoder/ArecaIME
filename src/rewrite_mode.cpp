#include "rewrite_mode.h"

#include <exception>
#include <utility>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>

namespace areca {
namespace {

fcitx::KeySym normalizeRewriteKeypadSym(fcitx::KeySym sym) {
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

bool hasRewriteShortcutModifier(const fcitx::Key &key) {
  const auto states = key.states();
  return states.test(fcitx::KeyState::Ctrl) ||
         states.test(fcitx::KeyState::Alt) ||
         states.test(fcitx::KeyState::Super) ||
         states.test(fcitx::KeyState::Meta) ||
         states.test(fcitx::KeyState::Hyper) ||
         states.test(fcitx::KeyState::Super2) ||
         states.test(fcitx::KeyState::Hyper2);
}

bool isSelectAllShortcut(const fcitx::Key &rawKey) {
  return rawKey.states().test(fcitx::KeyState::Ctrl) &&
         (rawKey.sym() == FcitxKey_a || rawKey.sym() == FcitxKey_A);
}

} // namespace

RewriteInputState::RewriteInputState(std::string inputMethod, bool spellCheck,
                                     bool realtimeSpellcheck,
                                     bool modernStyle,
                                     std::string outputCharset,
                                     bool macroEnabled, bool capitalizeMacro,
                                     uint64_t macroRevision,
                                     std::vector<MacroDefinition> macros)
    : inputMethod(std::move(inputMethod)), spellCheck(spellCheck),
      realtimeSpellcheck(realtimeSpellcheck),
      modernStyle(modernStyle), outputCharset(std::move(outputCharset)),
      macroEnabled(macroEnabled), capitalizeMacro(capitalizeMacro),
      macroRevision(macroRevision),
      engine(std::make_unique<BambooEngineAdapter>(
          this->inputMethod, this->spellCheck, this->realtimeSpellcheck,
          this->modernStyle, this->outputCharset, this->macroEnabled,
          this->capitalizeMacro, std::move(macros))) {}

RewriteModeHandler::RewriteModeHandler(fcitx::EventLoop &eventLoop,
                                       StateFactory &stateFactory,
                                       InputScheduler &scheduler,
                                       BoolProvider autoCapitalizeProvider,
                                       BoolProvider debugProvider,
                                       ResetDelayProvider resetDelayProvider,
                                       BackendVerdictProtector
                                           backendVerdictProtector)
    : eventLoop_(eventLoop), stateFactory_(stateFactory), scheduler_(scheduler),
      autoCapitalizeProvider_(std::move(autoCapitalizeProvider)),
      debugProvider_(std::move(debugProvider)),
      resetDelayProvider_(std::move(resetDelayProvider)),
      backendVerdictProtector_(std::move(backendVerdictProtector)) {}

RewriteModeHandler::~RewriteModeHandler() { lifetime_.reset(); }

RewriteInputState *
RewriteModeHandler::stateFor(fcitx::InputContext &inputContext) const {
  return inputContext.propertyFor(&stateFactory_);
}

void RewriteModeHandler::activate(fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->surroundingReliability.reset();
  }
}

void RewriteModeHandler::deactivate(fcitx::InputContext &inputContext) {
  if (!scheduler_.rewritePending()) {
    resetContext(inputContext);
  }
}

void RewriteModeHandler::handleKeyEvent(fcitx::KeyEvent &event) {
  auto *inputContext = event.inputContext();
  if (!inputContext) {
    return;
  }
  auto *state = stateFor(*inputContext);
  const auto key = event.key();
  const auto normalizedKey = key.normalize();
  const auto rawKey = event.rawKey();
  const auto rawSym = rawKey.sym();
  const bool isBackspace =
      key.check(FcitxKey_BackSpace) || rawKey.check(FcitxKey_BackSpace);
  const auto textSym = normalizeRewriteKeypadSym(normalizedKey.sym());
  const bool isEnter = textSym == FcitxKey_Return ||
                       rawSym == FcitxKey_Return || rawSym == FcitxKey_KP_Enter;

  if (debugProvider_() && (!event.isRelease() || isBackspace || isEnter)) {
    FCITX_INFO() << "areca: rewrite handler key=" << key.toString()
                 << " release=" << event.isRelease()
                 << " pending=" << scheduler_.rewritePending()
                 << " queue=" << scheduler_.queuedKeyCount();
  }
  if (event.isRelease()) {
    return;
  }
  if (rawKey.isModifier()) {
    return;
  }
  if (isBackspace && scheduler_.rewritePending()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: forward pending Backspace without mutating state";
    }
    event.forward();
    return;
  }
  if (inputContext->capabilityFlags().test(fcitx::CapabilityFlag::Password)) {
    cancelProtectedReset(*inputContext);
    resetContext(*inputContext);
    event.forward();
    return;
  }
  if (!state || !state->engine) {
    event.forward();
    return;
  }

  if (isBackspace) {
    backendVerdictProtector_(*inputContext, "user-backspace");
  } else if (key.isCursorMove() || isSelectAllShortcut(rawKey)) {
    backendVerdictProtector_(*inputContext,
                             "selection-or-navigation-shortcut");
  }

  const bool resetAndForward =
      key.isCursorMove() || rawSym == FcitxKey_Tab ||
      rawSym == FcitxKey_KP_Tab || rawSym == FcitxKey_ISO_Left_Tab ||
      rawSym == FcitxKey_Escape || hasRewriteShortcutModifier(rawKey);
  if (resetAndForward) {
    cancelProtectedReset(*inputContext);
    state->sentenceCapitalization.reset();
    state->engine->reset();
    event.forward();
    return;
  }
  if (textSym == FcitxKey_Delete || rawSym == FcitxKey_Delete) {
    state->sentenceCapitalization.reset();
    event.forward();
    return;
  }
  if (isBackspace) {
    cancelProtectedReset(*inputContext);
    state->sentenceCapitalization.reset();
    try {
      state->engine->backspace();
    } catch (const std::exception &error) {
      FCITX_ERROR() << "areca: rewrite Backspace failed: " << error.what();
      state->engine->reset();
    }
    event.forward();
    return;
  }
  if (isEnter) {
    cancelProtectedReset(*inputContext);
    state->sentenceCapitalization.reset();
    state->engine->reset();
    event.forward();
    return;
  }

  auto effectiveTextSym = textSym;
  if (autoCapitalizeProvider_()) {
    effectiveTextSym = capitalizeAfterSentenceBoundary(
        state->sentenceCapitalization, effectiveTextSym);
  } else {
    state->sentenceCapitalization.reset();
  }
  const uint32_t codepoint = fcitx::Key::keySymToUnicode(effectiveTextSym);
  const auto utf8Text = fcitx::Key::keySymToUTF8(effectiveTextSym);
  if (!codepoint || utf8Text.empty()) {
    event.forward();
    return;
  }
  cancelProtectedReset(*inputContext);
  event.filterAndAccept();
  scheduler_.enqueue(*inputContext, codepoint, utf8Text);
}

void RewriteModeHandler::requestProtectedReset(
    fcitx::InputContext &inputContext) {
  cancelProtectedReset(inputContext);
  if (scheduler_.shouldRejectReset()) {
    if (debugProvider_()) {
      FCITX_INFO() << "areca: protected reset rejected (active rewrite or 50ms post-commit window)";
    }
    return;
  }
  if (auto *state = stateFor(inputContext)) {
    scheduleProtectedReset(inputContext, *state);
  }
}

void RewriteModeHandler::scheduleProtectedReset(
    fcitx::InputContext &inputContext, RewriteInputState &state) {
  state.delayedResetTimer.reset();
  const auto inputContextRef = inputContext.watch();
  const std::weak_ptr<void> lifetime = lifetime_;
  const uint64_t deadline = fcitx::now(CLOCK_MONOTONIC) +
                            static_cast<uint64_t>(resetDelayProvider_()) * 1000;
  state.delayedResetTimer = eventLoop_.addTimeEvent(
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
          scheduleProtectedReset(*inputContext, *state);
          return false;
        }
        resetContext(*inputContext);
        return false;
      });
  if (state.delayedResetTimer) {
    state.delayedResetTimer->setOneShot();
  }
}

void RewriteModeHandler::cancelProtectedReset(
    fcitx::InputContext &inputContext) {
  if (auto *state = stateFor(inputContext)) {
    state->delayedResetTimer.reset();
  }
}

void RewriteModeHandler::resetContext(fcitx::InputContext &inputContext) {
  scheduler_.resetContext(inputContext);
  if (auto *state = stateFor(inputContext)) {
    state->delayedResetTimer.reset();
    state->sentenceCapitalization.reset();
  }
}

} // namespace areca
