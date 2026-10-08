/*
 * kernel/ac97.h — AC'97 audio capture driver (Crystal CS4281 / generic AC'97)
 *
 * Provides two functions consumed by the CLOAD cassette loader:
 *
 *   ac97_has_signal()         — returns 1 if mic/line-in is active
 *   ac97_capture_kcs_byte()   — decodes one KCS-framed byte from the stream
 *
 * Call ac97_init() once (from ac97_has_signal on first use, internally)
 * before either capture function is used.  The driver is silent if no
 * supported AC'97 device is found on the PCI bus.
 *
 * PCI identification (in order of preference):
 *   Vendor 0x1013 / Device 0x4281  — Crystal Semiconductor CS4281
 *   Class  0x04   / Subclass 0x01  — Generic AC'97 audio controller
 *
 * The HP Vectra VEi8 (1998) ships with a Crystal CS4281 or compatible
 * AC'97 PCI codec.  This driver targets that hardware; the Intel HDA driver
 * (kernel/hda.c) is not applicable because HDA was introduced in 2004.
 *
 * KCS encoding (Kansas City Standard):
 *   0 bit  → 1 cycle  of 1200 Hz  (half-period ≈ 3.3 samples at 8 kHz)
 *   1 bit  → 2 cycles of 2400 Hz  (half-period ≈ 1.7 samples at 8 kHz)
 *   Frame  → start(0)  D0–D7  stop(1)  [8N1, LSB first]
 */

#ifndef AC97_H
#define AC97_H

#include <stdint.h>

/* Initialise the AC'97 controller and arm the capture stream.
 * Safe to call multiple times — subsequent calls are no-ops.
 * Returns 1 if a controller was found and initialised, 0 otherwise. */
int  ac97_init(void);

/* Returns 1 if the capture stream has audio above the noise floor,
 * 0 if the input is silent or no supported controller is present.   */
int  ac97_has_signal(void);

/* Block until one KCS-framed byte is decoded from the capture stream,
 * or until an internal timeout expires (~2 s per byte at 1200 baud).
 *
 * Return values:
 *   0–255  decoded byte (success)
 *   -1     framing error: stop bit was not a 1-bit (data corruption)
 *   -2     timeout: no start-bit or data-bit arrived within the window
 *          (normal end-of-stream, or a brief audio dropout)           */
int  ac97_capture_kcs_byte(void);

#endif /* AC97_H */
