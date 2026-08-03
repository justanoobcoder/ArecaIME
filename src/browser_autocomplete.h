#pragma once

#include <string>

namespace areca {

enum class BrowserAutocompleteStrategy {
  ForwardOne,
  EdgeUrlForwardTwo,
};

bool isBrowserLikeProgram(const std::string &program);

BrowserAutocompleteStrategy
browserAutocompleteStrategy(const std::string &program, bool isUrl);

bool looksLikeBrowserAutocomplete(const std::string &surroundingText,
                                  unsigned int cursor, unsigned int anchor,
                                  const std::string &shownText);

} // namespace areca
