# =============================================================================
# inteilidOS -- CMakeLists.txt
# Cross-compiles a bare-metal x86 (IA-32) kernel using an i686-elf toolchain.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(inteilidOS ASM C CXX)

# ---------------------------------------------------------------------------
# Toolchain validation
# ---------------------------------------------------------------------------
# This project MUST be built with a bare-metal i686-elf (or i386-elf)
# toolchain.  build.sh passes -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake.
# If the user is configuring manually, remind them.
if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER MATCHES "/usr/bin/cc$")
    message(WARNING
        "You appear to be using the host compiler.\n"
        "For a proper bare-metal build run:  ./build.sh\n"
        "or supply -DCMAKE_TOOLCHAIN_FILE=cmake/i686-elf.cmake manually.")
endif()

# ---------------------------------------------------------------------------
# Project-wide flags
# ---------------------------------------------------------------------------
set(KERNEL_CFLAGS
    -m32
    -march=i686
    -nostdlib
    -ffreestanding
    -fno-builtin
    -fno-stack-protector
    -fno-pic
    -fno-pie
    -Wall
    -Wextra
    -O2
    -std=gnu11
)

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

    # Ensure output directory exists
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
# MBR boot sector: assemble to flat binary, then convert to C header
# ---------------------------------------------------------------------------
set(MBR_BIN    ${CMAKE_CURRENT_BINARY_DIR}/boot/mbr.bin)
set(MBR_HEADER ${CMAKE_CURRENT_BINARY_DIR}/shell/mbr_data.h)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/boot")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shell")

# Step 1: nasm -f bin → mbr.bin  (flat 512-byte image)
add_custom_command(
    OUTPUT  ${MBR_BIN}
    COMMAND ${NASM} -f bin -o ${MBR_BIN}
                    ${CMAKE_SOURCE_DIR}/boot/mbr.asm
    DEPENDS ${CMAKE_SOURCE_DIR}/boot/mbr.asm
    COMMENT "Assembling MBR boot sector (flat binary)"
)

# Step 2: bin2header.py → mbr_data.h
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
# Compile C sources → object files
# ---------------------------------------------------------------------------
# We create a static library from the C files so CMake handles dependency
# tracking, then link everything together with ld.
add_library(kernel_c STATIC
    ${KERNEL_C_SOURCES}
    ${SHELL_C_SOURCES}
)

target_include_directories(kernel_c PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/kernel
    ${CMAKE_SOURCE_DIR}/shell
    ${CMAKE_CURRENT_BINARY_DIR}/shell   # for generated mbr_data.h
)

add_dependencies(kernel_c mbr_header)

# Apply bare-metal flags (remove any host defaults first)
target_compile_options(kernel_c PRIVATE
    ${KERNEL_CFLAGS}
)

# Suppress host linker being invoked for the static lib
set_target_properties(kernel_c PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(kernel_c asm_objects)

# ---------------------------------------------------------------------------
# Final link step: produce inteilidOS.elf then flatten to .bin
# ---------------------------------------------------------------------------
find_program(LD_CROSS i686-elf-ld
    HINTS /usr/local/bin /opt/cross/bin
    DOC "i686-elf cross linker")

if(NOT LD_CROSS)
    # Fallback to host ld (will probably fail on macOS / x86_64 Linux)
    find_program(LD_CROSS ld REQUIRED)
    message(WARNING "i686-elf-ld not found; using host ld -- link may fail.")
endif()

find_program(OBJCOPY_CROSS i686-elf-objcopy
    HINTS /usr/local/bin /opt/cross/bin
    DOC "i686-elf objcopy")
if(NOT OBJCOPY_CROSS)
    find_program(OBJCOPY_CROSS objcopy REQUIRED)
endif()

set(KERNEL_ELF ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.elf)
set(KERNEL_BIN ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.bin)

# Collect all .o from the static library
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
# ISO image (requires grub-mkrescue + xorriso)
# ---------------------------------------------------------------------------
# macOS Homebrew installs i686-elf-grub-mkrescue; Linux installs grub-mkrescue
find_program(GRUB_MKRESCUE
    NAMES i686-elf-grub-mkrescue grub-mkrescue
    HINTS /usr/bin /usr/local/bin /opt/homebrew/bin
    DOC "grub-mkrescue tool")

if(NOT GRUB_MKRESCUE)
    message(FATAL_ERROR
        "grub-mkrescue not found.\n"
        "  macOS:  brew install i686-elf-grub xorriso\n"
        "  Linux:  sudo apt install grub-pc-bin grub-common xorriso\n"
        "Then re-run ./build.sh")
endif()

set(ISO_DIR  ${CMAKE_CURRENT_BINARY_DIR}/iso)
set(GRUB_DIR ${ISO_DIR}/boot/grub)
set(KERNEL_ISO ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.iso)

add_custom_command(
    OUTPUT  ${KERNEL_ISO}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GRUB_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${KERNEL_ELF} ${ISO_DIR}/boot/inteilidOS.elf
    COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/grub/grub.cfg ${GRUB_DIR}/grub.cfg
    COMMAND ${GRUB_MKRESCUE} --output=${KERNEL_ISO} ${ISO_DIR}
    DEPENDS ${KERNEL_ELF} ${CMAKE_SOURCE_DIR}/grub/grub.cfg
    COMMENT "Building bootable ISO: inteilidOS.iso"
)

# Part of ALL so every plain build produces the ISO automatically
add_custom_target(iso ALL DEPENDS ${KERNEL_ISO})
message(STATUS "grub-mkrescue: ${GRUB_MKRESCUE} -- ISO built automatically")

# ---------------------------------------------------------------------------
# QEMU run targets (convenience)
# ---------------------------------------------------------------------------
find_program(QEMU qemu-system-i386 HINTS /usr/bin /usr/local/bin /opt/homebrew/bin)
if(QEMU)
    # Direct-kernel boot -- no ISO required (Multiboot1 is supported natively by QEMU)
    add_custom_target(run
        COMMAND ${QEMU}
            -kernel ${KERNEL_ELF}
            -m 128
            -serial stdio
            -no-reboot
            -display curses
        DEPENDS inteilidOS
        COMMENT "Running inteilidOS in QEMU (direct kernel boot)"
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
    # ISO-based boot (only available when grub-mkrescue is present)
    if(GRUB_MKRESCUE)
        add_custom_target(run-iso
            COMMAND ${QEMU}
                -cdrom ${CMAKE_CURRENT_BINARY_DIR}/inteilidOS.iso
                -m 128
                -serial stdio
                -no-reboot
                -display curses
            DEPENDS iso
            COMMENT "Running inteilidOS in QEMU via ISO"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Status summary
# ---------------------------------------------------------------------------
message(STATUS "inteilidOS build configured")
message(STATUS "  NASM:    ${NASM}")
message(STATUS "  LD:      ${LD_CROSS}")
message(STATUS "  OBJCOPY: ${OBJCOPY_CROSS}")
message(STATUS "  Outputs: ${KERNEL_ELF}, ${KERNEL_BIN}")
