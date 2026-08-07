#!/usr/bin/env bash
# =============================================================================
# inteilidOS -- build.sh
# Configures and builds inteilidOS using CMake + a bare-metal i686-elf
# toolchain.  Run from the inteilidOS/ directory.
#
# Usage:
#   ./build.sh            — interactive: asks modern or legacy
#   ./build.sh --modern   — non-interactive modern build
#   ./build.sh --legacy   — non-interactive legacy build
#   ./build.sh [cmake args] — extra -D flags forwarded to cmake
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/i686-elf.cmake"

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; DIM='\033[2m'; RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }
step()    { echo -e "\n${BOLD}${CYAN}▶ $*${RESET}"; }

# ---------------------------------------------------------------------------
# Parse --modern / --legacy flags (strip them before forwarding to cmake)
# ---------------------------------------------------------------------------
BUILD_TARGET=""
CMAKE_EXTRA_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --modern) BUILD_TARGET="modern" ;;
        --legacy) BUILD_TARGET="legacy" ;;
        *)        CMAKE_EXTRA_ARGS+=("$arg") ;;
    esac
done

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}  ╔══════════════════════════════════════╗${RESET}"
echo -e "${BOLD}  ║       inteiliDOS  Build  System      ║${RESET}"
echo -e "${BOLD}  ╚══════════════════════════════════════╝${RESET}"
echo ""

# ---------------------------------------------------------------------------
# Step 1 — Dependency checks
# ---------------------------------------------------------------------------
step "Checking build dependencies"

check_dep() {
    local cmd="$1" pkg="${2:-$1}"
    if ! command -v "$cmd" &>/dev/null; then
        error "'$cmd' not found.  Install with: sudo apt install $pkg"
    fi
    info "Found: $(command -v "$cmd")"
}

check_dep cmake        cmake
check_dep nasm         nasm
check_dep i686-elf-gcc "gcc (cross-compiler — see BUILD.md for setup)"
check_dep i686-elf-ld  "binutils (cross — see BUILD.md)"

# ---------------------------------------------------------------------------
# Step 2 — Ensure ISO / floppy-image tools
# ---------------------------------------------------------------------------
step "Checking ISO tools"

