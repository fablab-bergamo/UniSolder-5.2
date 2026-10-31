# UniSolder station firmware

Main C firmware for the **PIC32MX564F128H** front controller. It controls heating,
acquires sensor and electrical measurements, identifies instruments, drives the
OLED/menu, and exposes USB HID telemetry and tuning commands.

See the [software index](../../README.md), [hardware/wiring documentation](../../../README.md),
[bootloader](../US_BootLoader.X/README.md), and [Windows application](../../PC/README.md).

## Features and fork behavior

- Two heater channels, thermocouple/resistive profiles, sensor conversion,
  waveform correction, and PID control with power/current limits.
- AC phase synchronization and a timer-driven DC acquisition schedule.
- Automatic resistor-ID selection and a guard against accepting a different
  instrument ID before a stable open connector is observed.
- `MODEL` selection: `AUTO` uses identification; forced selections bypass it and
  display a `FORCED` badge. The profile must match the connected instrument.
- Holder, sleep, standby, calibration, and EEPROM-backed station settings.
- Live USB measurements, RAM-only parameter tuning, and bootloader entry.

This fork uses **330 ohm** for the Weller WSP80 ID connection where the original
firmware used 120 ohm. Follow the root README's instrument wiring instructions;
firmware/profile compatibility matters when swapping hardware between builds.

## Source organization and execution model

| Files | Responsibility |
| --- | --- |
| `main.c` | Startup, AC/DC timing detection, foreground loop, power-loss persistence |
| `PIC32MX564F128H.c/.h`, `mcu.h` | Registers, peripheral setup, interrupt entry points, pin mapping |
| `isr.c/.h` | Acquisition/heater phase state machine and interrupt-driven I2C |
| `PID.c/.h`, `sensorMath.c/.h` | Fixed-point control, filtering, sensor polynomial conversion |
| `iron.c/.h` | Compiled profiles, identification debounce/lock, profile switching |
| `menu.c`, `OLED.c`, font/logo headers | User interaction, framebuffer rendering, display output |
| `pars.c/.h`, `EEP.c/.h` | Parameter definitions, EEPROM transactions, target-temperature ring |
| `io.c/.h`, `usb/` | Application HID protocol and bundled Microchip USB stack |
| `firmware.ld`, `nbproject/` | Bootloader-compatible memory layout and MPLAB configurations |

Priority-7 acquisition interrupts handle measurement sequencing and call `PID()`;
I2C runs at priority 6 and encoder handlers at priority 4. The foreground services
menus, USB, identification, and cold-junction readings. Do not introduce blocking
work into acquisition. EEPROM calls block until the interrupt engine consumes
their request and require I2C interrupts to remain serviceable.

## Environment and build

The repository's documented baseline is **MPLAB X 4.20**, **XC32 2.50**, and the
legacy **PIC32 peripheral libraries** installed for that compiler. Device headers,
`GenericTypeDefs.h`, PLIB, startup objects, and linker support are toolchain
dependencies; a generic host C compiler cannot build the complete image.

1. Open this `.X` directory as a project in MPLAB X.
2. Select the installed XC32 compiler and matching device support. Regenerate
   local project/build metadata if it contains another machine's tool paths.
3. Select **`PIC32_with_bootloader`** and build a production image.

From this directory, with MPLAB make tools and compiler paths configured:

```sh
make -f nbproject/Makefile-PIC32_with_bootloader.mk SUBPROJECTS= .build-conf
```

Expected output:

```text
dist/PIC32_with_bootloader/production/US_Firmware.X.production.hex
```

| Configuration | Intended use |
| --- | --- |
| `PIC32_with_bootloader` | Application image for the existing USB bootloader |
| `PIC32_Standalone` | Standalone programming workflow; not accepted by the PC updater |
| `PIC32_NoOptimization` | Diagnostic build configuration; inspect its settings before use |

`build.bat` attempts all three configurations. Use the explicit single-configuration
command when preparing a USB update. Generated makefiles rely on MPLAB metadata;
edit project settings/source rather than patching generated paths into source code.

The manually triggered [firmware workflow](../../../.github/workflows/build-firmware.yaml)
builds only `PIC32_with_bootloader` on Linux and publishes an artifact of that name.
It does not establish that the other configurations build in that environment.

## Loading and memory compatibility

Upload the bootloader-compatible HEX with the Windows application's **Update
Firmware** command. A bootloader must already be installed. The application entry
is at physical `0x1D003000`; the linker also places application exception vectors
in the separate boot-flash region at `0x9FC01000`. Preserve the bootloader/linker
agreement and the final flash CRC-word reservation when changing either project.

See the PC README for validation, update/recovery behavior, and current updater
limitations. Do not use a standalone image as a replacement USB-update artifact.

## Formatting and arithmetic tests

Run these commands from the **repository root**:

```sh
python -m pip install -r tools/requirements-format.txt
python -m pre_commit install
python tools/format_firmware.py --check
python tools/format_firmware.py --fix
```

The hook checks formatting without rewriting/staging files. Vendor USB, bitmap
headers, and historical `isr_old.c` are excluded; `isr_old.c` is not in the active
project source list.

For arithmetic regression tests in Linux/WSL with Python 3 and GCC:

```sh
python3 tools/test_pid.py --sanitize --opt 0
python3 tools/test_pid.py --sanitize --opt 2
```

With `gcc-mipsel-linux-gnu`, `libc6-dev-mipsel-cross`, and `qemu-user` installed:

```sh
python3 tools/test_pid.py --cc mipsel-linux-gnu-gcc --runner qemu-mipsel
```

Tests compile production `PID.c`, substitute acquisition/sensor inputs, and check
integral bounds, rounding, waveform behavior, compensation, normalization, and
fault pulses. A nominal replay matches the pre-fix MIPS baseline. The formatting
and PID workflows run on relevant pushes/pull requests. QEMU arithmetic results
do not establish XC32 image size, peripheral behavior, or real ISR execution time.

## Maintenance and troubleshooting

- Add profiles in `iron.c` and update `NB_IRONS` in `iron.h`; the compile-time
  count check must pass. Instrument names are fixed-width 24-character fields.
- Preserve units: targets use 2 C steps; measured/ambient temperatures generally
  use half-degrees Celsius. Respect the documented PID bounds and rounding stages.
- Keep parameter layout, `ParDef`, and EEPROM compatibility aligned. Existing
  settings are not a versioned serialization format.
- Missing `plib.h`, peripheral headers, or `GenericTypeDefs.h` usually means the
  legacy toolchain/include setup differs from the recorded project.
- Link/vector overlap or bootloader rejection calls for checking the selected
  configuration and linker map, not renaming the HEX file.
- For behavioral changes, build the affected configuration and verify identification,
  heater/sensor fault responses, AC/DC timing as applicable, calibration, and
  power-loss persistence on appropriate hardware. Keep arithmetic and tuning
  changes separately reviewable and record actual hardware checks performed.
