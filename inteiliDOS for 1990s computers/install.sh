#!/usr/bin/env bash
# =============================================================================
# inteilidOS -- install.sh
# Automatically downloads, builds, and installs the i686-elf cross-compiler
# toolchain required to build inteilidOS.
#
# Installs:
#   - binutils  (assembler, linker, objcopy, etc.)
#   - gcc       (C / C++ compiler targeting bare-metal i686)
#
# Default install prefix: $HOME/opt/cross
# Override:  PREFIX=/usr/local/cross ./install.sh
#
# Supported hosts: Ubuntu/Debian, Fedora/RHEL, Arch Linux, macOS (Homebrew)
# =============================================================================
set -euo pipefail

# ---------------------------------------------------------------------------
# Versions (update here to pick up newer releases)
# ---------------------------------------------------------------------------
BINUTILS_VER="${BINUTILS_VER:-2.41}"
GCC_VER="${GCC_VER:-13.2.0}"
TARGET="${TARGET:-i686-elf}"
PREFIX="${PREFIX:-$HOME/opt/cross}"

# Mirror base URLs
BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz"

# Working directories
SRC_DIR="${SRC_DIR:-$HOME/src/inteilidOS-toolchain}"
BUILD_BINUTILS="${SRC_DIR}/build-binutils"
BUILD_GCC="${SRC_DIR}/build-gcc"

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; RESET=''
fi

