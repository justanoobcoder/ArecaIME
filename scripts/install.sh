#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"
ARECA_TARGET_GROUP="$(id -gn "$ARECA_TARGET_USER")"
if [[ -v ARECA_UINPUT_SOCKET ]]; then
  ARECA_SOCKET_PATH="$ARECA_UINPUT_SOCKET"
  ARECA_SOCKET_EXPLICIT=1
else
  ARECA_SOCKET_PATH="/tmp/areca-uinput.sock"
  ARECA_SOCKET_EXPLICIT=0
fi
ARECA_INSTALL_DEPS=1
ARECA_RESTART_FCITX=1
ARECA_RUN_TESTS=1

areca_target_home() {
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v getent >/dev/null 2>&1; then
    getent passwd "$ARECA_TARGET_USER" | cut -d: -f6
    return
  fi
  printf '%s\n' "${HOME:?HOME is required}"
}

ARECA_TARGET_HOME="$(areca_target_home)"

is_user_prefix() {
  [[ "$ARECA_PREFIX" == "$ARECA_TARGET_HOME" ||
     "$ARECA_PREFIX" == "$ARECA_TARGET_HOME/"* ]]
}

run_as_target_user() {
  if [[ "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo -u "$ARECA_TARGET_USER" "$@"
  else
    return 1
  fi
}

install_deps_debian() {
  echo "[areca] Installing build dependencies with apt"
  sudo apt-get update
  local common=(build-essential cmake ninja-build pkg-config extra-cmake-modules
                golang-go)
  local fcitx=(fcitx5 fcitx5-config-qt libfcitx5core-dev
               libfcitx5config-dev libfcitx5utils-dev)
  if ! sudo apt-get install -y "${common[@]}" "${fcitx[@]}"; then
    echo "[areca] Individual Fcitx development packages unavailable; trying fcitx5-dev"
    sudo apt-get install -y "${common[@]}" fcitx5 fcitx5-config-qt fcitx5-dev
  fi
}

install_deps_arch() {
  echo "[areca] Installing build dependencies with pacman"
  sudo pacman -Sy --needed --noconfirm \
    base-devel cmake ninja pkgconf extra-cmake-modules go fcitx5 \
    fcitx5-configtool
}

install_deps_fedora() {
  echo "[areca] Installing build dependencies with dnf"
  sudo dnf install -y \
    gcc-c++ cmake ninja-build pkgconf-pkg-config extra-cmake-modules go \
    fcitx5 fcitx5-devel fcitx5-configtool
}

install_build_deps() {
  if [[ "$(uname -s)" != "Linux" || ! -r /etc/os-release ]]; then
    echo "[areca] Skipping dependency installation: unsupported host"
    return
  fi

  # shellcheck disable=SC1091
  . /etc/os-release
  if command -v apt-get >/dev/null 2>&1; then
    case " ${ID:-} ${ID_LIKE:-} " in
      *" ubuntu "*|*" debian "*) install_deps_debian; return ;;
    esac
  fi
  if command -v pacman >/dev/null 2>&1; then
    case " ${ID:-} ${ID_LIKE:-} " in
      *" arch "*|*" cachyos "*) install_deps_arch; return ;;
    esac
  fi
  if command -v dnf >/dev/null 2>&1; then
    case " ${ID:-} ${ID_LIKE:-} " in
      *" fedora "*|*" rhel "*) install_deps_fedora; return ;;
    esac
  fi
  echo "[areca] Skipping dependency installation for ${PRETTY_NAME:-unknown distro}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      ARECA_PREFIX="$ARECA_TARGET_HOME/.local"
      shift
      ;;
    --prefix)
      ARECA_PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --build-dir)
      ARECA_BUILD_DIR="${2:?missing value for --build-dir}"
      shift 2
      ;;
    --build-type)
      ARECA_BUILD_TYPE="${2:?missing value for --build-type}"
      shift 2
      ;;
    --socket)
      ARECA_SOCKET_PATH="${2:?missing value for --socket}"
      ARECA_SOCKET_EXPLICIT=1
      shift 2
      ;;
    --skip-deps)
      ARECA_INSTALL_DEPS=0
      shift
      ;;
    --skip-tests)
      ARECA_RUN_TESTS=0
      shift
      ;;
    --no-restart)
      ARECA_RESTART_FCITX=0
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --user                 Install to $ARECA_TARGET_HOME/.local without sudo
  --prefix PATH          Installation prefix (default: /usr)
  --build-dir PATH       CMake build directory (default: ./build)
  --build-type TYPE      CMake build type (default: RelWithDebInfo)
  --socket PATH          Unix socket path used by addon/server
  --skip-deps            Do not install distro build dependencies
  --skip-tests           Do not run CTest and Go server tests
  --no-restart           Do not attempt to restart Fcitx5

Environment:
  PREFIX=/usr
  BUILD_DIR=./build
  BUILD_TYPE=RelWithDebInfo
  ARECA_UINPUT_SOCKET=/tmp/areca-uinput.sock
