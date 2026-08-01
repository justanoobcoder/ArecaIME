#include <cassert>
#include <iostream>

#include "preedit_logic.h"

int main() {
  areca::BambooResult unchangedBoundary;
  unchangedBoundary.currentText = "ăn";
  unchangedBoundary.commitText = " ";
  assert(areca::buildPreeditCommit(unchangedBoundary) == "ăn ");

  areca::BambooResult rewrittenBoundary;
  rewrittenBoundary.currentText = "ăbc";
  rewrittenBoundary.deleteCount = 3;
  rewrittenBoundary.commitText = "awbc ";
  assert(areca::buildPreeditCommit(rewrittenBoundary) == "awbc ");

  areca::BambooResult partialDelta;
  partialDelta.currentText = "chào";
  partialDelta.deleteCount = 2;
  partialDelta.commitText = "ao.";
  assert(areca::buildPreeditCommit(partialDelta) == "chao.");

  areca::BambooResult emptyComposition;
  emptyComposition.commitText = ".";
  assert(areca::buildPreeditCommit(emptyComposition) == ".");

  std::cout << "Preedit logic tests passed\n";
  return 0;
}
