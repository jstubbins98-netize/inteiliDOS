#!/usr/bin/env bash
# Build inteiliDOS for 1990s Computers with a generated hardware profile.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_1990s"
CONFIG_DIR="${SCRIPT_DIR}/configurations"
CONFIG_HEADER="${CONFIG_DIR}/config.h"
TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/i686-elf.cmake"
CMAKE_EXTRA_ARGS=()
CLEAN=0
USE_DEFAULTS=0
CONFIGURE_ONLY=0
CONFIG_ONLY=0

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; MAGENTA='\033[0;35m'; BOLD='\033[1m'; RESET='\033[0m'

willburd() { echo -e "${MAGENTA}${BOLD}Willburd:${RESET} $*"; }
info()     { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success()  { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()     { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()    { echo -e "${RED}[ERROR]${RESET} $*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: ./build.sh [options] [extra CMake arguments]

Options:
  --clean            Remove build_1990s before configuring
  --defaults         Use the safest generic 1990s PC answers without prompting
  --config-only      Generate configurations/config.h, then stop
  --configure-only   Generate config.h and configure CMake, but do not compile
  --help             Show this help

The old --modern, --legacy, and --universal flags are accepted as aliases for
the universal i386-safe compiler target. Hardware behavior is selected through
Willburd's generated configurations/config.h file.
EOF
}

for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --defaults|--non-interactive) USE_DEFAULTS=1 ;;
        --config-only) CONFIG_ONLY=1 ;;
        --configure-only) CONFIGURE_ONLY=1 ;;
        --modern|--legacy|--universal) ;;
        --help|-h) usage; exit 0 ;;
        *) CMAKE_EXTRA_ARGS+=("$arg") ;;
    esac
done

