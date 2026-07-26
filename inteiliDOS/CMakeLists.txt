# =============================================================================
# inteilidOS -- CMakeLists.txt
# Cross-compiles a bare-metal x86 (IA-32) kernel using an i686-elf toolchain.
#
# Build targets:
#   modern  (default) — i686, -O2, GRUB2 El Torito no-emulation ISO
#   legacy            — i486, -O1, GRUB2 legacy ISO + raw 1.44 MB floppy image
#
# Invoke via build.sh (recommended) or manually:
#   cmake -S . -B build_modern -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
#         -DBUILD_TARGET=modern -DCMAKE_BUILD_TYPE=Release
#   cmake -S . -B build_legacy -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake \
#         -DBUILD_TARGET=legacy -DCMAKE_BUILD_TYPE=Release
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(inteilidOS ASM C CXX)

# ---------------------------------------------------------------------------
# Build target selection
# ---------------------------------------------------------------------------
set(BUILD_TARGET "modern" CACHE STRING "Build target: 'modern' (i686/-O2) or 'legacy' (i486/-O1)")
set_property(CACHE BUILD_TARGET PROPERTY STRINGS "modern" "legacy")

# Whether genisoimage/mkisofs is available (set by build.sh; can be forced manually)
set(HAVE_GENISOIMAGE "0" CACHE STRING "1 if genisoimage or mkisofs is present")

if(NOT BUILD_TARGET STREQUAL "modern" AND NOT BUILD_TARGET STREQUAL "legacy")
    message(FATAL_ERROR "BUILD_TARGET must be 'modern' or 'legacy', got: ${BUILD_TARGET}")
endif()

message(STATUS "Build target: ${BUILD_TARGET}")

# ---------------------------------------------------------------------------
# Toolchain validation
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER MATCHES "/usr/bin/cc$")
    message(WARNING
        "You appear to be using the host compiler.\n"
        "For a proper bare-metal build run:  ./build.sh\n"
        "or supply -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake manually.")
endif()

# ---------------------------------------------------------------------------
# Project-wide compiler / assembler / linker flags
# ---------------------------------------------------------------------------

# Common flags shared by both targets
set(KERNEL_CFLAGS_COMMON
    -m32
    -nostdlib
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -fno-pic
    -fno-pie
    -Wall
    -Wextra
    -std=gnu11
)

if(BUILD_TARGET STREQUAL "legacy")
    # ── Legacy: i486 — safe for 486DX, Pentium, Pentium MMX ─────────────────
    # -march=i486 : no CMOV, no P6 branch-prediction hints, no SSE
    # -O1         : basic optimisations only; avoids advanced scheduling that
    #               can produce instructions crashing pre-Pentium-Pro CPUs
    set(KERNEL_CFLAGS
        ${KERNEL_CFLAGS_COMMON}
        -march=i486
        -O1
    )
    message(STATUS "Compiler flags: -march=i486 -O1  (486DX / Pentium compatible)")
else()
    # ── Modern: i686 — Pentium Pro and later ─────────────────────────────────
    set(KERNEL_CFLAGS
        ${KERNEL_CFLAGS_COMMON}
        -march=i686
        -O2
    )
    message(STATUS "Compiler flags: -march=i686 -O2  (Pentium Pro and later)")
endif()

set(KERNEL_ASFLAGS
    -f elf32
)

set(KERNEL_LDFLAGS
    -m elf_i386
    -nostdlib
    -T ${CMAKE_SOURCE_DIR}/linker.ld
)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(BOOT_ASM_SOURCES
    boot/boot.asm
    boot/gdt_flush.asm
    boot/idt_load.asm
    boot/isr_stubs.asm
)

set(KERNEL_C_SOURCES
    kernel/kernel.c
    kernel/vga.c
    kernel/gdt.c
    kernel/idt.c
    kernel/isr.c
    kernel/timer.c
    kernel/keyboard.c
    kernel/memory.c
    kernel/ata.c
    kernel/pci.c
    kernel/usb.c
    kernel/cdrom.c
)

