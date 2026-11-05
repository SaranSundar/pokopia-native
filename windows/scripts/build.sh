#!/bin/bash
# build.sh [-j N] [Debug|Release]: the Linux build (docs/LINUX-PLAN.md), the counterpart of build.cmd. Configures the first time and builds into
# $NXW_BUILD_ROOT/linux-<config> (default ~/pkn-linux/build) with clang from LLVM 23.1.4's Linux release. Gentle by default: 2 jobs at nice 19.
# The toolchains come from $NXW_TOOLCHAINS (default ~/pkn-linux/toolchains): llvm-23.1.4, vulkansdk-1.4.363.0, SDL3-3.4.18-linux, and cmake from
# ~/pkn-linux/venv when the system's is older than 3.28.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
JOBS=2; CONFIG=Release; NATIVE=()
while [ $# -gt 0 ]; do
  case $1 in
    -j) JOBS=$2; shift 2 ;;
    --) shift; NATIVE=("$@"); break ;;   # the rest to ninja, e.g. -- -k 0
    *) CONFIG=$1; shift ;;
  esac
done
TC=${NXW_TOOLCHAINS:-$HOME/pkn-linux/toolchains}
B=${NXW_BUILD_ROOT:-$HOME/pkn-linux/build}/linux-$CONFIG
[ -x "$HOME/pkn-linux/venv/bin/cmake" ] && export PATH=$HOME/pkn-linux/venv/bin:$PATH
for need in "$TC/llvm-23.1.4/bin/clang" "$TC/vulkansdk-1.4.363.0/x86_64/include/vulkan/vulkan.h" "$TC/SDL3-3.4.18-linux/include/SDL3/SDL.h"; do
  [ -e "$need" ] || { echo "build.sh: missing $need (see docs/LINUX-PLAN.md section 4)"; exit 1; }
done
if [ ! -f "$B/build.ninja" ]; then
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DCMAKE_C_COMPILER="$TC/llvm-23.1.4/bin/clang" -DCMAKE_CXX_COMPILER="$TC/llvm-23.1.4/bin/clang++" \
    -DNXW_LLVM_DIR="$TC/llvm-23.1.4" -DNXRT_VULKAN_SDK="$TC/vulkansdk-1.4.363.0/x86_64" -DNXRT_SDL3_DIR="$TC/SDL3-3.4.18-linux"
fi
exec nice -n 19 cmake --build "$B" -j "$JOBS" ${NATIVE[@]+-- "${NATIVE[@]}"}
