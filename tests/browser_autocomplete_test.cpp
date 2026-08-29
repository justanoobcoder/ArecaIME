#include <cassert>
#include <iostream>

#include "browser_autocomplete.h"

int main() {
  using areca::looksLikeBrowserAutocomplete;

  assert(areca::isBrowserLikeProgram("google-chrome"));
  assert(areca::isBrowserLikeProgram("/usr/bin/firefox.desktop"));
  assert(areca::isBrowserLikeProgram(""));
  assert(!areca::isBrowserLikeProgram("org.kde.kate"));
  // User typed "go" and the browser selected "ogle" through line end.
  assert(looksLikeBrowserAutocomplete("google", 2, 6, "go"));

  // No selection and no appended suffix is normal surrounding text.
  assert(!looksLikeBrowserAutocomplete("go", 2, 2, "go"));

  // OpenKey case 2: no selection, but autocomplete appended text after cursor.
  assert(looksLikeBrowserAutocomplete("google", 2, 2, "go"));
  assert(!looksLikeBrowserAutocomplete("goo", 2, 2, "go"));
  assert(!looksLikeBrowserAutocomplete("go\nogle", 2, 2, "go"));

  // Selection not extending to line end is not browser inline autocomplete.
  assert(!looksLikeBrowserAutocomplete("google more", 2, 6, "go"));

  // The shown composition must be the suffix immediately before cursor.
  assert(!looksLikeBrowserAutocomplete("xxgoogle", 3, 8, "go"));

  // Reverse cursor/anchor direction is handled like OpenKey.
  assert(looksLikeBrowserAutocomplete("google", 6, 2, "go"));

  // Autocomplete never spans a newline.
  assert(!looksLikeBrowserAutocomplete("go\nogle", 2, 7, "go"));

  std::cout << "Browser autocomplete tests passed\n";
  return 0;
}
