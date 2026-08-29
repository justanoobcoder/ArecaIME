#pragma once

#include <string>

namespace areca {

bool isBrowserLikeProgram(const std::string &program);

bool looksLikeBrowserAutocomplete(const std::string &surroundingText,
                                  unsigned int cursor, unsigned int anchor,
                                  const std::string &shownText);

} // namespace areca