step()    { echo -e "\n${CYAN}${BOLD}>>> $*${RESET}"; }
info()    { echo -e "    ${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "    ${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "    ${YELLOW}[WARN]${RESET}  $*"; }
die()     { echo -e "\n${RED}[FATAL]${RESET} $*\n" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Detect OS and install system packages
# ---------------------------------------------------------------------------
install_system_deps() {
    step "Installing system build dependencies"

    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Detect distro
        if command -v apt-get &>/dev/null; then
            info "Detected Debian/Ubuntu — using apt-get"
            sudo apt-get update -qq
            sudo apt-get install -y \
                build-essential \
                cmake \
                nasm \
                libgmp-dev \
                libmpfr-dev \
                libmpc-dev \
                libisl-dev \
                texinfo \
                bison \
                flex \
                wget \
                tar \
                xorriso \
                grub-pc-bin \
                grub-common \
                qemu-system-x86 \
                mtools \
                2>/dev/null || true

        elif command -v dnf &>/dev/null; then
            info "Detected Fedora/RHEL — using dnf"
            sudo dnf install -y \
                gcc gcc-c++ make cmake nasm \
                gmp-devel mpfr-devel libmpc-devel isl-devel \
                texinfo bison flex wget tar \
                xorriso grub2-tools qemu-system-x86 \
                2>/dev/null || true

        elif command -v pacman &>/dev/null; then
            info "Detected Arch Linux — using pacman"
            sudo pacman -Sy --noconfirm \
                base-devel cmake nasm \
                gmp libmpc mpfr \
                texinfo bison flex wget \
                xorriso grub qemu-system-x86 \
                2>/dev/null || true

        elif command -v zypper &>/dev/null; then
            info "Detected openSUSE — using zypper"
            sudo zypper install -y \
                gcc gcc-c++ make cmake nasm \
                gmp-devel mpfr-devel libmpc-devel \
                texinfo bison flex wget tar \
                xorriso grub2-i386-pc qemu-x86 \
                2>/dev/null || true
        else
            warn "Unknown Linux distro — skipping automatic package install."
            warn "Ensure these are installed: gcc g++ make cmake nasm wget"
            warn "  gmp-devel mpfr-devel libmpc-devel texinfo bison flex"
        fi

    elif [[ "$OSTYPE" == "darwin"* ]]; then
        info "Detected macOS — using Homebrew"
        if ! command -v brew &>/dev/null; then
            die "Homebrew is not installed. Visit https://brew.sh to install it."
        fi
        brew install cmake nasm wget gmp mpfr libmpc isl \
                     texinfo bison flex xorriso qemu 2>/dev/null || true

    else
        warn "Unsupported OS: $OSTYPE — skipping automatic package install."
    fi

    success "System dependencies ready"
}

# ---------------------------------------------------------------------------
# Helpers: download with wget or curl
# ---------------------------------------------------------------------------
download() {
    local url="$1" dest="$2"
    if [[ -f "$dest" ]]; then
        info "Already downloaded: $(basename "$dest")"
        return
    fi
    info "Downloading $(basename "$url")..."
    if command -v wget &>/dev/null; then
        wget --quiet --show-progress -O "$dest" "$url"
    elif command -v curl &>/dev/null; then
        curl -# -L -o "$dest" "$url"
    else
        die "Neither wget nor curl found. Install one and retry."
    fi
    success "Download complete: $(basename "$dest")"
}

# ---------------------------------------------------------------------------
# Number of parallel jobs
# ---------------------------------------------------------------------------
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
info "Using ${JOBS} parallel build jobs"

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║       inteilidOS — i686-elf Toolchain Installer              ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${RESET}"
echo ""
echo -e "  Target   : ${CYAN}${TARGET}${RESET}"
echo -e "  Prefix   : ${CYAN}${PREFIX}${RESET}"
echo -e "  binutils : ${CYAN}${BINUTILS_VER}${RESET}"
echo -e "  GCC      : ${CYAN}${GCC_VER}${RESET}"
echo -e "  Sources  : ${CYAN}${SRC_DIR}${RESET}"
echo ""

# ---------------------------------------------------------------------------
# Early-exit if toolchain already installed
# ---------------------------------------------------------------------------
if [[ -x "${PREFIX}/bin/${TARGET}-gcc" ]]; then
    INSTALLED_VER=$("${PREFIX}/bin/${TARGET}-gcc" --version 2>&1 | head -1)
    warn "Toolchain already installed: ${INSTALLED_VER}"
    read -rp "    Re-install? [y/N] " ans
    if [[ ! "$ans" =~ ^[Yy]$ ]]; then
        echo -e "\n${GREEN}Nothing to do. Toolchain is ready.${RESET}"
        echo -e "Add to your PATH:\n  export PATH=\"${PREFIX}/bin:\$PATH\"\n"
        exit 0
    fi
fi

# ---------------------------------------------------------------------------
# Step 1: system dependencies
# ---------------------------------------------------------------------------
install_system_deps

# ---------------------------------------------------------------------------
# Step 2: prepare directories
# ---------------------------------------------------------------------------
step "Preparing build directories"
mkdir -p "${SRC_DIR}" "${BUILD_BINUTILS}" "${BUILD_GCC}" "${PREFIX}"
success "Directories ready"

# ---------------------------------------------------------------------------
# Step 3: download sources
# ---------------------------------------------------------------------------
step "Downloading sources"
download "${BINUTILS_URL}" "${SRC_DIR}/binutils-${BINUTILS_VER}.tar.xz"
download "${GCC_URL}"      "${SRC_DIR}/gcc-${GCC_VER}.tar.xz"

# ---------------------------------------------------------------------------
# Step 4: extract sources
# ---------------------------------------------------------------------------
step "Extracting archives"

if [[ ! -d "${SRC_DIR}/binutils-${BINUTILS_VER}" ]]; then
    info "Extracting binutils..."
    tar -xf "${SRC_DIR}/binutils-${BINUTILS_VER}.tar.xz" -C "${SRC_DIR}"
    success "binutils extracted"
else
    info "binutils source already extracted"
fi

if [[ ! -d "${SRC_DIR}/gcc-${GCC_VER}" ]]; then
    info "Extracting GCC..."
    tar -xf "${SRC_DIR}/gcc-${GCC_VER}.tar.xz" -C "${SRC_DIR}"
    success "GCC extracted"
else
    info "GCC source already extracted"
fi

# ---------------------------------------------------------------------------
# Step 5: download GCC prerequisites (gmp, mpfr, mpc, isl)
# These are bundled inside the GCC tree via contrib/download_prerequisites.
# ---------------------------------------------------------------------------
step "Downloading GCC prerequisites (gmp, mpfr, mpc, isl)"
cd "${SRC_DIR}/gcc-${GCC_VER}"
if [[ ! -d "gmp" ]]; then
    contrib/download_prerequisites
    success "Prerequisites downloaded"
else
    info "Prerequisites already present"
fi
cd "${SRC_DIR}"

# ---------------------------------------------------------------------------
# Step 6: build binutils
# ---------------------------------------------------------------------------
step "Building binutils-${BINUTILS_VER} for ${TARGET}"
info "This usually takes 2–5 minutes..."

cd "${BUILD_BINUTILS}"
"${SRC_DIR}/binutils-${BINUTILS_VER}/configure" \
    --target="${TARGET}" \
    --prefix="${PREFIX}" \
    --with-sysroot \
    --disable-nls \
    --disable-werror \
    --disable-multilib

make -j"${JOBS}"
make install
success "binutils installed to ${PREFIX}"

# ---------------------------------------------------------------------------
# Step 7: build GCC (C + C++ front-ends)
# ---------------------------------------------------------------------------
step "Building GCC-${GCC_VER} for ${TARGET}"
info "This usually takes 10–30 minutes depending on your CPU..."

# GCC needs its own binutils on PATH
export PATH="${PREFIX}/bin:${PATH}"

cd "${BUILD_GCC}"
"${SRC_DIR}/gcc-${GCC_VER}/configure" \
    --target="${TARGET}" \
    --prefix="${PREFIX}" \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers \
    --disable-multilib \
    --disable-shared \
    --disable-libssp \
    --disable-libquadmath \
    --disable-libgomp

make -j"${JOBS}" all-gcc
make -j"${JOBS}" all-target-libgcc
make install-gcc
make install-target-libgcc
success "GCC installed to ${PREFIX}"

# ---------------------------------------------------------------------------
# Step 8: verify installation
# ---------------------------------------------------------------------------
step "Verifying toolchain"

TOOLS=(
    "${TARGET}-gcc"
    "${TARGET}-g++"
    "${TARGET}-ld"
    "${TARGET}-as"
    "${TARGET}-ar"
    "${TARGET}-objcopy"
    "${TARGET}-objdump"
    "${TARGET}-ranlib"
    "${TARGET}-strip"
)

ALL_OK=1
for tool in "${TOOLS[@]}"; do
    if [[ -x "${PREFIX}/bin/${tool}" ]]; then
        VER=$("${PREFIX}/bin/${tool}" --version 2>&1 | head -1)
        success "${tool}: ${VER}"
    else
        warn "MISSING: ${PREFIX}/bin/${tool}"
        ALL_OK=0
    fi
done

if [[ "${ALL_OK}" == "0" ]]; then
    die "Some tools are missing — see warnings above."
fi

# Quick compile smoke-test
step "Smoke-testing the compiler"
TMPDIR_TEST=$(mktemp -d)
cat > "${TMPDIR_TEST}/hello.c" <<'EOF'
/* bare-metal smoke test — no includes, no libc */
void kmain(void) {
    volatile int x = 42;
    (void)x;
}
EOF

"${PREFIX}/bin/${TARGET}-gcc" \
    -m32 -ffreestanding -nostdlib -nostdinc \
    -c "${TMPDIR_TEST}/hello.c" -o "${TMPDIR_TEST}/hello.o"

if [[ -f "${TMPDIR_TEST}/hello.o" ]]; then
    success "Compiler smoke-test passed (hello.o built successfully)"
else
    die "Compiler smoke-test FAILED — object file not produced"
fi
rm -rf "${TMPDIR_TEST}"

# ---------------------------------------------------------------------------
# Step 9: write shell environment snippet
# ---------------------------------------------------------------------------
ENVFILE="${PREFIX}/env.sh"
cat > "${ENVFILE}" <<EOF
# i686-elf cross-compiler environment for inteilidOS
# Auto-generated by inteilidOS/install.sh
export PATH="${PREFIX}/bin:\${PATH}"
EOF
success "Environment snippet written to ${ENVFILE}"

# ---------------------------------------------------------------------------
# Step 10: automatically add toolchain to the user's PATH
# ---------------------------------------------------------------------------
step "Adding toolchain to PATH"

EXPORT_LINE="export PATH=\"${PREFIX}/bin:\$PATH\""
MARKER="# inteilidOS i686-elf toolchain"
BLOCK="${MARKER}\n${EXPORT_LINE}"

# Build an ordered list of shell rc files to update.
# We check the running shell first, then fall back to common files.
RC_FILES=()

# 1. Current shell (most reliable)
CURRENT_SHELL="$(basename "${SHELL:-}")"
case "${CURRENT_SHELL}" in
    bash) RC_FILES+=("${HOME}/.bashrc" "${HOME}/.bash_profile") ;;
    zsh)  RC_FILES+=("${HOME}/.zshrc") ;;
    fish) RC_FILES+=("${HOME}/.config/fish/config.fish") ;;
    ksh)  RC_FILES+=("${HOME}/.kshrc") ;;
    *)    RC_FILES+=("${HOME}/.profile") ;;
