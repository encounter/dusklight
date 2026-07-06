#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
APP_DIR="$ROOT_DIR/platforms/android/app/src/main/jniLibs"
ANDROID_HOME_DIR="${ANDROID_HOME:-$HOME/Android/Sdk}"
ANDROID_NDK_VER="${ANDROID_NDK_VERSION:-}"
ANDROID_STAGE_ABIS="${ANDROID_STAGE_ABIS:-arm64-v8a x86_64}"
ANDROID_STAGE_STRIP="${ANDROID_STAGE_STRIP:-1}"
STRIP_TOOL=""

if [[ -z "$ANDROID_NDK_VER" ]] && [[ -d "$ANDROID_HOME_DIR/ndk" ]]; then
  ANDROID_NDK_VER="$(ls -1 "$ANDROID_HOME_DIR/ndk" | sort -V | tail -n 1)"
fi

if [[ -n "$ANDROID_NDK_VER" ]]; then
  case "$(uname -s)" in
    Darwin) HOST_TAG="darwin-x86_64" ;;
    Linux) HOST_TAG="linux-x86_64" ;;
    *) HOST_TAG="" ;;
  esac

  PREBUILT_DIR="$ANDROID_HOME_DIR/ndk/$ANDROID_NDK_VER/toolchains/llvm/prebuilt"
  if [[ -n "$HOST_TAG" && -x "$PREBUILT_DIR/$HOST_TAG/bin/llvm-strip" ]]; then
    STRIP_TOOL="$PREBUILT_DIR/$HOST_TAG/bin/llvm-strip"
  else
    for candidate in "$PREBUILT_DIR"/*/bin/llvm-strip; do
      if [[ -x "$candidate" ]]; then
        STRIP_TOOL="$candidate"
        break
      fi
    done
  fi
fi

ndk_triple_for_abi() {
  case "$1" in
    arm64-v8a) echo "aarch64-linux-android" ;;
    x86_64) echo "x86_64-linux-android" ;;
    *) echo "" ;;
  esac
}

# The build uses ANDROID_STL=c++_shared so libmain.so and dlopen'd code mods share one
# C++ runtime; the runtime library ships in the APK alongside libmain.so.
copy_stl() {
  local abi="$1"
  local triple dst_dir src
  triple="$(ndk_triple_for_abi "$abi")"
  if [[ -z "$triple" || -z "$ANDROID_NDK_VER" || -z "${HOST_TAG:-}" ]]; then
    echo "Cannot locate libc++_shared.so for $abi (NDK version or host tag unknown)" >&2
    exit 1
  fi
  src="$ANDROID_HOME_DIR/ndk/$ANDROID_NDK_VER/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/$triple/libc++_shared.so"
  if [[ ! -f "$src" ]]; then
    echo "Missing libc++_shared.so for $abi: $src" >&2
    exit 1
  fi
  dst_dir="$APP_DIR/$abi"
  mkdir -p "$dst_dir"
  cp -f "$src" "$dst_dir/libc++_shared.so"
  echo "Staged $src -> $dst_dir/libc++_shared.so"
}

copy_lib() {
  local abi="$1"
  local src="$2"
  local dst_dir="$APP_DIR/$abi"
  local dst="$dst_dir/libmain.so"
  local tmp="$dst_dir/.libmain.so.$$"
  if [[ ! -f "$src" ]]; then
    echo "Missing native library for $abi: $src" >&2
    exit 1
  fi

  mkdir -p "$dst_dir"
  cp -f "$src" "$tmp"
  if [[ "$ANDROID_STAGE_STRIP" != "0" ]] && [[ -n "$STRIP_TOOL" ]]; then
    "$STRIP_TOOL" --strip-unneeded "$tmp"
    mv -f "$tmp" "$dst"
    echo "Stripped and staged $src -> $dst"
  else
    mv -f "$tmp" "$dst"
    echo "Staged $src -> $dst (strip disabled or strip tool unavailable)"
  fi
}

# Drop any previously staged ABI directories to avoid stale APK contents.
rm -rf "$APP_DIR/x86" "$APP_DIR/arm64-v8a" "$APP_DIR/x86_64"

for abi in $ANDROID_STAGE_ABIS; do
  case "$abi" in
    arm64-v8a) src="$ROOT_DIR/build/android-arm64/libmain.so" ;;
    x86_64) src="$ROOT_DIR/build/android-x86_64/libmain.so" ;;
    *)
      echo "Unsupported ABI '$abi'. Supported ABIs: arm64-v8a x86_64" >&2
      exit 1
      ;;
  esac
  copy_lib "$abi" "$src"
  copy_stl "$abi"
done
