#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$ROOT_DIR/lib"

if [[ ! -d "$LIB_DIR" ]]; then
  echo "Error: missing lib directory at $LIB_DIR" >&2
  exit 1
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage: ./rebuild_libs.sh

Cleans every *.a file under ./lib and rebuilds vendored static libraries.
EOF
  exit 0
fi

echo "[1/4] Collecting current static libraries..."
read_lines_into_array() {
  local array_name="$1"
  shift

  eval "$array_name=()"
  while IFS= read -r line; do
    eval "$array_name+=(\"\$line\")"
  done < <("$@" | sort)
}

read_lines_into_array existing_archives find "$LIB_DIR" -type f -name '*.a'

if ((${#existing_archives[@]} == 0)); then
  echo "No static libraries found under $LIB_DIR"
else
  printf 'Found %d static libraries:\n' "${#existing_archives[@]}"
  printf '  %s\n' "${existing_archives[@]}"
fi

echo "[2/4] Removing static libraries..."
if ((${#existing_archives[@]} > 0)); then
  find "$LIB_DIR" -type f -name '*.a' -delete
fi

echo "[3/4] Rebuilding vendored libraries..."

required_archives=()
declare -a RAYLIB_LINUX_BACKEND_ARGS=()

prompt_install_raylib_deps() {
  if [[ ! -t 0 ]]; then
    echo "Error: raylib build failed and this session is non-interactive, so dependency install prompt cannot be shown." >&2
    return 1
  fi

  local answer
  read -r -p "raylib build appears to be missing dependencies. Try to install them automatically? (y/n): " answer
  case "$(printf '%s' "$answer" | tr '[:upper:]' '[:lower:]')" in
    y|yes) return 0 ;;
    *)
      echo "Skipping dependency installation." >&2
      return 1
      ;;
  esac
}

install_raylib_deps_linux() {
  local -a elev=()
  if (( EUID != 0 )); then
    if command -v sudo >/dev/null 2>&1; then
      elev=(sudo)
    else
      echo "Error: need root privileges to install dependencies, but 'sudo' is not available." >&2
      return 1
    fi
  fi

  if command -v dnf >/dev/null 2>&1; then
    echo "Installing SDL2 development package via dnf..."
    "${elev[@]}" dnf -y install SDL2-devel
  elif command -v apt-get >/dev/null 2>&1; then
    echo "Installing SDL2 development package via apt..."
    "${elev[@]}" apt-get update
    "${elev[@]}" apt-get install -y libsdl2-dev
  elif command -v pacman >/dev/null 2>&1; then
    echo "Installing SDL2 development package via pacman..."
    "${elev[@]}" pacman -Sy --noconfirm sdl2
  elif command -v zypper >/dev/null 2>&1; then
    echo "Installing SDL2 development package via zypper..."
    "${elev[@]}" zypper --non-interactive install libSDL2-devel
  else
    echo "Error: unsupported package manager for auto-install." >&2
    return 1
  fi
}

raylib_make_args_for_linux() {
  RAYLIB_LINUX_BACKEND_ARGS=()
  local sdl_include_dir=""
  local sdl_libs=""

  if [[ "$(uname -s)" != "Linux" ]]; then
    return 0
  fi

  if ! command -v pkg-config >/dev/null 2>&1; then
    return 0
  fi

  if pkg-config --exists sdl2; then
    RAYLIB_LINUX_BACKEND_ARGS+=("PLATFORM=PLATFORM_DESKTOP_SDL")

    sdl_include_dir="$(pkg-config --cflags-only-I sdl2 2>/dev/null | tr ' ' '\n' | sed -n 's/^-I//p' | head -n1 || true)"
    if [[ -n "$sdl_include_dir" ]]; then
      RAYLIB_LINUX_BACKEND_ARGS+=("SDL_INCLUDE_PATH=$sdl_include_dir")
    fi

    sdl_libs="$(pkg-config --libs sdl2 2>/dev/null || true)"
    if [[ -n "$sdl_libs" ]]; then
      RAYLIB_LINUX_BACKEND_ARGS+=("SDL_LIBRARIES=$sdl_libs")
    fi

    return 0
  fi

  if pkg-config --exists x11 || pkg-config --exists wayland-client; then
    return 0
  fi
}

build_raylib() {
  local -a raylib_make_args=("RAYLIB_LIBTYPE=STATIC" "RAYLIB_RELEASE_PATH=../lib")
  local retried=0

  raylib_make_args_for_linux
  if ((${#RAYLIB_LINUX_BACKEND_ARGS[@]} > 0)); then
    raylib_make_args+=("${RAYLIB_LINUX_BACKEND_ARGS[@]}")
  fi

  if [[ " ${raylib_make_args[*]} " == *" PLATFORM=PLATFORM_DESKTOP_SDL "* ]]; then
    echo "  using SDL backend for raylib"
  else
    echo "  using default raylib desktop backend"
  fi

  while true; do
    make -C "$LIB_DIR/raylib/src" clean
    if make -C "$LIB_DIR/raylib/src" "${raylib_make_args[@]}"; then
      return 0
    fi

    if [[ "$(uname -s)" != "Linux" ]] || (( retried )); then
      return 1
    fi

    echo "raylib build failed; this is often due to missing Linux development dependencies." >&2
    if ! prompt_install_raylib_deps; then
      return 1
    fi

    install_raylib_deps_linux
    retried=1

    raylib_make_args=("RAYLIB_LIBTYPE=STATIC" "RAYLIB_RELEASE_PATH=../lib")
    raylib_make_args_for_linux
    if ((${#RAYLIB_LINUX_BACKEND_ARGS[@]} > 0)); then
      raylib_make_args+=("${RAYLIB_LINUX_BACKEND_ARGS[@]}")
    fi

    echo "Retrying raylib rebuild after dependency installation..."
  done
}

lua_make_target_for_host() {
  case "$(uname -s)" in
    Linux) echo "linux" ;;
    Darwin) echo "macosx" ;;
    *) echo "posix" ;;
  esac
}

if [[ -f "$LIB_DIR/raylib/src/Makefile" ]]; then
  echo "- Rebuilding raylib"
  mkdir -p "$LIB_DIR/raylib/lib"
  build_raylib
  required_archives+=("$LIB_DIR/raylib/lib/libraylib.a")
fi

if [[ -d "$LIB_DIR/lua-5.4.6/src" ]]; then
  echo "- Rebuilding Lua"
  make -C "$LIB_DIR/lua-5.4.6/src" clean "$(lua_make_target_for_host)"
  mkdir -p "$LIB_DIR/lua-5.4.6/lib"
  cp -f "$LIB_DIR/lua-5.4.6/src/liblua.a" "$LIB_DIR/lua-5.4.6/lib/liblua.a"
  required_archives+=("$LIB_DIR/lua-5.4.6/lib/liblua.a")
  required_archives+=("$LIB_DIR/lua-5.4.6/src/liblua.a")
fi

if [[ -f "$LIB_DIR/tomlc17/Makefile" ]]; then
  echo "- Rebuilding tomlc17"
  make -C "$LIB_DIR/tomlc17" clean install prefix=./
  required_archives+=("$LIB_DIR/tomlc17/lib/libtomlc17.a")
  required_archives+=("$LIB_DIR/tomlc17/src/libtomlc17.a")
fi

echo "[4/4] Validating rebuild results..."
read_lines_into_array rebuilt_archives find "$LIB_DIR" -type f -name '*.a'

if ((${#rebuilt_archives[@]} == 0)); then
  echo "Error: no static libraries were rebuilt." >&2
  exit 1
fi

printf 'Rebuilt %d static libraries:\n' "${#rebuilt_archives[@]}"
printf '  %s\n' "${rebuilt_archives[@]}"

missing=0
for archive in "${required_archives[@]}"; do
  if [[ ! -f "$archive" ]]; then
    echo "Error: required archive was not rebuilt: $archive" >&2
    missing=1
  fi
done

for archive in "${existing_archives[@]}"; do
  if [[ ! -f "$archive" ]] && [[ ! " ${required_archives[*]} " =~ " ${archive} " ]]; then
    echo "Warning: archive was removed and not rebuilt by this script: $archive" >&2
  fi
done

if ((missing)); then
  exit 1
fi

echo "Done. Library archives cleaned and rebuilt."