if [[ "$CLEAN" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

show_hat() {
    cat <<'EOF'

                         /\
                        /  \
                       / /\ \
                      / /  \ \
                     /_/____\_\
                        ||||
                    .-========-.
                   /  WILLBURD  \
                  / SETUP WIZARD\
                  '-------------'
EOF
}

ask_menu() {
    local prompt="$1"
    local help_text="$2"
    local default="$3"
    shift 3
    local options=("$@")
    local answer i

    while true; do
        echo ""
        willburd "$prompt"
        for ((i=0; i<${#options[@]}; i++)); do
            printf "  %d) %s\n" "$((i + 1))" "${options[$i]}"
        done
        echo "  H) How do I find the correct answer?"
        printf "Choice [%s]: " "$default"
        IFS= read -r answer || answer="$default"
        answer="${answer:-$default}"
        case "$(uppercase "$answer")" in
            H|\?|HELP)
                echo ""
                willburd "$help_text"
                ;;
            *)
                if [[ "$answer" =~ ^[0-9]+$ ]] &&
                   (( answer >= 1 && answer <= ${#options[@]} )); then
                    REPLY="$answer"
                    return
                fi
                warn "Choose a number from 1 to ${#options[@]}, or H for help."
                ;;
        esac
    done
}

ask_number() {
    local prompt="$1"
    local help_text="$2"
    local default="$3"
    local min="$4"
    local max="$5"
    local answer

    while true; do
        echo ""
        willburd "$prompt"
        echo "  H) How do I find the correct answer?"
        printf "Number [%s]: " "$default"
        IFS= read -r answer || answer="$default"
        answer="${answer:-$default}"
        case "$(uppercase "$answer")" in
            H|\?|HELP)
                echo ""
                willburd "$help_text"
                ;;
            *)
                if [[ "$answer" =~ ^[0-9]+$ ]] &&
                   (( answer >= min && answer <= max )); then
                    REPLY="$answer"
                    return
                fi
                warn "Enter a whole number from ${min} to ${max}, or H for help."
                ;;
        esac
    done
}

safe_c_string() {
    printf '%s' "$1" | tr -cd '[:alnum:] _.-' | cut -c1-48
}

uppercase() {
    # Bash 3.2 (still shipped by older systems and macOS) does not support
    # Bash 4's built-in uppercase parameter expansion.
    printf '%s' "$1" | tr '[:lower:]' '[:upper:]'
}

set_default_profile() {
    PROFILE_NAME="Generic 1990s PC"
    CPU_CLASS=3
    RAM_MB=16
    BOOT_MEDIA=3
    ENABLE_PS2=1
    ENABLE_USB=0
    ENABLE_PCI=0
    ENABLE_PCI_IDE=0
    ENABLE_AHCI=0
    ENABLE_ATA=1
    ENABLE_ATAPI=1
    ENABLE_FLOPPY=1
    AUDIO_MODE=0
    TIMER_HZ=1000
}

run_wizard() {
    local answer
    set_default_profile
    show_hat
    echo ""
    willburd "Greetings, traveler! I am Willburd the Setup Wizard."
    willburd "I shall craft this inteiliDOS build for the computer you name."
    willburd "Every question has an H option if the hardware runes are unclear."
    echo ""

    while true; do
        echo ""
        willburd "What short name should I give this computer profile?"
        echo "  H) How do I find the correct answer?"
        printf "Name [Generic 1990s PC]: "
        IFS= read -r answer || answer=""
        case "$(uppercase "$answer")" in
            H|\?|HELP)
                willburd "This is only a label shown at boot. Use a model, room, or nickname, such as Office 486 or Basement Pentium."
                ;;
            *)
                PROFILE_NAME="$(safe_c_string "${answer:-Generic 1990s PC}")"
                [[ -n "$PROFILE_NAME" ]] || PROFILE_NAME="Generic 1990s PC"
                break
                ;;
        esac
    done

    ask_menu \
        "Which processor family does the target computer use?" \
        "Watch the first power-on screen, inspect the case badge, or enter BIOS Setup. Choose 386 if uncertain; every generated kernel still uses only 386 instructions." \
        1 \
        "Intel 80386 or compatible" \
        "Intel 80486 or compatible" \
        "Pentium or Pentium MMX" \
        "Pentium Pro, II, III, or compatible"
    case "$REPLY" in
        1) CPU_CLASS=3 ;;
        2) CPU_CLASS=4 ;;
        3) CPU_CLASS=5 ;;
        4) CPU_CLASS=6 ;;
    esac

    ask_number \
        "How many megabytes of RAM are installed?" \
        "The power-on memory count and BIOS Setup usually show RAM in KB or MB. Divide a KB value by 1024. If unknown, 16 MB is a conservative starting point." \
        16 4 4096
    RAM_MB="$REPLY"

    ask_menu \
        "How will this computer boot inteiliDOS?" \
        "Choose floppy for machines without BIOS CD boot. Choose CD-ROM if BIOS Setup offers CDROM in its boot order. Choose both if you want both images." \
        3 \
        "3.5-inch floppy" \
        "BIOS-bootable CD-ROM" \
        "Both floppy and CD-ROM"
    BOOT_MEDIA="$REPLY"

    ask_menu \
        "What keyboard connection will be used?" \
        "Round mini-DIN is PS/2. The larger 5-pin DIN AT keyboard also normally uses the 8042 path. USB is optional because this kernel supports UHCI only; PS/2 remains enabled as a safe fallback." \
        1 \
        "PS/2 or AT keyboard" \
        "PS/2 plus optional UHCI USB keyboard"
    case "$REPLY" in
        1) ENABLE_PS2=1; ENABLE_USB=0 ;;
        2) ENABLE_PS2=1; ENABLE_USB=1; ENABLE_PCI=1 ;;
    esac

    ask_menu \
        "Which hard-disk controller best matches the computer?" \
        "Choose IDE/PATA when BIOS Setup says IDE, ATA, PATA, Legacy, or Compatibility. Native PCI IDE, SATA/AHCI, SCSI, and proprietary controllers are not universal in this edition." \
        1 \
        "Legacy IDE/PATA compatibility ports" \
        "No supported hard disk"
    case "$REPLY" in
        1)
            ENABLE_ATA=1; ENABLE_PCI_IDE=0; ENABLE_AHCI=0
            ;;
        2)
            ENABLE_ATA=0; ENABLE_PCI_IDE=0; ENABLE_AHCI=0
            ;;
    esac

    if [[ "$ENABLE_ATA" -eq 1 ]]; then
        ask_menu \
            "Does the computer have an IDE/ATAPI CD-ROM drive?" \
            "BIOS Setup may list the drive model. A wide 40-pin ribbon cable normally indicates IDE/ATAPI. Choose no for SCSI, proprietary, or absent optical drives." \
            1 \
            "Yes" \
            "No"
        [[ "$REPLY" -eq 1 ]] && ENABLE_ATAPI=1 || ENABLE_ATAPI=0
    else
        ENABLE_ATAPI=0
        willburd "I shall disable ATAPI too, since it shares the unsupported IDE controller."
    fi

    ask_menu \
        "Does the computer have a standard PC floppy controller?" \
        "Choose yes for a BIOS-visible 3.5-inch or 5.25-inch drive attached by a 34-pin ribbon cable. Choose no for USB-only or proprietary external floppy drives." \
        1 \
        "Yes" \
        "No"
    [[ "$REPLY" -eq 1 ]] && ENABLE_FLOPPY=1 || ENABLE_FLOPPY=0

    ask_menu \
        "Which audio capture hardware should CLOAD try?" \
        "Choose none if cassette loading is not needed. AC'97 appears on many late-1990s PCI systems. Intel HDA is generally newer than the target era. Auto permits both optional probes." \
        1 \
        "None; PC speaker output only" \
        "AC'97 capture" \
        "Intel HDA capture" \
        "Auto-detect optional capture hardware"
    case "$REPLY" in
        1) AUDIO_MODE=0 ;;
        2) AUDIO_MODE=1; ENABLE_PCI=1 ;;
        3) AUDIO_MODE=2; ENABLE_PCI=1 ;;
        4) AUDIO_MODE=3; ENABLE_PCI=1 ;;
    esac

    TIMER_HZ=1000
    willburd "The PIT shall remain at the tested 1000 Hz rate so milliseconds stay accurate."
}

