#include <cassert>
#include <algorithm>
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

  areca::BambooEngineAdapter modernToneEngine("Telex 2", true, true);
  areca::BambooEngineAdapter traditionalToneEngine("Telex 2", true, false);
  areca::BambooResult modernTone;
  areca::BambooResult traditionalTone;
  for (char key : std::string("hoaf")) {
    modernTone = type(modernToneEngine, key);
    traditionalTone = type(traditionalToneEngine, key);
  }
  assert(modernTone.newText == "hoà");
  assert(traditionalTone.newText == "hòa");

  const auto inputMethods = areca::BambooEngineAdapter::inputMethodNames();
  assert(std::find(inputMethods.begin(), inputMethods.end(), "VNI") !=
         inputMethods.end());
  assert(std::find(inputMethods.begin(), inputMethods.end(), "VIQR") !=
         inputMethods.end());
  for (const auto &inputMethod : inputMethods) {
    areca::BambooEngineAdapter availableMethod(inputMethod);
    assert(availableMethod.valid());
  }
  const auto charsets = areca::BambooEngineAdapter::charsetNames();
  assert(!charsets.empty() && charsets.front() == "Unicode");
  assert(std::find(charsets.begin(), charsets.end(), "Unicode tổ hợp") !=
         charsets.end());
  for (const auto &charset : charsets) {
    areca::BambooEngineAdapter encodedEngine("Telex 2", true, true, charset);
    type(encodedEngine, 'a');
    const auto encodedResult = type(encodedEngine, 's');
    assert(fcitx::utf8::validate(encodedResult.newText));
  }

  areca::BambooEngineAdapter vniEngine("VNI");
  areca::BambooResult vniResult;
  for (char key : std::string("a61")) {
    vniResult = type(vniEngine, key);
  }
  assert(vniResult.newText == "ấ");

  areca::BambooEngineAdapter combiningEngine(
      "Telex 2", true, true, "Unicode tổ hợp");
  type(combiningEngine, 'a');
  const auto combiningResult = type(combiningEngine, 's');
  assert(combiningResult.newText == "a\u0301");
  assert(combiningResult.deleteCount == 0);
  assert(combiningResult.commitText == "\u0301");

  std::cout << "Bamboo adapter tests passed\n";
  return 0;
}