esac

# 2. Also patch the other common ones so the toolchain is available
#    regardless of which shell the user switches to later.
for extra in "${HOME}/.bashrc" "${HOME}/.bash_profile" "${HOME}/.zshrc" "${HOME}/.profile"; do
    # Add only if it already exists and isn't already in the list
    if [[ -f "${extra}" ]]; then
        already=0
        for f in "${RC_FILES[@]}"; do [[ "$f" == "$extra" ]] && already=1 && break; done
        [[ "${already}" == "0" ]] && RC_FILES+=("${extra}")
    fi
done

PATCHED=0
for RC in "${RC_FILES[@]}"; do
    # Create the file if it belongs to the detected shell but doesn't exist yet
    if [[ ! -f "${RC}" ]]; then
        # Only create files for the user's actual shell, not extras
        if [[ "${RC}" == *"$(basename "${SHELL:-bash}")"* ]]; then
            touch "${RC}"
            info "Created ${RC}"
        else
            continue
        fi
    fi

    # Skip if this prefix is already present in the file
    if grep -qF "${PREFIX}/bin" "${RC}" 2>/dev/null; then
        info "Already present in ${RC} — skipping"
        continue
    fi

    # Append the export block
    printf "\n%b\n" "${BLOCK}" >> "${RC}"
    success "Added to ${RC}"
    PATCHED=1