set(SHELL_C_SOURCES
    shell/shell.c
    shell/commands.c
    shell/iedit.c
    shell/tour.c
    shell/basic.c
)

# ---------------------------------------------------------------------------
# Assemble .asm → .o using NASM
# ---------------------------------------------------------------------------
find_program(NASM nasm REQUIRED
    HINTS /usr/bin /usr/local/bin
    DOC "NASM assembler")

set(ASM_OBJECTS "")
foreach(ASM_SRC IN LISTS BOOT_ASM_SOURCES)
    get_filename_component(ASM_NAME ${ASM_SRC} NAME_WE)
    get_filename_component(ASM_DIR  ${ASM_SRC} DIRECTORY)
    set(OBJ "${CMAKE_CURRENT_BINARY_DIR}/${ASM_DIR}/${ASM_NAME}.o")

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${ASM_DIR}")

    add_custom_command(
        OUTPUT  ${OBJ}
        COMMAND ${NASM} ${KERNEL_ASFLAGS} -o ${OBJ}
                        ${CMAKE_CURRENT_SOURCE_DIR}/${ASM_SRC}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${ASM_SRC}
        COMMENT "Assembling ${ASM_SRC}"
    )
    list(APPEND ASM_OBJECTS ${OBJ})
endforeach()

add_custom_target(asm_objects DEPENDS ${ASM_OBJECTS})

# ---------------------------------------------------------------------------
# MBR boot sector (modern — LBA48): flat binary + C header
# ---------------------------------------------------------------------------
set(MBR_BIN    ${CMAKE_CURRENT_BINARY_DIR}/boot/mbr.bin)
set(MBR_HEADER ${CMAKE_CURRENT_BINARY_DIR}/shell/mbr_data.h)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boot")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shell")

add_custom_command(
    OUTPUT  ${MBR_BIN}
    COMMAND ${NASM} -f bin -o ${MBR_BIN}
                    ${CMAKE_SOURCE_DIR}/boot/mbr.asm
    DEPENDS ${CMAKE_SOURCE_DIR}/boot/mbr.asm
    COMMENT "Assembling modern MBR (LBA48 flat binary)"
)

add_custom_command(
    OUTPUT  ${MBR_HEADER}
    COMMAND python3
                ${CMAKE_SOURCE_DIR}/cmake/bin2header.py
                ${MBR_BIN} mbr_data ${MBR_HEADER}
    DEPENDS ${MBR_BIN} ${CMAKE_SOURCE_DIR}/cmake/bin2header.py
    COMMENT "Generating mbr_data.h from MBR binary"
)

add_custom_target(mbr_header DEPENDS ${MBR_HEADER})

# ---------------------------------------------------------------------------
# MBR boot sector (legacy — CHS): flat binary only
# Always built so setup.c can reference it; only embedded in floppy for legacy
# ---------------------------------------------------------------------------
set(MBR_LEGACY_BIN ${CMAKE_CURRENT_BINARY_DIR}/boot/mbr_legacy.bin)

add_custom_command(
    OUTPUT  ${MBR_LEGACY_BIN}
    COMMAND ${NASM} -f bin -o ${MBR_LEGACY_BIN}
                    ${CMAKE_SOURCE_DIR}/boot/mbr_legacy.asm
    DEPENDS ${CMAKE_SOURCE_DIR}/boot/mbr_legacy.asm
    COMMENT "Assembling legacy CHS MBR (flat binary)"
)

add_custom_target(mbr_legacy_bin DEPENDS ${MBR_LEGACY_BIN})

# ---------------------------------------------------------------------------
# Compile C sources → static library
# ---------------------------------------------------------------------------
add_library(kernel_c STATIC
    ${KERNEL_C_SOURCES}
    ${SHELL_C_SOURCES}
)

target_include_directories(kernel_c PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/shell
    ${CMAKE_CURRENT_BINARY_DIR}/shell
)

add_dependencies(kernel_c mbr_header)

target_compile_options(kernel_c PRIVATE
    ${KERNEL_CFLAGS}
)

