#include "program_compatibility.h"

#include <cassert>

int main() {
  using areca::isVSCodeFamilyProgram;

  // Existing VS Code-family matching remains intact.
  assert(isVSCodeFamilyProgram("code"));
  assert(isVSCodeFamilyProgram("/usr/bin/code-insiders"));
  assert(isVSCodeFamilyProgram("VSCodium.desktop"));

  // Distribution/desktop terminals can be reported as executables or app IDs.
  assert(isVSCodeFamilyProgram("gnome-terminal-server"));
  assert(isVSCodeFamilyProgram("org.gnome.Console.desktop"));
  assert(isVSCodeFamilyProgram("org.kde.konsole"));
  assert(isVSCodeFamilyProgram("xfce4-terminal"));
  assert(isVSCodeFamilyProgram("com.system76.CosmicTerm.desktop"));

  // Common third-party terminal emulators and development app IDs.
  assert(isVSCodeFamilyProgram("/usr/bin/alacritty"));
  assert(isVSCodeFamilyProgram("net.kovidgoyal.kitty.desktop"));
  assert(isVSCodeFamilyProgram("com.mitchellh.ghostty"));
  assert(isVSCodeFamilyProgram("app.devsuite.Ptyxis.Devel.desktop"));
  assert(isVSCodeFamilyProgram("dev.warp.Warp-Stable.desktop"));
  assert(isVSCodeFamilyProgram("dev.warp.Warp.desktop"));

  // General-purpose IDEs, code editors and their package-channel app IDs.
  assert(isVSCodeFamilyProgram("jetbrains-idea-ultimate.desktop"));
  assert(isVSCodeFamilyProgram("com.jetbrains.PyCharm-Professional.desktop"));
  assert(isVSCodeFamilyProgram("com.google.AndroidStudio.desktop"));
  assert(isVSCodeFamilyProgram("org.eclipse.Eclipse"));
  assert(isVSCodeFamilyProgram("dev.zed.Zed.desktop"));
  assert(isVSCodeFamilyProgram("org.gnome.Builder.Devel.desktop"));
  assert(isVSCodeFamilyProgram("org.kde.kdevelop"));
  assert(isVSCodeFamilyProgram("/usr/bin/neovide"));

  // Specialized IDEs and other development tools.
  assert(isVSCodeFamilyProgram("org.spyder-ide.spyder"));
  assert(isVSCodeFamilyProgram("cc.arduino.IDE2.desktop"));
  assert(isVSCodeFamilyProgram("org.godotengine.Godot"));
  assert(isVSCodeFamilyProgram("io.dbeaver.DBeaverCommunity.desktop"));
  assert(isVSCodeFamilyProgram("com.getpostman.Postman"));
  assert(isVSCodeFamilyProgram("com.axosoft.GitKraken.desktop"));

  // Exact matching prevents unrelated applications from inheriting the rule.
  assert(!isVSCodeFamilyProgram("terminal-notes"));
  assert(!isVSCodeFamilyProgram("wavebox"));
  assert(!isVSCodeFamilyProgram("footage"));
  assert(!isVSCodeFamilyProgram("stationeers"));
  assert(!isVSCodeFamilyProgram("idea-board"));
  assert(!isVSCodeFamilyProgram("studio-one"));
  assert(!isVSCodeFamilyProgram("atomizer"));
  assert(!isVSCodeFamilyProgram("fleet-manager"));
  assert(!isVSCodeFamilyProgram("firefox"));
  assert(!isVSCodeFamilyProgram(""));

  using areca::isTerminalProgram;
  assert(isTerminalProgram("ghostty"));
  assert(isTerminalProgram("com.mitchellh.ghostty"));
  assert(isTerminalProgram("org.gnome.Console.desktop"));
  assert(isTerminalProgram("app.devsuite.Ptyxis.desktop"));
  assert(isTerminalProgram("com.raggesilver.BlackBox.desktop"));
  assert(isTerminalProgram("gnome-terminal-server"));
  assert(isTerminalProgram("mate-terminal"));
  assert(isTerminalProgram("xfce4-terminal"));
  assert(isTerminalProgram("konsole"));
  assert(isTerminalProgram("io.elementary.terminal"));
  assert(!isTerminalProgram("firefox"));
  assert(!isTerminalProgram(""));
}