ensure_iso_tools() {
    local have_grub=0
    command -v i686-elf-grub-mkrescue &>/dev/null && have_grub=1
    command -v grub-mkrescue          &>/dev/null && have_grub=1

    local missing=()
    [[ "${have_grub}" == "0" ]]         && missing+=(grub-mkrescue)
    command -v xorriso &>/dev/null      || missing+=(xorriso)

    [[ ${#missing[@]} -eq 0 ]] && return 0

    warn "Missing ISO tools: ${missing[*]}"
    if [[ "$(uname)" == "Darwin" ]]; then
        command -v brew &>/dev/null \
            || error "Homebrew not found.  Install from https://brew.sh then re-run."
        info "Installing via Homebrew: i686-elf-grub xorriso"
        brew install i686-elf-grub xorriso
    else
        info "Installing via apt: grub-pc-bin grub-common xorriso"
        sudo apt-get install -y grub-pc-bin grub-common xorriso
    fi
}

ensure_iso_tools
success "ISO tools ready"

# Check for genisoimage (needed for El Torito floppy-emulation ISO)
HAVE_GENISOIMAGE=0
if command -v genisoimage &>/dev/null || command -v mkisofs &>/dev/null; then
    HAVE_GENISOIMAGE=1
    success "genisoimage / mkisofs found — floppy-emulation ISO available"
else
    warn "genisoimage / mkisofs not found — floppy-emulation ISO will be skipped"
    warn "  Install with: sudo apt install genisoimage   (Linux)"
    warn "                brew install cdrtools           (macOS)"
fi

# Check for QEMU
HAVE_QEMU=0
if command -v qemu-system-i386 &>/dev/null; then
    success "QEMU found — 'run' targets available"
    HAVE_QEMU=1
else
    warn "qemu-system-i386 not found — install with:"
    warn "  brew install qemu                       (macOS)"
    warn "  sudo apt install qemu-system-x86        (Linux)"
fi

# ---------------------------------------------------------------------------
# Step 3 — Choose build target (interactive if not supplied on command line)
# ---------------------------------------------------------------------------
step "Selecting build target"

if [[ -z "${BUILD_TARGET}" ]]; then
    echo ""
    echo -e "  ${BOLD}Which hardware platform are you targeting?${RESET}"
    echo ""
    echo -e "  ${GREEN}[1] Pentium III${RESET}  — HP Vectra VEi8 with Pentium III  (500–600 MHz)"
    echo -e "      Compiler : -march=pentium3 -O2  (i686 + MMX + SSE)"
    echo -e "      Boot     : GRUB2 El Torito no-emulation ISO"
    echo -e "      Output   : ${DIM}build_for_pentium3/inteilidOS.iso${RESET}"
    echo ""
    echo -e "  ${YELLOW}[2] Pentium II${RESET}   — HP Vectra VEi8 with Pentium II   (233–450 MHz)"
    echo -e "      Compiler : -march=pentium2 -O2  (i686 + MMX, no SSE)"
    echo -e "      Boot     : GRUB2 El Torito no-emulation ISO"
    echo -e "      Output   : ${DIM}build_for_pentium2/inteilidOS_legacy.iso${RESET}"
    echo -e "                 ${DIM}build_for_pentium2/inteilidOS_floppy.img  (raw 1.44 MB)${RESET}"
    echo ""

    while true; do
        read -rp "  Enter choice [1/2]: " choice
        case "$choice" in
            1) BUILD_TARGET="modern"; break ;;
            2) BUILD_TARGET="legacy"; break ;;
            *) echo -e "  ${RED}Invalid choice — enter 1 or 2.${RESET}" ;;
        esac
    done
fi

# Validate
case "${BUILD_TARGET}" in
    modern|legacy) ;;
    *) error "Unknown BUILD_TARGET '${BUILD_TARGET}'. Use 'modern' or 'legacy'." ;;
esac

if [[ "${BUILD_TARGET}" == "modern" ]]; then
    BUILD_DIR="${SCRIPT_DIR}/build_for_pentium3"
else
    BUILD_DIR="${SCRIPT_DIR}/build_for_pentium2"
fi

echo ""
if [[ "${BUILD_TARGET}" == "modern" ]]; then
    echo -e "  ${GREEN}▶ Pentium III build selected${RESET}"
    echo -e "  ${DIM}Targeting HP Vectra VEi8 / Pentium III (500–600 MHz).${RESET}"
    echo -e "  ${DIM}Compiler flags: -march=pentium3 -O2  (i686 + MMX + SSE)${RESET}"
    echo -e "  ${DIM}Output: ${BUILD_DIR}/inteilidOS.iso${RESET}"
else
    echo -e "  ${YELLOW}▶ Pentium II build selected${RESET}"
    echo -e "  ${DIM}Targeting HP Vectra VEi8 / Pentium II (233–450 MHz).${RESET}"
    echo -e "  ${DIM}Compiler flags: -march=pentium2 -O2  (i686 + MMX, no SSE)${RESET}"
    echo -e "  ${DIM}Output: ${BUILD_DIR}/inteilidOS_legacy.iso${RESET}"
    echo -e "  ${DIM}        ${BUILD_DIR}/inteilidOS_floppy.img${RESET}"
fi
echo ""

# ---------------------------------------------------------------------------
# Step 4 — CMake configure
# ---------------------------------------------------------------------------
step "Configuring with CMake (BUILD_TARGET=${BUILD_TARGET})"

cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}"  \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TARGET="${BUILD_TARGET}" \
      -DHAVE_GENISOIMAGE="${HAVE_GENISOIMAGE}" \
      "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}"

