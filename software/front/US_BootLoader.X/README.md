# UniSolder PIC32 USB bootloader

Recovery/startup firmware for the **PIC32MX564F128H**. It initializes the board
with heater drive off, checks the application's stored CRC, and either calls the
application reset entry or runs a USB HID programming loop with an OLED indicator.

Related components: [main firmware](../US_Firmware.X/README.md),
[Windows updater](../../PC/README.md), [software index](../../README.md).

## Features and startup selection

- Device information, operating-mode, erase, program, CRC-read, completion, and
  reset/application-start commands over HID VID `0x04D8`, PID `0x003C`.
- CRC-based application validity check before automatic startup.
- Application-requested recovery using the `NVMDATA` magic `0x6193471A` across
  a soft reset; main clears the request before running recovery services.
- OLED controller/charge-pump detection and a moving recovery indicator.

An invalid application stays in recovery. A valid application normally starts
immediately. For button entry, hold B2 at reset and release it before the
core-timer threshold (80,000,000 counts, approximately two seconds at the configured
40 MHz core timer). Holding B2 through the threshold still launches a valid
application; this is not a permanent hold-to-stay rule.

## Source layout

| Files | Responsibility |
| --- | --- |
| `main.c` | Startup decision, recovery polling loop, display animation |
| `io.c/.h` | HID command dispatch, payload alignment, protocol definitions |
| `PIC32MX564F128H.c/.h`, `mcu.h` | GPIO/startup, NVM operations, flash bounds, application jump |
| `crc.c/.h` | CRC-16 update routine |
| `OLED.c/.h` | Recovery display initialization and framebuffer transfer |
| `usb/` | Bundled Microchip USB stack and descriptors |
| `bootloader.ld`, `nbproject/` | Linker placement and MPLAB configuration |

## Environment and build

Use the repository's PIC32 toolchain setup: MPLAB X, **XC32 2.50**, and legacy
PIC32 peripheral libraries. The root firmware instructions record MPLAB X 4.20;
this project's metadata also records `PIC32MX_DFP` 1.3.231 and a PICkit3 tool.
Select installed compatible device/programmer support in the IDE and regenerate
machine-local build paths as needed. Toolchain migrations require validation.

Open `US_BootLoader.X`, select **`PIC32`**, and build a production image. From
this directory, once the MPLAB tools/paths are configured:

```sh
make -f nbproject/Makefile-PIC32.mk SUBPROJECTS= .build-conf
```

Output: `dist/PIC32/production/US_BootLoader.X.production.hex`.

Initial installation uses a compatible PIC programmer through MPLAB X/IPE and
the board's programming connections. The Windows application updates application
firmware through an installed bootloader; it does not install this image. There
is no dedicated bootloader build workflow in the repository.

## Application and protocol contract

| Item | Current definition |
| --- | --- |
| Application flash/reset start | `0x9D003000` cached address; physical `0x1D003000` |
| Application exception-vector region | `0x9FC01000` through `0x9FC01FFF` |
| Application flash end | Derived from `BMXPFMSZ` for the target device |
| Validity storage | Final four-byte word of application flash |
| Flash page size | 4096 bytes |
| Flash-operation key | `0x43211234`; accidental-operation guard, not authentication |

The CRC routine uses polynomial `0x1021`, MSB-first processing, and no final XOR.
Application validation/completion starts with seed **1234** and covers application
program flash excluding the final validity word. It does not cover the separate
application-vector region or authenticate an image.

The host sends 64-byte HID reports. Flash records contain a physical address,
length, and payload; the PC validator limits data to 51 bytes per report. Keep
the bootloader, application linker script, and host record format consistent.

## Known implementation limits

- `mcuWriteFlashRecord()` checks permitted ranges in the word path, but its
  aligned 512-byte row path bypasses those guards. Host-side record validation
  remains important; the command key is not a memory-protection boundary.
- The erase function can overwrite an earlier application-region error with a
  later vector-region result. A zero status does not prove all pages erased.
- `mcuProgramComplete()` returns the computed CRC and ignores the validity-word
  write status. The CRC-read command also accepts host-supplied address/length.
- The PC updater currently logs nonzero erase/program statuses without acting on
  them; read its README before interpreting an update success message.

## Verification and maintenance

There is no automated bootloader test suite; main-firmware PID tests do not
exercise this project. Verify boot selection with valid/invalid application CRC,
the B2 release sequence, and application-requested recovery. For programming
changes, verify interrupted updates remain recoverable and cannot overwrite
bootloader/configuration regions; check actual return statuses and flash contents.

Keep configuration words, linker placement, reset entry, and CRC reservation
aligned with the main firmware. Preserve bundled USB source notices. This project
is outside the current main-firmware formatting hook; avoid unrelated formatting
changes when reviewing NVM/protocol behavior. Existing `dist/` contents are not
evidence of a fresh build from a changed source tree.