EOF
      exit 0
      ;;
    *)
      echo "[areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$ARECA_PREFIX" in
  "~") ARECA_PREFIX="$ARECA_TARGET_HOME" ;;
  "~/"*) ARECA_PREFIX="$ARECA_TARGET_HOME/${ARECA_PREFIX#"~/"}" ;;
esac

echo "[areca] root=$ARECA_ROOT_DIR"
echo "[areca] build=$ARECA_BUILD_DIR prefix=$ARECA_PREFIX type=$ARECA_BUILD_TYPE"

if [[ "$ARECA_INSTALL_DEPS" == 1 ]]; then
  if is_user_prefix; then
    echo "[areca] User install selected; skipping system dependency installation"
  else
    install_build_deps
  fi
fi

if [[ -d "$ARECA_ROOT_DIR/.git" ]]; then
  echo "[areca] Initializing Bamboo submodule"
  git -C "$ARECA_ROOT_DIR" submodule update --init
fi

# Respect an existing CMake generator. A common failure is an old build cache
# configured with Ninja on a machine where ninja is no longer installed. Do
# not delete that cache; select a sibling Makefiles build directory instead.
ARECA_CMAKE_GENERATOR=""
if [[ -f "$ARECA_BUILD_DIR/CMakeCache.txt" ]]; then
  ARECA_CACHED_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' \
    "$ARECA_BUILD_DIR/CMakeCache.txt" | head -n 1)"
  if [[ "$ARECA_CACHED_GENERATOR" == "Ninja" ]] && \
     ! command -v ninja >/dev/null 2>&1; then
    echo "[areca] Existing build cache requires Ninja, but ninja is unavailable"
    ARECA_BUILD_DIR="${ARECA_BUILD_DIR}-make"
    ARECA_CMAKE_GENERATOR="Unix Makefiles"
    echo "[areca] Using fallback build directory: $ARECA_BUILD_DIR"
  fi
elif command -v ninja >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Ninja"
elif command -v make >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Unix Makefiles"
else
  echo "[areca] No build program found; install ninja or make" >&2
  exit 1
fi

ARECA_GENERATOR_ARGS=()
if [[ -n "$ARECA_CMAKE_GENERATOR" ]]; then
  ARECA_GENERATOR_ARGS=(-G "$ARECA_CMAKE_GENERATOR")
fi

if ! command -v c++ >/dev/null 2>&1 && \
   ! command -v g++ >/dev/null 2>&1 && \
   ! command -v clang++ >/dev/null 2>&1; then
  echo "[areca] No C++ compiler found; install g++ or clang++" >&2
  exit 1
fi

cmake -S "$ARECA_ROOT_DIR" -B "$ARECA_BUILD_DIR" \
  "${ARECA_GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$ARECA_BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$ARECA_PREFIX" \
  -DBUILD_UINPUT_SERVER=ON

echo "[areca] Building"
cmake --build "$ARECA_BUILD_DIR" -j

if [[ "$ARECA_RUN_TESTS" == 1 ]]; then
  echo "[areca] Running C++ tests"
  ctest --test-dir "$ARECA_BUILD_DIR" --output-on-failure
  echo "[areca] Running uinput-server tests"
  (
    cd "$ARECA_ROOT_DIR/server"
    go test ./...
  )
fi

if is_user_prefix; then
  echo "[areca] Installing to user prefix"
  cmake --install "$ARECA_BUILD_DIR"
else
  echo "[areca] Installing with sudo"
  sudo cmake --install "$ARECA_BUILD_DIR"
fi

ARECA_SERVER_BIN="$ARECA_PREFIX/libexec/areca-uinput-server"

