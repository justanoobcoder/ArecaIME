#include "bamboo_engine_adapter.h"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fcitx-utils/utf8.h>

extern "C" {
uint64_t ArecaBambooCreate(char *inputMethod, int modernStyle);
void ArecaBambooDestroy(uint64_t id);
int ArecaBambooCanProcess(uint64_t id, uint32_t key);
char *ArecaBambooProcess(uint64_t id, uint32_t key);
char *ArecaBambooFinalizeWord(uint64_t id, int spellCheck);
char *ArecaBambooBackspace(uint64_t id);
void ArecaBambooReset(uint64_t id);
char *ArecaBambooInputMethodNames();
char *ArecaBambooCharsetNames();
char *ArecaBambooEncode(char *charset, char *input);
}

namespace areca {
namespace {

std::vector<std::pair<uint32_t, size_t>> codepoints(const std::string &text) {
  std::vector<std::pair<uint32_t, size_t>> result;
  auto it = text.begin();
  while (it != text.end()) {
    const auto byteOffset = static_cast<size_t>(it - text.begin());
    uint32_t codepoint = 0;
    auto next = fcitx::utf8::getNextChar(it, text.end(), &codepoint);
    if (!fcitx::utf8::isValidChar(codepoint)) {
      throw std::runtime_error("Bamboo returned invalid UTF-8");
    }
    result.emplace_back(codepoint, byteOffset);
    it = next;
  }
  result.emplace_back(0, text.size());
  return result;
}

std::vector<std::string> splitLines(char *raw) {
  if (!raw) {
    return {};
  }
  std::string joined(raw);
  std::free(raw);
  std::vector<std::string> result;
  size_t begin = 0;
  while (begin <= joined.size()) {
    const auto end = joined.find('\n', begin);
    const auto value = joined.substr(begin, end - begin);
    if (!value.empty()) {
      result.push_back(value);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return result;
}

} // namespace

BambooEngineAdapter::BambooEngineAdapter(std::string inputMethod,
                                         bool spellCheck, bool modernStyle,
                                         std::string outputCharset)
    : spellCheck_(spellCheck), outputCharset_(std::move(outputCharset)) {
  handle_ = ArecaBambooCreate(inputMethod.data(), modernStyle ? 1 : 0);
  if (!handle_) {
    throw std::runtime_error("unknown Bamboo input method: " + inputMethod);
  }
}

BambooEngineAdapter::~BambooEngineAdapter() {
  if (handle_) {
    ArecaBambooDestroy(handle_);
  }
}

BambooResult BambooEngineAdapter::process(uint32_t codepoint,
                                          const std::string &utf8Text) {
  BambooResult result;
  result.currentText = renderedText_;

  // Finalize the current word before committing a boundary. With spell check
  // enabled, Bamboo restores an invalid Vietnamese-looking word to the raw
  // Latin keystrokes that produced it.
  if (!ArecaBambooCanProcess(handle_, codepoint)) {
    char *raw = ArecaBambooFinalizeWord(handle_, spellCheck_ ? 1 : 0);
    if (!raw) {
      throw std::runtime_error("Bamboo word finalization failed");
    }
    std::string next(raw);
    std::free(raw);
    next += utf8Text;
    next = encode(next);

    const auto oldChars = codepoints(renderedText_);
    const auto newChars = codepoints(next);
    size_t prefix = 0;
    while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
           oldChars[prefix].first == newChars[prefix].first) {
      ++prefix;
    }
    result.deleteCount =
        static_cast<uint32_t>((oldChars.size() - 1) - prefix);
    result.commitText = next.substr(newChars[prefix].second);
    renderedText_.clear();
    result.newText.clear();
    return result;
  }

  char *raw = ArecaBambooProcess(handle_, codepoint);
  if (!raw) {
    throw std::runtime_error("Bamboo processing failed");
  }
  std::string next = encode(raw);
  std::free(raw);

  const auto oldChars = codepoints(renderedText_);
  const auto newChars = codepoints(next);
  size_t prefix = 0;
  while (prefix + 1 < oldChars.size() && prefix + 1 < newChars.size() &&
         oldChars[prefix].first == newChars[prefix].first) {
    ++prefix;
  }

  result.deleteCount = static_cast<uint32_t>((oldChars.size() - 1) - prefix);
  result.commitText = next.substr(newChars[prefix].second);
  renderedText_ = std::move(next);
  result.newText = renderedText_;
  return result;
}

void BambooEngineAdapter::reset() {
  ArecaBambooReset(handle_);
  renderedText_.clear();
}

void BambooEngineAdapter::backspace() {
  char *raw = ArecaBambooBackspace(handle_);
  if (!raw) {
    throw std::runtime_error("Bamboo Backspace processing failed");
  }
  renderedText_ = encode(raw);
  std::free(raw);
}

std::vector<std::string> BambooEngineAdapter::inputMethodNames() {
  return splitLines(ArecaBambooInputMethodNames());
}

std::vector<std::string> BambooEngineAdapter::charsetNames() {
  return splitLines(ArecaBambooCharsetNames());
}

std::string BambooEngineAdapter::encode(const std::string &text) const {
  char *raw = ArecaBambooEncode(const_cast<char *>(outputCharset_.c_str()),
                                const_cast<char *>(text.c_str()));
  if (!raw) {
    throw std::runtime_error("Bamboo output encoding failed");
  }
  std::string encoded(raw);
  std::free(raw);
  return encoded;
}

} // namespace areca
