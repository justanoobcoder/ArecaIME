#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>

namespace areca {

struct StringListAnnotation : public fcitx::EnumAnnotation {
  void setList(std::vector<std::string> list) { list_ = std::move(list); }

  void dumpDescription(fcitx::RawConfig &config) const {
    fcitx::EnumAnnotation::dumpDescription(config);
    for (size_t i = 0; i < list_.size(); ++i) {
      config.setValueByPath("Enum/" + std::to_string(i), list_[i]);
    }
  }

private:
  std::vector<std::string> list_;
};

FCITX_CONFIGURATION(
    ArecaConfig,
    fcitx::Option<int, fcitx::IntConstrain> keyIntervalMs{
        this, "KeyIntervalMs", N_("Khoảng cách xử lý phím tối thiểu (ms)"), 20,
        fcitx::IntConstrain(20, 1000)};
    fcitx::Option<int, fcitx::IntConstrain> backspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 5,
        fcitx::IntConstrain(0, 1000)};
    fcitx::Option<int, fcitx::IntConstrain> afterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Thời gian chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> postCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> resetDelayMs{
        this, "ResetDelayMs", N_("Delay trước khi thực thi reset (ms)"), 120,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<std::string> socketPath{this, "SocketPath",
                                          N_("Unix socket của uinput server"),
                                          "/tmp/openkey-nonpreedit.sock"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        bambooInputMethod{
        this, "BambooInputMethod", N_("Kiểu gõ Bamboo"), "Telex 2"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        outputCharset{this, "OutputCharset", N_("Bảng mã đầu ra"),
                      "Unicode"};
    fcitx::Option<bool> spellCheck{
        this, "SpellCheck",
        N_("Kiểm tra chính tả và khôi phục từ không hợp lệ"), true};
    fcitx::Option<bool> modernStyle{
        this, "ModernStyle", N_("Đặt dấu kiểu oà, uý thay cho òa, úy"),
        true};
    fcitx::Option<bool> debug{this, "Debug", N_("Bật log debug Areca"),
                              true};);

} // namespace areca