if [[ "$ARECA_SOCKET_EXPLICIT" == 1 ]]; then
  ARECA_CONFIG_DIR="$ARECA_TARGET_HOME/.config/fcitx5/conf"
  ARECA_CONFIG_FILE="$ARECA_CONFIG_DIR/areca-advanced.conf"
  ARECA_CONFIG_TMP="$(mktemp)"
  trap 'rm -f "$ARECA_CONFIG_TMP"' EXIT
  if [[ -f "$ARECA_CONFIG_FILE" ]]; then
    awk -v socket="$ARECA_SOCKET_PATH" '
      BEGIN { replaced = 0 }
      /^SocketPath=/ { print "SocketPath=" socket; replaced = 1; next }
      { print }
      END { if (!replaced) print "SocketPath=" socket }
    ' "$ARECA_CONFIG_FILE" >"$ARECA_CONFIG_TMP"
  else
    printf 'SocketPath=%s\n' "$ARECA_SOCKET_PATH" >"$ARECA_CONFIG_TMP"
  fi
  run_as_target_user mkdir -p "$ARECA_CONFIG_DIR"
  if [[ "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
    install -m 0644 "$ARECA_CONFIG_TMP" "$ARECA_CONFIG_FILE"
  else
    sudo install -o "$ARECA_TARGET_USER" -g "$ARECA_TARGET_GROUP" \
      -m 0644 "$ARECA_CONFIG_TMP" "$ARECA_CONFIG_FILE"
  fi
  rm -f "$ARECA_CONFIG_TMP"
  trap - EXIT
fi

if [[ "$(uname -s)" == "Linux" ]] && ! is_user_prefix; then
  if [[ -x "$ARECA_SERVER_BIN" ]] && command -v setcap >/dev/null 2>&1; then
    echo "[areca] Granting optional scheduling-priority capability"
    sudo setcap cap_sys_nice+ep "$ARECA_SERVER_BIN" >/dev/null 2>&1 || true
  fi

  if [[ ! -e /dev/uinput ]]; then
    echo "[areca] Loading uinput kernel module"
    sudo modprobe uinput >/dev/null 2>&1 || true
  fi

  if [[ -e /dev/uinput ]]; then
    if ! getent group uinput >/dev/null 2>&1; then
      echo "[areca] Creating uinput group"
      sudo groupadd --system uinput >/dev/null 2>&1 || true
    fi
    if [[ -n "$ARECA_TARGET_USER" ]]; then
      echo "[areca] Adding $ARECA_TARGET_USER to uinput group"
      sudo usermod -aG uinput "$ARECA_TARGET_USER" >/dev/null 2>&1 || true
    fi
    if [[ -d /etc/udev/rules.d ]] && command -v udevadm >/dev/null 2>&1; then
      echo "[areca] Installing /dev/uinput udev rule"
      sudo tee /etc/udev/rules.d/99-areca-uinput.rules >/dev/null <<'EOF' || true
# Areca: allow members of group "uinput" to access /dev/uinput.
KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", MODE="0660", GROUP="uinput"
EOF
      sudo udevadm control --reload-rules >/dev/null 2>&1 || true
      sudo udevadm trigger --name-match=uinput >/dev/null 2>&1 || true
    fi
    sudo chgrp uinput /dev/uinput >/dev/null 2>&1 || true
    sudo chmod 0660 /dev/uinput >/dev/null 2>&1 || true
  else
    echo "[areca] WARNING: /dev/uinput is unavailable; fallback rewrite cannot work"
  fi
fi

ARECA_USER_UNIT_DIR="$ARECA_TARGET_HOME/.config/systemd/user"
ARECA_USER_UNIT="$ARECA_USER_UNIT_DIR/areca-uinput-server.service"
if [[ "$(uname -s)" == "Linux" && -x "$ARECA_SERVER_BIN" ]]; then
  echo "[areca] Installing user service: $ARECA_USER_UNIT"
  run_as_target_user mkdir -p "$ARECA_USER_UNIT_DIR"
  ARECA_UNIT_TMP="$(mktemp)"
  trap 'rm -f "$ARECA_UNIT_TMP"' EXIT
  cat >"$ARECA_UNIT_TMP" <<EOF
[Unit]
Description=Areca uinput Backspace server
After=graphical-session.target

[Service]
Type=simple
ExecStart="$ARECA_SERVER_BIN" -socket "$ARECA_SOCKET_PATH"
Environment=ARECA_UINPUT_SERVER_LOG=1
Restart=on-failure
RestartSec=1

[Install]
WantedBy=default.target
EOF
  if [[ "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
    install -m 0644 "$ARECA_UNIT_TMP" "$ARECA_USER_UNIT"
  else
    sudo install -o "$ARECA_TARGET_USER" -g "$ARECA_TARGET_GROUP" \
      -m 0644 "$ARECA_UNIT_TMP" "$ARECA_USER_UNIT"
  fi
  rm -f "$ARECA_UNIT_TMP"
  trap - EXIT

  if run_as_target_user systemctl --user daemon-reload >/dev/null 2>&1; then
    run_as_target_user systemctl --user enable \
      areca-uinput-server.service >/dev/null 2>&1 || true
    if ! run_as_target_user systemctl --user restart \
      areca-uinput-server.service >/dev/null 2>&1; then
      echo "[areca] WARNING: Could not restart areca-uinput-server.service"
    fi
  else
    echo "[areca] Could not reach the user systemd session; enable the service after login"
  fi
fi

if [[ "$ARECA_RESTART_FCITX" == 1 ]]; then
  echo "[areca] Restarting Fcitx5 (best effort)"
  run_as_target_user fcitx5 -rd >/dev/null 2>&1 || true
fi

cat <<EOF
[areca] Done.

Next:
  - Open fcitx5-configtool and add "Areca (Bamboo)".
  - If Plasma Wayland keeps the old addon in KWin, log out and log in once.
  - If the uinput group was added now, log out/in once before testing fallback.
  - Server log: journalctl --user -u areca-uinput-server -f
  - Addon log:  journalctl --user -f | grep areca
EOF
