#include "reliability_checker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontext.h>
#include <fcitx/surroundingtext.h>

#include "browser_autocomplete.h"
#include "word_segment.h"

namespace areca {
namespace {

constexpr size_t kMinMatch = 1;
constexpr uint64_t kForwardBackspaceCapabilityMask = 0x72;

std::string normalizedProgramName(std::string program) {
  const auto slash = program.find_last_of('/');
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  std::transform(program.begin(), program.end(), program.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  constexpr std::string_view desktopSuffix = ".desktop";
  if (program.ends_with(desktopSuffix)) {
    program.resize(program.size() - desktopSuffix.size());
  }
  return program;
}

bool isVSCodeFamilyProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  if (program == "code" || program.starts_with("code-")) {
    return true;
  }

  static constexpr std::array<std::string_view, 10> markers = {
      "visual-studio-code", "visualstudio.code", "vscode", "codium",
      "cursor",             "windsurf",          "antigravity",
      "positron",           "pearai",            "trae"};
  return std::any_of(markers.begin(), markers.end(),
                     [&program](std::string_view marker) {
                       return program.find(marker) != std::string::npos;
                     }) ||
         program == "kiro" || program.starts_with("kiro-") ||
         program == "void" || program.starts_with("void-");
}

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
    state.forceForwardBackspace =
        state.reliable &&
        capabilities.toInteger() == kForwardBackspaceCapabilityMask &&
        isVSCodeFamilyProgram(inputContext.program());
    if (state.forceForwardBackspace && debug) {
      FCITX_INFO() << "areca: reliability first-probe force_forward=1"
                   << " reason=vscode-family-reliable-capability-mask-0x72";
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
