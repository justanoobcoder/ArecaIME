#include <cassert>
#include <iostream>

#include "word_segment.h"

int main() {
  areca::WordSegment segment;

  assert(areca::extractWordBeforeCursor("xin chào", 8, segment));
  assert(segment.word == "chào");
  assert(segment.startChar == 4);
  assert(segment.endChar == 8);

  assert(areca::extractWordBeforeCursor("foo_bar baz", 7, segment));
  assert(segment.word == "foo_bar");

  assert(!areca::extractWordBeforeCursor("xin ", 4, segment));
  assert(!areca::extractWordBeforeCursor("", 0, segment));
  assert(!areca::extractWordBeforeCursor("abc", 4, segment));

  std::cout << "Word segment tests passed\n";
  return 0;
}
