#include "reliability_checker.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "browser_autocomplete.h"
#include "program_compatibility.h"
#include "word_segment.h"

namespace areca {
namespace {

constexpr size_t kMinMatch = 1;
constexpr uint64_t kForwardBackspaceCapabilityMask = 0x72;

} // namespace

ReliabilityDecision ReliabilityChecker::evaluate(
    fcitx::InputContext &inputContext, const std::string &shownText,
    SurroundingReliabilityState &state, bool debug) const {
  const auto &surrounding = inputContext.surroundingText();
  const bool browserAutocomplete =
      isBrowserLikeProgram(inputContext.program()) &&
      inputContext.capabilityFlags().test(
          fcitx::CapabilityFlag::SurroundingText) &&
      surrounding.isValid() &&
      looksLikeBrowserAutocomplete(surrounding.text(), surrounding.cursor(),
                                   surrounding.anchor(), shownText);

  // Decide exactly once, on the first rewrite for this input context. Probe
  // SurroundingText first, then apply the exact-mask policy only to a reliable
  // result. An autocomplete snapshot is not a valid first decision: leave
  // known=false so a later ordinary rewrite can decide.
  if (!state.known && !browserAutocomplete) {
    const auto capabilities = inputContext.capabilityFlags();
    if (capabilities.test(fcitx::CapabilityFlag::SurroundingText)) {
      if (surrounding.isValid()) {
        WordSegment segment;
        if (extractWordBeforeCursor(surrounding.text(), surrounding.cursor(),
                                    segment)) {
          const size_t n = std::min(segment.word.size(), shownText.size());
          if (n >= kMinMatch &&
              segment.word.compare(segment.word.size() - n, n, shownText,
                                   shownText.size() - n, n) == 0) {
            state.reliable = true;
          }
          if (debug) {
            FCITX_INFO() << "areca: reliability first-probe word="
                         << segment.word << " shown=" << shownText
                         << " reliable=" << state.reliable;
          }
        } else if (debug) {
          FCITX_INFO() << "areca: reliability first-probe no word";
        }
      } else if (debug) {
        FCITX_INFO() << "areca: reliability first-probe invalid surrounding";
      }
    } else if (debug) {
      FCITX_INFO()
          << "areca: reliability first-probe no surrounding capability";
    }
    state.forceForwardBackspace = false;
    if (isFirefoxFamilyProgram(inputContext.program())) {
      state.forceForwardBackspace = true;
      if (debug) {
        FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=firefox-family-no-dbus-delete-surrounding";
      }
    } else if (isVSCodeFamilyProgram(inputContext.program())) {
      const uint64_t capabilityMask = capabilities.toInteger();
      state.forceForwardBackspace =
          capabilityMask == kForwardBackspaceCapabilityMask;
      if (state.forceForwardBackspace && debug) {
        FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                     << " reason=program-compatibility-capability-mask-0x72";
      }
    }
    // Absence of the capability is also a complete first verdict. Cache the
    // false result instead of attempting the same probe on every rewrite.
    state.known = true;
  }

  if (debug) {
    FCITX_INFO() << "areca: reliability cached known=" << state.known
                 << " reliable=" << state.reliable
                 << " force_forward=" << state.forceForwardBackspace
                 << " browser_autocomplete=" << browserAutocomplete
                 << " program=" << inputContext.program();
  }

  ReliabilityDecision decision;
  decision.browserAutocomplete = browserAutocomplete;
  decision.useSurrounding = state.known && state.reliable &&
                            !state.forceForwardBackspace &&
                            !browserAutocomplete;
  return decision;
}

} // namespace areca
