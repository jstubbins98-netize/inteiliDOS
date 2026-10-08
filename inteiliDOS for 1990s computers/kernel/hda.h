/*
 * kernel/hda.h — Intel High Definition Audio (HDA) capture driver
 *
 * Provides two functions consumed by the CLOAD cassette loader:
 *
 *   hda_has_signal()         — returns 1 if mic/line-in is active
 *   hda_capture_kcs_byte()   — decodes one KCS-framed byte from the stream
 *
 * Call hda_init() once (from hda_has_signal on first use, internally) before
 * either capture function is used.  The driver is silent if no HDA controller
 * is found on the PCI bus.
 *
 * PCI identification:
 *   Class 0x04 (Multimedia), Subclass 0x03 (HDA-compatible controller)
 *
 * KCS encoding (Kansas City Standard):
 *   0 bit  → 1 cycle  of 1200 Hz  (half-period ≈ 3.3 samples at 8 kHz)
 *   1 bit  → 2 cycles of 2400 Hz  (half-period ≈ 1.7 samples at 8 kHz)
 *   Frame  → start(0)  D0–D7  stop(1)  [8N1, LSB first]
 */

#ifndef HDA_H
#define HDA_H

#include <stdint.h>

/* Initialise the HDA controller and arm the capture stream.
 * Safe to call multiple times — subsequent calls are no-ops.
 * Returns 1 if a controller was found and initialised, 0 otherwise. */
int  hda_init(void);

/* Returns 1 if the capture stream has audio above the noise floor,
 * 0 if the input is silent or the controller is not present.        */
int  hda_has_signal(void);

/* Block until one KCS-framed byte is decoded from the capture stream,
 * or until an internal timeout expires (~2 s per byte at 1200 baud).
 *
 * Return values:
 *   0–255  decoded byte (success)
 *   -1     framing error: stop bit was not a 1-bit (data corruption)
 *   -2     timeout: no start-bit or data-bit arrived within the window
 *          (normal end-of-stream, or a brief audio dropout)           */
int  hda_capture_kcs_byte(void);

#endif /* HDA_H */