set_target_properties(kernel_c PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(kernel_c asm_objects)

# ---------------------------------------------------------------------------
# Final link: inteilidOS.elf → inteilidOS.bin
# ---------------------------------------------------------------------------
find_program(LD_CROSS i686-elf-ld
    HINTS /usr/local/bin /opt/cross/bin
    DOC "i686-elf cross linker")
if(NOT LD_CROSS)
    find_program(LD_CROSS ld REQUIRED)
    message(WARNING "i686-elf-ld not found; using host ld — link will likely fail.")
endif()

find_program(OBJCOPY_CROSS i686-elf-objcopy
    HINTS /usr/local/bin /opt/cross/bin
    DOC "i686-elf objcopy")
if(NOT OBJCOPY_CROSS)
    find_program(OBJCOPY_CROSS objcopy REQUIRED)
endif()

set(KERNEL_ELF ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.elf)
set(KERNEL_BIN ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.bin)

add_custom_command(
    OUTPUT  ${KERNEL_ELF}
    COMMAND ${LD_CROSS}
            ${KERNEL_LDFLAGS}
            -o ${KERNEL_ELF}
            ${ASM_OBJECTS}
            --whole-archive $<TARGET_FILE:kernel_c> --no-whole-archive
    DEPENDS kernel_c asm_objects ${CMAKE_SOURCE_DIR}/linker.ld
    COMMENT "Linking inteilidOS.elf"
)

add_custom_command(
    OUTPUT  ${KERNEL_BIN}
    COMMAND ${OBJCOPY_CROSS} -O binary ${KERNEL_ELF} ${KERNEL_BIN}
    DEPENDS ${KERNEL_ELF}
    COMMENT "Generating inteilidOS.bin"
)

add_custom_target(inteilidOS ALL DEPENDS ${KERNEL_ELF} ${KERNEL_BIN})

# ---------------------------------------------------------------------------
# grub-mkrescue (required for both modern and legacy ISO)
# ---------------------------------------------------------------------------
find_program(GRUB_MKRESCUE
    NAMES i686-elf-grub-mkrescue grub-mkrescue
    HINTS /usr/bin /usr/local/bin /opt/homebrew/bin
    DOC "grub-mkrescue tool")

if(NOT GRUB_MKRESCUE)
    message(FATAL_ERROR
        "grub-mkrescue not found.\n"
        "  macOS: brew install i686-elf-grub xorriso\n"
        "  Linux: sudo apt install grub-pc-bin grub-common xorriso\n"
        "Then re-run ./build.sh")
endif()

# ---------------------------------------------------------------------------
# ── MODERN ISO ──────────────────────────────────────────────────────────────
# ---------------------------------------------------------------------------
if(BUILD_TARGET STREQUAL "modern")

    set(ISO_DIR      ${CMAKE_CURRENT_BINARY_DIR}/iso)
    set(GRUB_DIR     ${ISO_DIR}/boot/grub)
    set(KERNEL_ISO   ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.iso)

    add_custom_command(
        OUTPUT  ${KERNEL_ISO}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GRUB_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${KERNEL_ELF} ${ISO_DIR}/boot/inteilidOS.elf
        COMMAND ${CMAKE_COMMAND} -E copy
                    ${CMAKE_SOURCE_DIR}/grub/grub.cfg ${GRUB_DIR}/grub.cfg
        COMMAND ${GRUB_MKRESCUE} --output=${KERNEL_ISO} ${ISO_DIR}
        DEPENDS ${KERNEL_ELF} ${CMAKE_SOURCE_DIR}/grub/grub.cfg
        COMMENT "Building modern bootable ISO: inteilidOS.iso"
    )

    add_custom_target(iso ALL DEPENDS ${KERNEL_ISO})
    message(STATUS "Modern ISO: ${KERNEL_ISO}")

# ---------------------------------------------------------------------------
# ── LEGACY ISO + FLOPPY IMAGE ───────────────────────────────────────────────
# ---------------------------------------------------------------------------
else() # BUILD_TARGET == "legacy"

    # ── 2a. GRUB2 legacy ISO (El Torito no-emulation, legacy GRUB config) ────
    set(ISO_DIR_LEG  ${CMAKE_CURRENT_BINARY_DIR}/iso_legacy)
    set(GRUB_DIR_LEG ${ISO_DIR_LEG}/boot/grub)
    set(KERNEL_ISO   ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS_legacy.iso)

    add_custom_command(
        OUTPUT  ${KERNEL_ISO}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GRUB_DIR_LEG}
        COMMAND ${CMAKE_COMMAND} -E copy ${KERNEL_ELF} ${ISO_DIR_LEG}/boot/inteilidOS.elf
        COMMAND ${CMAKE_COMMAND} -E copy
                    ${CMAKE_SOURCE_DIR}/grub/grub_legacy.cfg ${GRUB_DIR_LEG}/grub.cfg
        # Use only i386-pc modules — skip EFI / VESA to keep the GRUB core small
        # and compatible with old BIOSes.  biosdisk + multiboot are all we need.
        COMMAND ${GRUB_MKRESCUE}
                    --output=${KERNEL_ISO}
                    --install-modules="biosdisk part_msdos multiboot normal"
                    ${ISO_DIR_LEG}
        DEPENDS ${KERNEL_ELF} ${CMAKE_SOURCE_DIR}/grub/grub_legacy.cfg
        COMMENT "Building legacy GRUB2 ISO: inteilidOS_legacy.iso"
    )

    add_custom_target(iso ALL DEPENDS ${KERNEL_ISO})
    message(STATUS "Legacy GRUB2 ISO: ${KERNEL_ISO}")

    # ── 2b. Raw 1.44 MB floppy image ─────────────────────────────────────────
    # Layout: [sector 0: mbr_legacy.bin (512 B)] [sectors 1-N: kernel binary]
    # Compatible with:
    #   • El Torito floppy-emulation boot (1.44 MB virtual floppy)
    #   • Writing directly to a physical 3.5" floppy (dd if=… of=/dev/fd0)
    #   • USB drive used as a floppy emulator
    set(FLOPPY_IMG ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS_floppy.img)

    add_custom_command(
        OUTPUT  ${FLOPPY_IMG}
        # 1. Create blank 1.44 MB image (2880 × 512 bytes = 1,474,560 bytes)
        COMMAND dd if=/dev/zero of=${FLOPPY_IMG} bs=512 count=2880
        # 2. Write CHS boot sector at byte 0
        COMMAND dd if=${MBR_LEGACY_BIN} of=${FLOPPY_IMG} bs=512 count=1 conv=notrunc
        # 3. Append kernel binary starting at sector 1 (byte 512)
        COMMAND dd if=${KERNEL_BIN} of=${FLOPPY_IMG} bs=512 seek=1 conv=notrunc
        DEPENDS ${MBR_LEGACY_BIN} ${KERNEL_BIN}
        COMMENT "Building 1.44 MB legacy floppy image: inteilidOS_floppy.img"
    )

    add_custom_target(floppy ALL DEPENDS ${FLOPPY_IMG})
    message(STATUS "Floppy image: ${FLOPPY_IMG}")

    # ── 2c. El Torito floppy-emulation ISO (requires genisoimage / mkisofs) ──
    # This ISO presents the floppy image as a virtual 1.44 MB floppy via the
    # El Torito "floppy emulation" boot catalogue entry.  Every El Torito-
    # capable BIOS from 1995 onward supports this, making it the most
    # compatible CD-ROM boot method for mid-1990s hardware.
    if(HAVE_GENISOIMAGE STREQUAL "1")
        find_program(GENISOIMAGE
            NAMES genisoimage mkisofs
            HINTS /usr/bin /usr/local/bin /opt/homebrew/bin
            DOC "genisoimage or mkisofs for El Torito floppy-emulation ISO")

        if(GENISOIMAGE)
            set(FLOPPY_ISO ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS_floppy_eltorito.iso)

            add_custom_command(
                OUTPUT  ${FLOPPY_ISO}
                COMMAND ${GENISOIMAGE}
                            -o ${FLOPPY_ISO}
                            -b inteilidOS_floppy.img   # El Torito boot image (relative path inside the staging area)
                            -boot-load-size 4          # load 4 × 512 = 2 KB (covers the entire boot sector + extra)
                            -boot-info-table           # patch El Torito info table into the boot image
                            -no-emul-boot              # no-emul so the BIOS loads our boot sector verbatim
                            -V "INTEILIDOSLEG"         # volume label (max 16 chars, no spaces)
                            -J -R                      # Joliet + Rock Ridge (wider OS support)
                            ${CMAKE_CURRENT_BINARY_DIR} # input directory (contains inteilidOS_floppy.img)
                DEPENDS ${FLOPPY_IMG}
                COMMENT "Building El Torito no-emulation ISO: inteilidOS_floppy_eltorito.iso"
            )

            add_custom_target(floppy-iso ALL DEPENDS ${FLOPPY_ISO})
            message(STATUS "El Torito no-emulation ISO: ${FLOPPY_ISO}")
        endif()
    endif()

