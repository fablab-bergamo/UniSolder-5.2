# UniSolder holder sensor firmware

Standalone firmware for the **PIC12F615** sensor controller. It generates a PWM
carrier, sends a challenge pattern, samples the returned signal, and drives a
sleep/holder output according to whether the echo matches.

This is a separate microcontroller project: it does not run the station PID,
expose USB, or use the [PIC32 bootloader](../../front/US_BootLoader.X/README.md).
See the [software index](../../README.md) and [hardware documentation](../../../README.md)
for its relationship to the station.

## Behavior and connections

| Signal | Firmware GPIO | Purpose |
| --- | --- | --- |
| `TX` | GP1 | Transmitted challenge data |
| `RX` | GP4 | Sampled return data |
| `SLEEPOUT` | GP5 | Match/missing-echo output |

These names describe the firmware mapping; use the hardware schematics to identify
physical connector/programming pins and electrical connections.

`main.c` configures Timer2/CCP PWM, then repeatedly exchanges the four-byte
**`WaKe`** pattern least-significant bit first. A frame uses a 6 ms leader,
32 bits with two 600 us phases each, and a 100 ms inter-frame delay. RX is
sampled into the shifting 32-bit word during transmission.

A matching echo sets `SLEEPOUT` high and resets the miss counter. Eight consecutive
nonmatching frames set it low; the first seven misses retain the previous output.
The PWM period uses `PR2 = 52`; the source's approximate 40 kHz label is not a
precision frequency specification. Verify the carrier and data waveform after
changing oscillator, timer, compiler, or optimization settings.

## Environment and build

- Open `US_sensor.X` as an MPLAB X project.
- Target **PIC12F615**, configuration **`default`**.
- The checked-in project records **XC8 1.30**, uses the legacy compiler command
  format, and selects a PICkit3 programmer. Use the matching legacy tools or
  deliberately migrate/regenerate the project; current XC8 command-line options
  should not be assumed compatible with these makefiles.
- `xc.h` and the legacy `GenericTypeDefs.h` must be available through the compiler
  or configured include paths. `_XTAL_FREQ` and configuration bits both select
  an **8 MHz** timing baseline.

Build a production image in the IDE. From this project directory, with the
MPLAB make tools and compiler paths configured:

```sh
make -f nbproject/Makefile-default.mk SUBPROJECTS= .build-conf
```

Output: `dist/default/production/US_sensor.X.production.hex`.

The project requests the legacy compiler's `pro` mode. If that mode is unavailable
in the selected toolchain, change the build settings explicitly and recheck
generated code and waveform timing. The recorded compiler/mode is project
metadata, not a claim that a modern installation has been validated.

## Programming and source files

Program the PIC12F615 using a compatible PIC programmer and the sensor hardware's
programming connections. The Windows application cannot upload this HEX file.

| File | Responsibility |
| --- | --- |
| `main.c` | GPIO/PWM initialization, blocking challenge/echo loop, miss counter |
| `config.h` | Oscillator and device configuration bits |
| `Makefile`, `nbproject/` | MPLAB project and generated build configuration |

There is no foreground scheduler or interrupt-driven protocol here: timing uses
blocking compiler delay routines. Watchdog and brown-out settings are explicitly
disabled in `config.h`; do not assume automatic timeout recovery is enabled.

## Verification and troubleshooting

No sensor CI build or automated protocol tests are configured. The main-firmware
PID tests and formatting hook do not cover this component.

Verify the carrier, leader/bit timing, input sampling, valid-echo assertion,
eight-miss deassertion, and recovery on a later valid echo on actual sensor
hardware. Record toolchain and oscillator settings with results.

Missing legacy compiler options/includes usually indicate a toolchain mismatch.
Wrong delays call for checking `_XTAL_FREQ`, configuration bits, and generated
code. A permanently low/high output calls for checking the TX/RX waveform and
echo/miss logic before changing station firmware. Rebuild the source: tracked
`dist/` files may predate local changes.
