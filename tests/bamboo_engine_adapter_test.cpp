#include <cassert>
#include <iostream>
#include <string>

#include <fcitx-utils/utf8.h>

#include "bamboo_engine_adapter.h"

namespace {

areca::BambooResult type(areca::BambooEngineAdapter &engine, char key) {
  return engine.process(static_cast<unsigned char>(key), std::string(1, key));
}

} // namespace

int main() {
  areca::BambooEngineAdapter engine("Telex 2");

  auto a = type(engine, 'a');
  assert(a.currentText.empty());
  assert(a.newText == "a");
  assert(a.deleteCount == 0);
  assert(a.commitText == "a");

  auto w = type(engine, 'w');
  assert(w.currentText == "a");
  assert(w.newText == "ă");
  assert(w.deleteCount == 1);
  assert(w.commitText == "ă");

  engine.backspace();
  engine.reset();
  a = type(engine, 'a');
  w = type(engine, 'w');

  auto n = type(engine, 'n');
  assert(n.currentText == "ă");
  assert(n.newText == "ăn");
  assert(n.deleteCount == 0);
  assert(n.commitText == "n");

  auto space = type(engine, ' ');
  assert(space.currentText == "ăn");
  assert(space.newText.empty());
  assert(space.deleteCount == 0);
  assert(space.commitText == " ");

  engine.reset();
  std::string display;
  for (char key : std::string("chuaarn")) {
    const auto result = type(engine, key);
    const auto displayLength = fcitx::utf8::length(display);
    assert(displayLength >= result.deleteCount);
    if (result.deleteCount) {
      const auto eraseFrom = fcitx::utf8::nextNChar(
          display.begin(), displayLength - result.deleteCount);
      display.erase(eraseFrom, display.end());
    }
    display += result.commitText;
  }
  assert(display == "chuẩn");

  // At a word boundary, spell check restores an invalid Vietnamese-looking
  // syllable to the original Latin keystrokes before appending the boundary.
  engine.reset();
  display.clear();
  for (char key : std::string("awbc")) {
    const auto result = type(engine, key);
    const auto displayLength = fcitx::utf8::length(display);
    const auto eraseFrom = fcitx::utf8::nextNChar(
        display.begin(), displayLength - result.deleteCount);
    display.erase(eraseFrom, display.end());
    display += result.commitText;
  }
  assert(display == "ăbc");
  auto checkedBoundary = type(engine, ' ');
  assert(checkedBoundary.currentText == "ăbc");
  assert(checkedBoundary.deleteCount == 3);
  assert(checkedBoundary.commitText == "awbc ");

  areca::BambooEngineAdapter uncheckedEngine("Telex 2", false);
  for (char key : std::string("awbc")) {
    type(uncheckedEngine, key);
  }
  auto uncheckedBoundary = type(uncheckedEngine, ' ');
  assert(uncheckedBoundary.currentText == "ăbc");
  assert(uncheckedBoundary.deleteCount == 0);
  assert(uncheckedBoundary.commitText == " ");

  std::cout << "Bamboo adapter tests passed\n";
  return 0;
}
