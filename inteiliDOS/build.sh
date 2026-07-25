#!/usr/bin/env bash
# =============================================================================
# inteilidOS -- build.sh
# Configures and builds inteilidOS using CMake + a bare-metal i686-elf
# toolchain.  Run from the inteilidOS/ directory.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/i686-elf.cmake"

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Dependency checks
# ---------------------------------------------------------------------------
check_dep() {
    local cmd="$1" pkg="${2:-$1}"
    if ! command -v "$cmd" &>/dev/null; then
        error "'$cmd' not found. Install it with: sudo apt install $pkg"
    fi
    info "Found: $(command -v "$cmd")"
}

echo ""
echo -e "${BOLD}  inteiliDOS Build System${RESET}"
echo -e "${BOLD}  ========================${RESET}"
echo ""

info "Checking build dependencies..."
check_dep cmake        cmake
check_dep nasm         nasm
check_dep i686-elf-gcc "gcc (cross-compiler -- see BUILD.md for setup)"
check_dep i686-elf-ld  "binutils (cross -- see BUILD.md)"

HAVE_QEMU=0

# ---------------------------------------------------------------------------
# Auto-install grub-mkrescue + xorriso (required for ISO output)
# ---------------------------------------------------------------------------
ensure_iso_tools() {
    # macOS Homebrew ships i686-elf-grub-mkrescue; Linux ships grub-mkrescue
    local have_grub=0
    command -v i686-elf-grub-mkrescue &>/dev/null && have_grub=1
    command -v grub-mkrescue          &>/dev/null && have_grub=1

    local missing=()
    [[ "${have_grub}" == "0" ]]             && missing+=(grub-mkrescue)
    command -v xorriso &>/dev/null || missing+=(xorriso)
    [[ ${#missing[@]} -eq 0 ]] && return 0

    warn "Missing ISO tools: ${missing[*]}"
    if [[ "$(uname)" == "Darwin" ]]; then
        if command -v brew &>/dev/null; then
            info "Installing via Homebrew: i686-elf-grub xorriso ..."
            brew install i686-elf-grub xorriso
        else
            error "Homebrew not found. Install it from https://brew.sh then re-run ./build.sh"
        fi
    else
        info "Installing via apt: grub-pc-bin grub-common xorriso ..."
        sudo apt-get install -y grub-pc-bin grub-common xorriso
    fi
}

info "Checking ISO tools (grub-mkrescue + xorriso)..."
ensure_iso_tools
info "ISO tools ready"

if command -v qemu-system-i386 &>/dev/null; then
    info "QEMU found -- 'run' target available"
    HAVE_QEMU=1
else
    warn "qemu-system-i386 not found -- install with: brew install qemu  (macOS) or sudo apt install qemu-system-x86"
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
echo ""
info "Configuring with CMake..."
cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}"  \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE=Release \
      "$@"          # pass-through any extra -D flags

# ---------------------------------------------------------------------------
# Build (ISO is part of ALL, so this produces kernel + ISO in one step)
# ---------------------------------------------------------------------------
echo ""
info "Building inteilidOS kernel + bootable ISO..."
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
cmake --build "${BUILD_DIR}" --parallel "${CORES}"

# ---------------------------------------------------------------------------
# Result summary
# ---------------------------------------------------------------------------
echo ""
success "Build complete!"
echo ""
echo -e "  Kernel ELF:   ${BUILD_DIR}/inteilidOS.elf"
echo -e "  Kernel BIN:   ${BUILD_DIR}/inteilidOS.bin"
echo -e "  Bootable ISO: ${BUILD_DIR}/inteilidOS.iso"
echo ""

if [[ "${HAVE_QEMU}" == "1" ]]; then
    echo -e "${CYAN}Run in QEMU from ISO:${RESET}"
    echo -e "  qemu-system-i386 -cdrom ${BUILD_DIR}/inteilidOS.iso -m 128 -serial stdio"
    echo ""
    echo -e "${CYAN}Or via cmake target:${RESET}"
    echo -e "  cmake --build ${BUILD_DIR} --target run-iso"
    echo ""
else
    echo -e "${YELLOW}To burn to a USB drive:${RESET}"
    echo -e "  dd if=${BUILD_DIR}/inteilidOS.iso of=/dev/sdX bs=4M status=progress"
    echo ""
fi

echo -e "${BOLD}  \"The Classic Command Line, Reimagined.\"${RESET}"
echo ""
