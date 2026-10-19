# Sourced by build.sh after successful image compilation (Bash 3.2 compatible).
offer_media_writing() {
    if [[ ! -t 0 || "${USE_DEFAULTS}" -eq 1 ]]; then
        info "Physical media writing skipped (unattended/defaults mode)."
        return
    fi
    local answer kind image
    while :; do
        willburd "Would you like to write an image to physical media?"
        [[ "${BOOT_MEDIA}" -eq 2 || "${BOOT_MEDIA}" -eq 3 ]] &&
            echo "  I. ISO: burn a CD/DVD and/or flash a USB drive"
        [[ "${BOOT_MEDIA}" -eq 1 || "${BOOT_MEDIA}" -eq 3 ]] &&
            echo "  F. Floppy image: write a physical floppy disk"
        echo "  N. Finish without writing more media (default)"
        if ! read -r -p "Choose [I/F/N]: " answer; then return; fi
        case "$answer" in
            i|I)
                if [[ "${BOOT_MEDIA}" -ne 2 && "${BOOT_MEDIA}" -ne 3 ]]; then
                    warn "This build did not produce an ISO.";continue
                fi
                image="${BUILD_DIR}/inteiliDOS_1990s.iso"
                willburd "Burn the ISO to a blank CD/DVD? [y/N]"
                if ! read -r answer; then return; fi
                case "$answer" in y|Y|yes|YES)
                    if ! python3 "${SCRIPT_DIR}/scripts/write_media.py" --kind optical --image "$image"; then
                        warn "Optical writing did not complete. The compiled image is unchanged."
                    fi ;;
                esac
                willburd "Would you also like to flash the ISO to a USB drive? [y/N]"
                warn "Flashing erases that drive's files and partitions. Bootability depends on the ISO and BIOS."
                if ! read -r answer; then return; fi
                kind=usb
                ;;
            f|F)
                if [[ "${BOOT_MEDIA}" -ne 1 && "${BOOT_MEDIA}" -ne 3 ]]; then
                    warn "This build did not produce a floppy image.";continue
                fi
                image="${BUILD_DIR}/inteiliDOS_1990s_floppy.img";kind=floppy
                willburd "Write the floppy image to a physical floppy disk? [y/N]"
                warn "The selected disk's existing contents will be erased."
                if ! read -r answer; then return; fi
                ;;
            ""|n|N|no|NO) return ;;
            *) warn "Choose I, F or N.";continue ;;
        esac
        case "$answer" in y|Y|yes|YES)
            if ! python3 "${SCRIPT_DIR}/scripts/write_media.py" --kind "$kind" --image "$image"; then
                warn "Media writing did not complete. Compilation still succeeded."
            fi ;;
        esac
    done
}
