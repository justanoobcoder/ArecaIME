#include <cassert>
#include <iostream>

#include <fcitx-utils/keysym.h>

#include "sentence_capitalizer.h"

int main() {
  areca::SentenceCapitalizationState state;

  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_period) ==
         FcitxKey_period);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_space) ==
         FcitxKey_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_space) ==
         FcitxKey_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_a) ==
         FcitxKey_A);
  assert(!state.capitalizeNextLetter);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_question);
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_b) ==
         FcitxKey_B);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_period);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_c) ==
         FcitxKey_c);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_Return);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_d) ==
         FcitxKey_D);

  state.reset();
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_exclam);
  areca::capitalizeAfterSentenceBoundary(state, FcitxKey_space);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_BackSpace) ==
         FcitxKey_BackSpace);
  assert(areca::capitalizeAfterSentenceBoundary(state, FcitxKey_e) ==
         FcitxKey_e);

  std::cout << "Sentence capitalizer tests passed\n";
  return 0;
}