endif() # BUILD_TARGET

# ---------------------------------------------------------------------------
# QEMU run targets
# ---------------------------------------------------------------------------
find_program(QEMU qemu-system-i386 HINTS /usr/bin /usr/local/bin /opt/homebrew/bin)

if(QEMU)
    if(BUILD_TARGET STREQUAL "modern")
        # Direct-kernel boot (Multiboot1, no ISO)
        add_custom_target(run
            COMMAND ${QEMU}
                -kernel ${KERNEL_ELF}
                -m 128
                -serial stdio
                -no-reboot
                -display curses
            DEPENDS inteilidOS
            COMMENT "Running inteilidOS in QEMU (direct kernel boot, modern)"
        )
        add_custom_target(run-debug
            COMMAND ${QEMU}
                -kernel ${KERNEL_ELF}
                -m 128
                -serial stdio
                -no-reboot
                -display curses
                -s -S
            DEPENDS inteilidOS
            COMMENT "Running inteilidOS in QEMU (GDB debug mode, port 1234)"
        )
        if(GRUB_MKRESCUE)
            add_custom_target(run-iso
                COMMAND ${QEMU}
                    -cdrom ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.iso
                    -m 128
                    -serial stdio
                    -no-reboot
                    -display curses
                DEPENDS iso
                COMMENT "Running inteilidOS in QEMU via modern ISO"
            )
        endif()

    else() # legacy
        # Legacy ISO — emulate a Pentium CPU (no Pentium Pro instructions)
        add_custom_target(run-legacy
            COMMAND ${QEMU}
                -cdrom ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS_legacy.iso
                -cpu pentium
                -m 64
                -serial stdio
                -no-reboot
                -display curses
            DEPENDS iso
            COMMENT "Running inteilidOS legacy ISO in QEMU (CPU: Pentium)"
        )
        # Boot directly from the raw floppy image
        add_custom_target(run-floppy
            COMMAND ${QEMU}
                -drive file=${CMAKE_CURRENT_BINARY_DIR}/inteilidOS_floppy.img,format=raw,if=floppy
                -cpu pentium
                -m 64
                -serial stdio
                -no-reboot
                -display curses
            DEPENDS floppy
            COMMENT "Running inteilidOS floppy image in QEMU (CPU: Pentium)"
        )
        # Direct-kernel boot for quick iteration (still uses i486 kernel binary)
        add_custom_target(run
            COMMAND ${QEMU}
                -kernel ${KERNEL_ELF}
                -cpu pentium
                -m 64
                -serial stdio
                -no-reboot
                -display curses
            DEPENDS inteilidOS
            COMMENT "Running inteilidOS in QEMU (direct kernel boot, legacy)"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Status summary
# ---------------------------------------------------------------------------
message(STATUS "inteilidOS build configured")
message(STATUS "  Build target : ${BUILD_TARGET}")
message(STATUS "  NASM    : ${NASM}")
message(STATUS "  LD      : ${LD_CROSS}")
message(STATUS "  OBJCOPY : ${OBJCOPY_CROSS}")
message(STATUS "  Outputs : ${KERNEL_ELF}, ${KERNEL_BIN}")
