#pragma once

#include <string>
#include <utility>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>

namespace areca {

enum class PresentationMode { Rewrite, Preedit, Redirect };
FCITX_CONFIG_ENUM_NAME_WITH_I18N(PresentationMode, N_("Rewrite trực tiếp"),
                                 N_("Preedit"), N_("Redirect (EN)"));

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

FCITX_CONFIGURATION(MacroEntry,
                    fcitx::Option<std::string> key{this, "Key",
                                                   N_("Từ viết tắt"), ""};
                    fcitx::Option<std::string> value{
                        this, "Value", N_("Nội dung thay thế"), ""};);

FCITX_CONFIGURATION(
    MacroTableConfig,
    fcitx::OptionWithAnnotation<std::vector<MacroEntry>,
                                fcitx::ListDisplayOptionAnnotation>
        macros{this,
               "Macro",
               N_("Danh sách macro"),
               {},
               {},
               {},
               fcitx::ListDisplayOptionAnnotation("Key")};);

FCITX_CONFIGURATION(
    AdvancedConfig,
    fcitx::Option<int, fcitx::IntConstrain> backspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::Option<int, fcitx::IntConstrain> afterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Thời gian chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<int, fcitx::IntConstrain> postCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<bool> preciseTiming{this, "PreciseTiming",
                                      N_("Dùng timer độ chính xác cao"), true};
    fcitx::Option<int, fcitx::IntConstrain> resetDelayMs{
        this, "ResetDelayMs", N_("Delay trước khi thực thi reset (ms)"), 250,
        fcitx::IntConstrain(0, 5000)};);

FCITX_CONFIGURATION(
    ArecaConfig,
    fcitx::OptionWithAnnotation<PresentationMode,
                                PresentationModeI18NAnnotation>
        presentationMode{this, "PresentationMode", N_("Chế độ hiển thị"),
                         PresentationMode::Rewrite};
    fcitx::KeyListOption switchModeKey{
        this,
        "SwitchModeKey",
        N_("Phím tắt chuyển chế độ gõ"),
        {fcitx::Key("Alt+space")},
        fcitx::KeyListConstrain(fcitx::KeyConstrainFlag::AllowModifierLess)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyBackspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        fcitx::IntConstrain(0, 1000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyAfterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Thời gian chờ sau Backspace cuối (ms)"), 10,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyPostCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        fcitx::IntConstrain(0, 5000)};
    fcitx::HiddenOption<int, fcitx::IntConstrain> legacyResetDelayMs{
        this, "ResetDelayMs", N_("Delay trước khi thực thi reset (ms)"), 250,
        fcitx::IntConstrain(0, 5000)};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        bambooInputMethod{this, "BambooInputMethod", N_("Kiểu gõ Bamboo"),
                          "Telex 2"};
    fcitx::OptionWithAnnotation<std::string, StringListAnnotation>
        outputCharset{this, "OutputCharset", N_("Bảng mã đầu ra"), "Unicode"};
    fcitx::Option<bool> spellCheck{
        this, "SpellCheck",
        N_("Kiểm tra chính tả và khôi phục từ không hợp lệ"), true};
    fcitx::Option<bool> modernStyle{
        this, "ModernStyle", N_("Đặt dấu kiểu oà, uý thay cho òa, úy"), true};
    fcitx::Option<bool> autoCapitalizeAfterPunctuation{
        this, "AutoCapitalizeAfterPunctuation",
        N_("Tự viết hoa sau dấu kết câu (. ! ?)"), false};
    fcitx::Option<bool> enableMacro{this, "EnableMacro", N_("Bật macro"), true};
    fcitx::Option<bool> capitalizeMacro{
        this, "CapitalizeMacro", N_("Tự đổi hoa/thường cho nội dung macro"),
        true};
    fcitx::SubConfigOption macroEditor{this, "MacroEditor",
                                       N_("Chỉnh sửa macro"),
                                       "fcitx://config/addon/areca/macro"};
    fcitx::SubConfigOption advancedEditor{
        this, "AdvancedEditor", N_("Cấu hình nâng cao"),
        "fcitx://config/addon/areca/advanced"};
    fcitx::Option<bool> debug{this, "Debug", N_("Bật log debug Areca"), true};);

} // namespace areca
