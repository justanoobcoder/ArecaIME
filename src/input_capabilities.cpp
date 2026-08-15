#include "input_capabilities.h"

#include <algorithm>
#include <array>

namespace areca {
namespace {

constexpr uint64_t kForwardBackspaceCapabilityMask = 0x72;

std::string normalizedProgramName(std::string program) {
  const auto slash = program.find_last_of('/');
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  std::transform(program.begin(), program.end(), program.begin(), [](char c) {
    if (c >= 'A' && c <= 'Z') {
      return static_cast<char>(c - 'A' + 'a');
    }
    return c;
  });

  constexpr const char suffix[] = ".desktop";
  constexpr size_t suffixLength = sizeof(suffix) - 1;
  if (program.size() >= suffixLength &&
      program.compare(program.size() - suffixLength, suffixLength, suffix) ==
          0) {
    program.resize(program.size() - suffixLength);
  }
  return program;
}

} // namespace

bool isVSCodeFamilyProgram(const std::string &rawProgram) {
  const std::string program = normalizedProgramName(rawProgram);
  if (program == "code" || program == "code-insiders") {
    return true;
  }

  static constexpr std::array<const char *, 10> patterns = {
      "code-oss", "visual-studio-code", "vscode",      "vscodium", "codium",
      "cursor",   "windsurf",           "antigravity", "trae",     "positron"};
  return std::any_of(patterns.begin(), patterns.end(),
                     [&program](const char *pattern) {
                       return program.find(pattern) != std::string::npos;
                     });
}

bool requiresForwardBackspaceForCapabilityMask(fcitx::CapabilityFlags flags,
                                               const std::string &program) {
  return flags.toInteger() == kForwardBackspaceCapabilityMask &&
         isVSCodeFamilyProgram(program);
}

} // namespace areca