write_config() {
    mkdir -p "${CONFIG_DIR}"
    cat > "${CONFIG_HEADER}" <<EOF
/*
 * Generated by build.sh and Willburd the Setup Wizard.
 * Re-run build.sh to change this hardware profile.
 */
#ifndef INTEILIDOS_GENERATED_CONFIG_H
#define INTEILIDOS_GENERATED_CONFIG_H

#define CONFIG_PROFILE_NAME "${PROFILE_NAME}"
#define CONFIG_CPU_CLASS ${CPU_CLASS}
#define CONFIG_RAM_MB ${RAM_MB}
#define CONFIG_BOOT_MEDIA ${BOOT_MEDIA}

#define CONFIG_ENABLE_PS2_KEYBOARD ${ENABLE_PS2}
#define CONFIG_ENABLE_USB_KEYBOARD ${ENABLE_USB}
#define CONFIG_ENABLE_PCI ${ENABLE_PCI}
#define CONFIG_ENABLE_PCI_IDE ${ENABLE_PCI_IDE}
#define CONFIG_ENABLE_AHCI ${ENABLE_AHCI}
#define CONFIG_ENABLE_ATA ${ENABLE_ATA}
#define CONFIG_ENABLE_ATAPI_CDROM ${ENABLE_ATAPI}
#define CONFIG_ENABLE_FLOPPY ${ENABLE_FLOPPY}
#define CONFIG_AUDIO_MODE ${AUDIO_MODE}
#define CONFIG_TIMER_HZ ${TIMER_HZ}

#define CONFIG_AUDIO_NONE 0
#define CONFIG_AUDIO_AC97 1
#define CONFIG_AUDIO_HDA 2
#define CONFIG_AUDIO_AUTO 3

#define CONFIG_CPU_386 3
#define CONFIG_CPU_486 4
#define CONFIG_CPU_PENTIUM 5
#define CONFIG_CPU_P6 6

#define CONFIG_BOOT_FLOPPY 1
#define CONFIG_BOOT_CDROM 2
#define CONFIG_BOOT_BOTH 3

#if !CONFIG_ENABLE_PS2_KEYBOARD && !CONFIG_ENABLE_USB_KEYBOARD
#error "At least one keyboard driver must be enabled"
#endif

#if CONFIG_ENABLE_USB_KEYBOARD && !CONFIG_ENABLE_PCI
#error "UHCI USB keyboard support requires PCI probing"
#endif

#if CONFIG_ENABLE_AHCI && !CONFIG_ENABLE_PCI
#error "AHCI support requires PCI probing"
#endif

#endif
EOF
}