done

if [[ "${PATCHED}" == "0" ]]; then
    info "All rc files already contained the toolchain PATH — nothing to patch"
fi

# Make the toolchain available in THIS script's session immediately
export PATH="${PREFIX}/bin:${PATH}"
success "Toolchain is now on PATH for this session"

# Confirm the binary is reachable
if command -v "${TARGET}-gcc" &>/dev/null; then
    LIVE_VER=$("${TARGET}-gcc" --version 2>&1 | head -1)
    success "Live check: ${LIVE_VER}"
else
    warn "${TARGET}-gcc not found on PATH — you may need to open a new terminal"
fi

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
echo ""
echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}║          Toolchain installation complete!                    ║${RESET}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${RESET}"
echo ""
echo -e "  ${GREEN}i686-elf-gcc ${GCC_VER}${RESET} installed at ${CYAN}${PREFIX}/bin${RESET}"
echo ""
echo -e "  PATH updated in:"
for RC in "${RC_FILES[@]}"; do
    [[ -f "${RC}" ]] && echo -e "    ${CYAN}${RC}${RESET}"
done
echo ""
echo -e "  ${BOLD}To apply in your current terminal:${RESET}"
echo -e "    ${CYAN}source ${ENVFILE}${RESET}"
echo -e "  ${BOLD}(new terminals will pick it up automatically)${RESET}"
echo ""
echo -e "  Then build inteilidOS with:"
echo -e "    ${CYAN}cd inteilidOS && ./build.sh${RESET}"
echo ""
echo -e "  ${BOLD}\"The Classic Command Line, Reimagined.\"${RESET}"
echo ""
