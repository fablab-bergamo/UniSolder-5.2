# UniSolder software

The station application, recovery bootloader, holder sensor, and Windows tools
are separate build targets. Start with the component README rather than building
or flashing every project together.

| Component | Purpose | Build environment |
| --- | --- | --- |
| [Main firmware](front/US_Firmware.X/README.md) | PIC32 station control, instrument profiles, display/menu, USB telemetry | MPLAB X, XC32 2.50, legacy PIC32 peripheral libraries |
| [Bootloader](front/US_BootLoader.X/README.md) | PIC32 startup validation and USB application programming | MPLAB X, XC32 2.50, legacy peripheral libraries |
| [Holder sensor](sensor/US_sensor.X/README.md) | PIC12F615 challenge/echo sensor and sleep output | MPLAB X, legacy XC8 project (1.30 recorded) |
| [Windows application](PC/README.md) | Monitoring, tuning, and firmware upload | Windows, Visual Studio/MSBuild, .NET Framework 4.8 |
| [SSComm](PC/SSComm/README.md) | Windows USB transports and logging | Built with the Windows solution |
| [SSControls](PC/SSControl/README.md) | WinForms chart library | Built with the Windows solution |

## How the components fit together

The bootloader starts a CRC-valid station application or services USB recovery
commands. The main firmware owns heater control, sampling, identification, and
user interaction. Both expose USB HID interfaces used by the Windows application,
whose protocol layer sits above SSComm. SSControls only renders charts.

The PIC12 holder sensor runs independently; it is not a USB peripheral and is
not updated through the Windows application. `resID.xlsx` is reference data for
instrument identification; runtime profiles are compiled from the firmware's
`iron.c`.

## Build and validation boundaries

- **USB application updates:** use `PIC32_with_bootloader` output. Initial
  bootloader and sensor programming require their respective programmer workflows.
- **Hardware files and wiring:** see the [repository README](../README.md).
- **Firmware formatting:** `python tools/format_firmware.py --check`, from the
  repository root, currently covers maintained top-level main-firmware C files.
- **PID arithmetic:** `python3 tools/test_pid.py --sanitize` in Linux/WSL checks
  production PID code with synthetic inputs; optional MIPS/QEMU checks are
  described in the main-firmware README. These do not validate peripherals or
  physical control-loop timing.

The two `usb/` trees are bundled Microchip stack copies. Keep their original
source/license notices and review changes to both consumers; they are outside
the main-firmware formatting scope. Generated `build/`, `dist/`, `bin/`, and
`obj/` files are not substitutes for rebuilding the matching source/configuration.
