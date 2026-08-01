#pragma once

#include <string>

#include "types.h"

namespace areca {

// Reconstruct the complete text represented by a Bamboo delta. Rewrite mode
// applies the delta to application text; Preedit mode applies it to the
// in-memory composition and commits the resulting whole string at a boundary.
std::string buildPreeditCommit(const BambooResult &result);

} // namespace areca
