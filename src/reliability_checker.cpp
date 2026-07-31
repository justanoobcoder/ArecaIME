#include "reliability_checker.h"

#include <algorithm>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "browser_autocomplete.h"
#include "word_segment.h"

namespace areca {
namespace {

constexpr size_t kMinMatch = 1;

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

  // Probe exactly once, on the first rewrite for this input context. The text
  // Bamboo believes is currently visible must match the suffix immediately
  // before the application cursor. An autocomplete snapshot is not a valid
  // probe: leave known=false so a later ordinary rewrite can decide.
  if (!state.known && !browserAutocomplete) {
    if (inputContext.capabilityFlags().test(
            fcitx::CapabilityFlag::SurroundingText)) {
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
            FCITX_INFO()
                << "areca: reliability first-probe word=" << segment.word
                << " shown=" << shownText << " reliable=" << state.reliable;
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
    // Absence of the capability is also a complete first verdict. Cache the
    // false result instead of attempting the same probe on every rewrite.
    state.known = true;
  }

  if (debug) {
    FCITX_INFO() << "areca: reliability cached known=" << state.known
                 << " reliable=" << state.reliable
                 << " browser_autocomplete=" << browserAutocomplete
                 << " program=" << inputContext.program();
  }

  ReliabilityDecision decision;
  decision.browserAutocomplete = browserAutocomplete;
  decision.useSurrounding =
      state.known && state.reliable && !browserAutocomplete;
  // OpenKey sends one real Backspace for the selected inline suggestion. The
  // socket server independently appends its own sentinel Backspace.
  decision.additionalFallbackBackspaces = browserAutocomplete ? 1 : 0;
  return decision;
}

} // namespace areca
