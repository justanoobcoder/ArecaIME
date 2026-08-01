#pragma once

#include <cstdint>
#include <string>

#include "types.h"

namespace areca {

class VietnameseEngine {
public:
  virtual ~VietnameseEngine() = default;
  virtual BambooResult process(uint32_t codepoint,
                               const std::string &utf8Text) = 0;
  virtual void backspace() = 0;
  virtual void reset() = 0;
};

class BambooEngineAdapter final : public VietnameseEngine {
public:
  explicit BambooEngineAdapter(std::string inputMethod = "Telex 2",
                               bool spellCheck = true,
                               bool modernStyle = true);
  ~BambooEngineAdapter() override;

  BambooEngineAdapter(const BambooEngineAdapter &) = delete;
  BambooEngineAdapter &operator=(const BambooEngineAdapter &) = delete;

  BambooResult process(uint32_t codepoint,
                       const std::string &utf8Text) override;
  void backspace() override;
  void reset() override;
  bool valid() const { return handle_ != 0; }

private:
  uint64_t handle_ = 0;
  bool spellCheck_ = true;
  std::string renderedText_;
};

} // namespace areca