print_profile() {
    echo ""
    willburd "The spell is prepared. Here is your hardware profile:"
    echo "  Name             : ${PROFILE_NAME}"
    echo "  CPU class        : ${CPU_CLASS}86-family (kernel remains i386-safe)"
    echo "  Installed RAM    : ${RAM_MB} MB"
    echo "  Boot media       : ${BOOT_MEDIA} (1=floppy, 2=CD, 3=both)"
    echo "  PS/2 keyboard    : ${ENABLE_PS2}"
    echo "  USB keyboard     : ${ENABLE_USB}"
    echo "  PCI probing      : ${ENABLE_PCI}"
    echo "  PCI IDE probing  : ${ENABLE_PCI_IDE}"
    echo "  AHCI probing     : ${ENABLE_AHCI}"
    echo "  ATA disks        : ${ENABLE_ATA}"
    echo "  ATAPI CD-ROM     : ${ENABLE_ATAPI}"
    echo "  Floppy controller: ${ENABLE_FLOPPY}"
    echo "  Audio mode       : ${AUDIO_MODE}"
    echo "  PIT timer        : ${TIMER_HZ} Hz"
    echo "  Header           : ${CONFIG_HEADER}"
    echo ""
}

if [[ "$USE_DEFAULTS" -eq 1 || ! -t 0 ]]; then
    set_default_profile
    show_hat
    willburd "No interactive crystal ball detected; I shall use safe defaults."
else
    run_wizard
fi

write_config
print_profile

if [[ "$CONFIG_ONLY" -eq 1 ]]; then
    success "Generated hardware configuration: ${CONFIG_HEADER}"
    willburd "The configuration spell is complete. No compilation was requested."
    exit 0
fi

willburd "Now I shall inspect the workshop tools."
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
    error "grub-mkrescue is required to build the BIOS ISO."
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

willburd "The configuration grimoire is complete. I shall invoke CMake."
cmake -S "${SCRIPT_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TARGET=universal \
      -DINTEILIDOS_CONFIG_HEADER="${CONFIG_HEADER}" \
      -DHAVE_GENISOIMAGE="${HAVE_GENISOIMAGE}" \
      -DGRUB_MKRESCUE="${GRUB_MKRESCUE}" \
      "${CMAKE_EXTRA_ARGS[@]+"${CMAKE_EXTRA_ARGS[@]}"}"

if [[ "$CONFIGURE_ONLY" -eq 1 ]]; then
    success "Configuration generated and CMake configured."
    willburd "My work here is paused. Build later with: cmake --build \"${BUILD_DIR}\""
    exit 0
fi

CPU_COUNT="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"
willburd "By compiler, assembler, and ancient runes: let the build begin!"
cmake --build "${BUILD_DIR}" --target inteilidOS -- -j"${CPU_COUNT}"

if [[ "$BOOT_MEDIA" -eq 1 || "$BOOT_MEDIA" -eq 3 ]]; then
    cmake --build "${BUILD_DIR}" --target floppy -- -j"${CPU_COUNT}"
fi
if [[ "$BOOT_MEDIA" -eq 2 || "$BOOT_MEDIA" -eq 3 ]]; then
    cmake --build "${BUILD_DIR}" --target iso -- -j"${CPU_COUNT}"
fi

echo ""
success "Generated config:   ${CONFIG_HEADER}"
if [[ "$BOOT_MEDIA" -eq 2 || "$BOOT_MEDIA" -eq 3 ]]; then
    success "Universal BIOS ISO: ${BUILD_DIR}/inteiliDOS_1990s.iso"
fi
if [[ "$BOOT_MEDIA" -eq 1 || "$BOOT_MEDIA" -eq 3 ]]; then
    success "Raw floppy image:   ${BUILD_DIR}/inteiliDOS_1990s_floppy.img"
fi
willburd "The spell is complete. May your interrupts be orderly and your sectors readable!"
echo ""