# ---------------------------------------------------------------------------
# Step 5 — Build
# ---------------------------------------------------------------------------
step "Building inteilidOS"
CPU_COUNT="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build "${BUILD_DIR}" -- -j"${CPU_COUNT}"

# ---------------------------------------------------------------------------
# Step 6 — Summary
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}  ╔══════════════════════════════════════╗${RESET}"
echo -e "${BOLD}  ║           Build Complete             ║${RESET}"
echo -e "${BOLD}  ╚══════════════════════════════════════╝${RESET}"
echo ""

if [[ "${BUILD_TARGET}" == "modern" ]]; then
    ISO="${BUILD_DIR}/inteilidOS.iso"
    echo -e "  ${BOLD}Target  :${RESET} Modern x86  (Pentium Pro / i686 and later)"
    echo -e "  ${BOLD}Compiler:${RESET} -march=i686 -O2"
    echo ""
    if [[ -f "${ISO}" ]]; then
        SIZE=$(du -sh "${ISO}" | cut -f1)
        success "ISO image ready: ${ISO}  (${SIZE})"
    fi
    echo ""
    echo -e "  ${BOLD}Burn to CD-R:${RESET}"
    echo -e "  ${DIM}  wodim -v dev=/dev/sr0 -dao speed=4 ${ISO}${RESET}"
    echo ""
    if [[ "${HAVE_QEMU}" == "1" ]]; then
        echo -e "  ${BOLD}Test in QEMU:${RESET}"
        echo -e "  ${DIM}  cmake --build ${BUILD_DIR} --target run-iso${RESET}"
    fi
else
    ISO="${BUILD_DIR}/inteilidOS_legacy.iso"
    FLOPPY="${BUILD_DIR}/inteilidOS_floppy.img"
    echo -e "  ${BOLD}Target  :${RESET} Legacy x86  (486DX / Pentium / Pentium MMX)"
    echo -e "  ${BOLD}Compiler:${RESET} -march=i486 -O1"
    echo ""
    if [[ -f "${ISO}" ]]; then
        SIZE=$(du -sh "${ISO}" | cut -f1)
        success "GRUB2 legacy ISO : ${ISO}  (${SIZE})"
    fi
    if [[ -f "${FLOPPY}" ]]; then
        SIZE=$(du -sh "${FLOPPY}" | cut -f1)
        success "Raw floppy image : ${FLOPPY}  (${SIZE})"
    fi
    echo ""
    echo -e "  ${BOLD}Burn ISO to CD-R (preferred):${RESET}"
    echo -e "  ${DIM}  wodim -v dev=/dev/sr0 -dao speed=4 ${ISO}${RESET}"
    echo ""
    echo -e "  ${BOLD}Write floppy image to physical disk:${RESET}"
    echo -e "  ${DIM}  dd if=${FLOPPY} of=/dev/fd0 bs=512${RESET}"
    echo ""
    echo -e "  ${BOLD}Write floppy image to USB (emulates 1.44 MB floppy):${RESET}"
    echo -e "  ${DIM}  dd if=${FLOPPY} of=/dev/sdX bs=512${RESET}"
    echo ""
    if [[ "${HAVE_QEMU}" == "1" ]]; then
        echo -e "  ${BOLD}Test in QEMU with 486 CPU emulation:${RESET}"
        echo -e "  ${DIM}  cmake --build ${BUILD_DIR} --target run-legacy${RESET}"
        echo ""
        echo -e "  ${BOLD}Test floppy image directly:${RESET}"
        echo -e "  ${DIM}  cmake --build ${BUILD_DIR} --target run-floppy${RESET}"
    fi
fi

echo ""

# ---------------------------------------------------------------------------
# Step 7 — Burn the ISO to a blank CD-R
# ---------------------------------------------------------------------------
step "CD-R burning"

# Resolve the ISO we just built
if [[ "${BUILD_TARGET}" == "modern" ]]; then
    BURN_ISO="${BUILD_DIR}/inteilidOS.iso"
