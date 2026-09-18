#!/usr/bin/env bash
# Build inteiliDOS for 1990s Computers.
# The generated kernel is restricted to the Intel 80386 instruction set.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_1990s"
TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/i686-elf.cmake"
CMAKE_EXTRA_ARGS=()

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }

for arg in "$@"; do
    case "$arg" in
        --clean) rm -rf "${BUILD_DIR}" ;;
        --modern|--legacy|--universal) ;;
        *) CMAKE_EXTRA_ARGS+=("$arg") ;;
    esac
done

echo ""
echo -e "${BOLD}  inteiliDOS for 1990s Computers${RESET}"
echo -e "  Universal Intel 80386-compatible build"
echo ""

for cmd in cmake nasm i686-elf-gcc i686-elf-ld i686-elf-objcopy; do
    command -v "${cmd}" >/dev/null 2>&1 ||
        error "'${cmd}' is required. See BUILD.md for toolchain setup."
    info "Found: $(command -v "${cmd}")"
done

if command -v i686-elf-grub-mkrescue >/dev/null 2>&1; then
    GRUB_MKRESCUE="$(command -v i686-elf-grub-mkrescue)"
elif command -v grub-mkrescue >/dev/null 2>&1; then
    GRUB_MKRESCUE="$(command -v grub-mkrescue)"
else
    error "grub-mkrescue is required to build the universal BIOS ISO."
fi

command -v xorriso >/dev/null 2>&1 ||
    error "xorriso is required by grub-mkrescue."

HAVE_GENISOIMAGE=0
if command -v genisoimage >/dev/null 2>&1 ||
   command -v mkisofs >/dev/null 2>&1; then
    HAVE_GENISOIMAGE=1
else
    warn "genisoimage/mkisofs not found; the optional El Torito image is skipped."
fi

cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TARGET=universal \
      -DHAVE_GENISOIMAGE="${HAVE_GENISOIMAGE}" \
      -DGRUB_MKRESCUE="${GRUB_MKRESCUE}" \
      "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}"

CPU_COUNT="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
cmake --build "${BUILD_DIR}" -- -j"${CPU_COUNT}"

echo ""
success "Universal BIOS ISO: ${BUILD_DIR}/inteiliDOS_1990s.iso"
success "Raw floppy image:   ${BUILD_DIR}/inteiliDOS_1990s_floppy.img"
echo ""
echo "Test on the oldest CPU profile available in QEMU:"
echo "  cmake --build \"${BUILD_DIR}\" --target run-legacy"
echo ""