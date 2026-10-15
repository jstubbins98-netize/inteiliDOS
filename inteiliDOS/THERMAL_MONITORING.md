# Background thermal monitoring

The kernel starts the monitor at boot. It starts a sensor check every 500 ms,
whether or not `HEALTH` is open. SMBus transactions advance in small,
non-blocking steps on the host timer interrupt. There are no wait loops,
heap allocations, screen writes or BIOS calls in the interrupt handler.
Monitoring also continues during PC-speaker PCM playback and the software
MS-DOS emulator, which uses the host timer without replacing it.
The PCM timer path is covered by a deterministic IRQ-handler test;
end-to-end PCM playback in QEMU remains unverified because the emulator
stalled monitor requests during the playback fixture.

Run `HEALTH` and select **4 Cooling** (or use Tab/Left/Right). The tab shows
temperature channels, CPU temperature when wiring is verified, fan-header
RPM/status, sample age, monitoring errors and a latched temperature warning.
Leaving the dashboard does not stop the monitor.

## Important limitation: fan activation is NOT implemented

This is monitoring and warning support, **not guaranteed overheating
protection**. It cannot automatically start, stop or boost the fans on the
requested computers. Existing BIOS/embedded-controller fan control is left
unchanged. Do not disable firmware thermal protection or rely on this
monitor to prevent damage.

There is no universal BIOS call to obtain temperature or switch fans on
1990s PCs. Support depends on the monitoring chip, controller and board
wiring. Arbitrary OEM fan-register writes could stop cooling, reset hardware
or conflict with the firmware.

The implemented backend reads W83781D, W83782D and AS99127F monitors at SMBus
address `0x2D`, through an Intel PIIX4 (`8086:7113`) controller **already
enabled by firmware**. It does not enable controllers or sensors, change
limits, run a bus-wide scan, reset hardware or write fan/PWM registers.
It validates chip/vendor IDs and restores the sensor's bank selector after
each cycle, including best-effort restoration after transfer errors.
Disabled sensors and failed/stale readings are unavailable, not 0 degrees.

| Requested target | Current scope |
| --- | --- |
| HP Vectra VEi8 | 86Box models an AS99127F with remote channel 3 assigned to CPU. Monitoring works only with the supported, firmware-enabled SMBus path. AS99127F fan controls are undocumented and are not used. |
| HP Pavilion | 86Box Pavilion 85xx models a W83781D with CPU on channel 3. Other Pavilion models are not automatically covered. W83781D has no PWM fan-control outputs. |
| Compaq Presario / Deskpro | These are product families, not one hardware interface. Matching supported chips/controllers can be monitored; other configurations are unavailable. No blanket compatibility claim. |
| Early IBM ThinkPad / IBM Aptiva | No embedded-controller or model-specific thermal driver is included. Do not access undocumented ThinkPad EC registers. Unsupported configurations show unavailable. |
| Packard Bell Legend 822CDT | No verified thermal interface or fan-control driver is included; unavailable unless the supported chip/controller combination is present. |
| Gateway 2000 | No brand-wide interface exists; the same chip/controller restrictions apply. |

Source references:

- [Linux W83781D/AS99127F documentation](https://www.kernel.org/doc/html/latest/hwmon/w83781d.html):
  chip identification, fan divisor/RPM conversion and documented limitations.
  The Linux driver does not implement AS99127F PWM because its controls are
  insufficiently understood. A W83782D's documented PWM registers do not
  establish the wiring or safe control policy of an arbitrary OEM board.
- [Linux i2c-piix4 driver](https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-piix4.c):
  PIIX4 byte-data transactions and status bits.
- [86Box Slot 1 board definitions](https://github.com/86Box/86Box/blob/master/src/machine/m_at_slot1.c):
  `machine_at_vei8_init` and `machine_at_pavilion85xx_init` describe their
  sensor and fan-channel wiring. Emulator readings are simulated hardware,
  not the temperature of the computer hosting 86Box.

## Sensor assignment and warnings

SMBIOS 2.x identification in the low BIOS-memory window can identify HP
VEi8 / Pavilion 85xx CPU channel 3. If SMBIOS is absent or ambiguous, the
dashboard still shows genuine numbered temperature channels, but **does not
label one CPU temperature by guessing**. For a separately verified board
wiring, a developer can define `CONFIG_THERMAL_CPU_CHANNEL` as `1`, `2` or `3`;
the default `0` means unknown. This setting does not add fan-control support.

Remote sensors' half-degree readings are conservatively rounded up to the
displayed integer Celsius value; 65.5 C therefore displays as 66 C and
cannot incorrectly clear a warning at 65 C.

The advisory defaults are **75 C to raise a warning** and **65 C to clear
it**. They are not manufacturer-certified limits, automatic shutdown
temperatures or fan-control thresholds. A warning is raised when any
available channel reaches 75 C. It clears only after all three channels
provide valid readings at or below 65 C. Missing readings never clear an
existing warning. Consult the specific computer's specifications; older
processors may require lower limits. These constants are in
`kernel/thermal.h`.

`running cooling fan...` is displayed only when a fresh tachometer reading
shows rotation. `not running / no tach pulses` means a fresh counter reports
no rotation; the header may instead be unused or lack a tachometer wire.
Neither message means inteiliDOS commanded the fan. Unknown/failed/stale
readings say unavailable. The dashboard explicitly reports fan control as
unavailable.

The monitor requires working host timer interrupts. Native software that
disables interrupts, an OS hang, or a hardware fault can prevent checks.
Firmware-controlled cooling must remain the primary independent safeguard.