else
    BURN_ISO="${BUILD_DIR}/inteilidOS_legacy.iso"
fi

echo ""
echo -e "  ${BOLD}ISO ready to burn:${RESET}  ${BURN_ISO}"
echo -e "  ${DIM}Insert a blank CD-R into your optical drive before proceeding.${RESET}"
echo ""

read -rp "  Burn this ISO to a CD-R now? [y/N]: " burn_answer
if [[ "$(echo "$burn_answer" | tr '[:upper:]' '[:lower:]')" != "y" ]]; then
    info "CD-R burn skipped."
    echo ""
    echo -e "  To burn manually later:"
    if [[ "$(uname)" == "Darwin" ]]; then
        echo -e "  ${DIM}  hdiutil burn -speed min \"${BURN_ISO}\"${RESET}"
    else
        echo -e "  ${DIM}  wodim -v speed=1 dev=/dev/sr0 -data \"${BURN_ISO}\"${RESET}"
    fi
    echo ""
else
    echo ""
    if [[ "$(uname)" == "Darwin" ]]; then
        # ── macOS — hdiutil burn ──────────────────────────────────────────────
        # -speed min requests the lowest speed the drive supports (per man hdiutil).
        # hdiutil handles drive selection itself; if no disc or drive is found it
        # will print a clear error rather than hanging silently.
        step "Burning ISO via hdiutil (lowest available speed)"
        info "hdiutil burn -speed min \"${BURN_ISO}\""
        echo ""
        if hdiutil burn -speed min "${BURN_ISO}"; then
            echo ""
            success "CD-R burn complete."
        else
            echo ""
            error "hdiutil burn failed — make sure a blank CD-R is inserted and the drive is ready."
        fi
    else
        # ── Linux — wodim / cdrecord ──────────────────────────────────────────
        # Prefer wodim (the Debian/Ubuntu default); fall back to cdrecord.
        BURN_CMD=""
        if command -v wodim &>/dev/null; then
            BURN_CMD="wodim"
        elif command -v cdrecord &>/dev/null; then
            BURN_CMD="cdrecord"
        fi

        if [[ -z "${BURN_CMD}" ]]; then
            warn "No CD burning tool found (wodim or cdrecord)."
            warn "  Install with:  sudo apt install wodim   (Debian / Ubuntu)"
            warn "                 sudo dnf install wodim   (Fedora)"
            echo ""
        else
            # Detect the first available optical drive (/dev/sr0, /dev/sr1, …)
            OPTICAL_DEV=""
            for candidate in /dev/sr0 /dev/sr1 /dev/sr2 /dev/cdrom /dev/dvd; do
                if [[ -e "$candidate" ]]; then
                    OPTICAL_DEV="$candidate"
                    break
                fi
            done

            if [[ -z "${OPTICAL_DEV}" ]]; then
                warn "No optical drive found under /dev/sr* or /dev/cdrom."
                warn "  Check that the drive is connected and recognised by the kernel."
                echo -e "  ${DIM}  ${BURN_CMD} -v speed=1 dev=/dev/sr0 -data \"${BURN_ISO}\"${RESET}"
                echo ""
            else
                # speed=1 → 1× (150 KB/s) — the lowest standard CD-R write speed.
                # Most drives silently bump to their own minimum if 1× is below
                # what the drive or disc supports; that is fine.
                step "Burning ISO via ${BURN_CMD} at 1× (lowest speed) to ${OPTICAL_DEV}"
                info "${BURN_CMD} -v speed=1 dev=${OPTICAL_DEV} -data \"${BURN_ISO}\""
                echo ""
                if sudo "${BURN_CMD}" -v speed=1 dev="${OPTICAL_DEV}" -data "${BURN_ISO}"; then
                    echo ""
                    success "CD-R burn complete — disc is ready to boot."
                else
                    echo ""
                    error "${BURN_CMD} failed — make sure a blank CD-R is inserted and the drive is writable."
                fi
            fi
        fi
    fi
fi

echo ""
