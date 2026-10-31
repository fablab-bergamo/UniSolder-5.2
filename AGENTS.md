# Repository Guidelines

## Project Structure & Module Organization

- `software/front/US_Firmware.X/`: main PIC32 firmware; control logic includes `PID.c`, `iron.c`, `sensorMath.c`, and `menu.c`.
- `software/front/US_BootLoader.X/` and `software/sensor/US_sensor.X/`: separate MPLAB X bootloader and sensor projects.
- `software/PC/UniSolder.sln`: Windows solution containing the UniSolder WinForms application, `SSComm` USB communications, and `SSControl` chart controls.
- `schematics/`, `gerber/`, `BOM/`, and `3D/`: hardware designs, manufacturing outputs, component spreadsheets, and mechanical assets. `img/` contains documentation images.
- `.github/workflows/`: manually triggered firmware and Windows builds.

## Build, Test, and Development Commands

Install Visual Studio 2022 with the .NET desktop development workload and .NET Framework 4.8 targeting support. From `software/PC/`, run:

```powershell
msbuild UniSolder.sln /p:Configuration=Release /p:Platform="Any CPU" /p:SignManifests=false
```

Run the UniSolder project from Visual Studio to debug the application.

Firmware uses MPLAB X 4.20, XC32 v2.50, and the PIC32 legacy peripheral libraries. Open the project in MPLAB X to generate local build metadata. From `software/front/US_Firmware.X/`, with MPLAB make tools available, run:

```powershell
make -f nbproject/Makefile-PIC32_with_bootloader.mk SUBPROJECTS= .build-conf
```

Use `PIC32_with_bootloader` for updates through the PC application. The firmware CI currently builds only this configuration on Linux. Both GitHub Actions workflows support manual execution and artifact downloads.

## Coding Style & Naming Conventions

Match surrounding code: generally four-space indentation, C# braces on separate lines, and firmware C braces on the declaration line. Preserve existing identifiers such as `PIDTasks`, `t_IronConfig`, and uppercase constants. Keep C declarations in the corresponding headers. No repository-wide formatter or linter configuration is present; avoid unrelated reformatting.

## Testing Guidelines

No automated test framework or coverage requirement is configured. Build each affected project and document manual verification. For PC changes, check USB connection/disconnection, bootloader handling, chart updates, and display scaling as relevant. For firmware changes, verify instrument identification and heater shutdown behavior on appropriate hardware. Include reproduction steps and `%LOCALAPPDATA%\UniSolder\UniSolder.log` for PC failures.

## Commit & Pull Request Guidelines

Recent commits use concise imperative subjects with prefixes such as `PC:`, `Firmware:`, `CI:`, and `README:`. Follow that pattern. Describe behavior changes, link relevant issues, and record build configurations and hardware checks performed. Include screenshots for UI changes. Keep generated build outputs out of routine source changes, and update `README.md` when fork behavior or compatibility changes.
