# UniSolder Windows application

Windows desktop companion for the UniSolder 5.2 soldering controller: monitor
live measurements, adjust controller parameters, and upload application firmware
through the existing USB bootloader.

This is the FabLab Bergamo fork. See the [repository README](../../README.md)
for the upstream project, hardware documentation, firmware differences, and
license notice.

## Features

- Automatic USB HID connection and reconnection, with device state in the status bar.
- Live chart with selectable target/measured/filtered temperatures, ADC values,
  PID temperature, duty, heater resistance, heater state, and waveform information.
- Live adjustment of gain, Kp, Ki, derivative gain, and overshoot gain. Controls
  stay disabled when parameters cannot be read from the device.
- **Query device** command to identify a connected station through its feedback.
- Intel HEX loading and validation before a firmware update, including checksums,
  report-size limits, and the expected bootloader-compatible application layout.
- File logging of connections, commands/replies, tuning changes, and update steps.
  Double-click the status bar to open the log.
- DPI-aware WinForms layout configured for per-monitor scaling.

## Environment and prerequisites

Use a Windows 10/11 development machine with:

1. **Visual Studio 2022** with the **.NET desktop development** workload, or
   Visual Studio Build Tools with the equivalent desktop/MSBuild components.
2. **.NET Framework 4.8 targeting pack and SDK**. The runtime alone does not
   provide the reference assemblies needed to compile; install the developer
   pack or select the components in Visual Studio Installer. See
   [Microsoft's developer-pack instructions](https://learn.microsoft.com/en-us/dotnet/framework/install/guide-for-developers).
3. Git, if cloning and contributing to this repository.

All three projects target **.NET Framework 4.8**, with `Debug`/`Release` and
`Any CPU` solution configurations. They use the classic MSBuild project format.
Use Visual Studio's MSBuild for these instructions. The PC build does not need
MPLAB X, XC32, Python, Node.js, or a connected station.

To run the built application, install the .NET Framework 4.8 runtime or a
compatible later 4.x runtime. The Windows HID implementation is platform-specific;
Linux/WSL is not the application build/run environment documented here.

## Source layout and dependencies

| Path | Purpose |
| --- | --- |
| `UniSolder.sln` | Main solution; build this to include all dependencies. |
| `UniSolder/` | WinForms executable, UI, device protocol, and HEX validation. |
| `UniSolder/Form1.cs` | Connection/UI behavior, live plotting, and update orchestration. |
| `UniSolder/UniSolderComm.cs` | Application and bootloader command encoding/decoding. |
| `UniSolder/HexFileManager.cs` | HEX parsing, record assembly, and layout checks. |
| `SSComm/` | USB transport, Windows HID interop, and shared logging. |
| `SSControl/` | Custom chart controls; builds the `SSControls` assembly. |

`SSComm.csproj` references the checked-in `SSComm/LibUsbDotNet.dll`; another copy
exists at this folder's root. There is no NuGet package manifest or separate
package-restore step for the current solution. The main form selects the native
Windows HID transport; the generic LibUsbDotNet transport is also compiled into
`SSComm`.

## Build and run

### Visual Studio

1. Open `software/PC/UniSolder.sln` from the repository checkout.
2. Set **UniSolder** as the startup project.
3. Select **Debug** or **Release**, platform **Any CPU**.
4. Choose **Build > Build Solution**.
5. Press **F5** to debug, or **Ctrl+F5** to run without the debugger.

The solution header mentions Visual Studio 14, but the repository's documented
development environment is Visual Studio 2022; installing the original IDE is
not required.

### Command line

Open **Developer PowerShell for Visual Studio**, then run from the repository root:

```powershell
Set-Location software/PC
msbuild UniSolder.sln /t:Build /p:Configuration=Release /p:Platform="Any CPU" /p:SignManifests=false
.\UniSolder\bin\Release\UniSolder.exe
```

For a clean rebuild, replace `/t:Build` with `/t:Rebuild`. For debugging, use
`/p:Configuration=Debug`; the executable will be under `UniSolder/bin/Debug/`.

Release output is `UniSolder/bin/Release/`. Keep the executable, its generated
`.config`, and dependency DLLs together when copying the build to another PC.
Do not distribute just `UniSolder.exe`.

ClickOnce manifest signing is disabled in the project and explicitly disabled
above. The historical `UniSolder_TemporaryKey.pfx` is not required to sign a
normal build. Legacy bootstrapper metadata still mentions .NET Framework 4.5.2;
the actual executable targets 4.8. Check prerequisites before creating a
ClickOnce installer, rather than relying on that old bootstrapper selection.

### GitHub Actions

Run the **Windows Unisolder application** workflow manually from the repository's
Actions page. Download and extract the **Unisolder-WindowsApp** artifact, which
contains `UniSolder/bin/Release/` build output. See the
[Windows build workflow](../../.github/workflows/build-windows.yaml) for the
current automation; it is triggered by `workflow_dispatch`, not automatically
for every pull request.

## Connect and use

Connect a UniSolder station over USB and launch the application. It searches for
HID VID `0x04D8`, PID `0x003C` and retries disconnected-device detection every
second. This is a HID connection, not a serial COM-port connection.

Use the chart checkboxes to select traces. **STOP/START** pauses/resumes chart
updates; it does not stop station heating. Slider changes are sent to the
connected device immediately; there is no separate Apply button. In this fork's
firmware, those tuning changes update the current profile in RAM and are not
saved as permanent instrument-profile edits.

**Connected - no answer (bootloader?)** means the HID device opened but PID
parameters could not be read. Bootloader mode is one possible cause, not a
confirmed diagnosis. Parameter sliders remain disabled in that state.

## Update station firmware

1. Obtain a HEX file built with **`PIC32_with_bootloader`**, using the
   [firmware build instructions](../../README.md#building-instructions).
2. Connect the station, choose **Update Firmware**, and select the HEX file.
3. Review the selected file details and confirm. The application requests
   bootloader mode, reconnects, erases the application, programs records, and
   sends the completion/restart commands. Keep the connection and power stable.
4. If the update reports a failure, inspect the log and retry. After an interrupted
   erase/program operation, the device may remain in bootloader mode; power-cycle
   it if reconnection is needed.

Do not select a `PIC32_Standalone` image or the bootloader's own HEX image. This
workflow updates the application through an already installed bootloader; it
does not install the initial bootloader.

Current limitations: erase/program status bytes are logged but nonzero values
are not acted on. The updater does not perform a host-side readback/CRC comparison;
the bootloader computes its application CRC during completion. Transport failures
and exceptions before that completion step stop the update, but a success dialog
should not be treated as an independent verification of every flash write.

## Logs and troubleshooting

The application appends to:

```text
%LOCALAPPDATA%\UniSolder\UniSolder.log
```

At startup, a log larger than 5 MiB is rotated to `UniSolder.old.log`. Logging
also writes to debugger output. Attach the relevant log when reporting a fault.

| Symptom | Check |
| --- | --- |
| `msbuild` is not recognized | Use Developer PowerShell or run the installed Visual Studio MSBuild by its full path. |
| `MSB3644` / missing .NET 4.8 reference assemblies | Install the **4.8 developer/targeting pack**, not only the runtime. |
| Missing `LibUsbDotNet` reference | Restore the checked-in `SSComm/LibUsbDotNet.dll` and build the complete solution. |
| Certificate/signing error | Keep `/p:SignManifests=false`; check for local publish settings overriding it. |
| Not connected | Check station power, a USB data cable, and Windows device enumeration; close other tools using the device. |
| Sliders disabled | Check connection, application/bootloader mode, and command failures in the log. |
| Clipped controls or scaling problems | Report display scaling, monitor setup, and build version; retain `UniSolder.exe.config` alongside the executable. |
| HEX rejected | Read the reported reason and use the `PIC32_with_bootloader` artifact. |

## Development and verification

There is no automated PC-application test project in this solution. Before
submitting a PC change, build Release and verify the affected behavior:

- Launch without hardware; connect, disconnect, and reconnect a station.
- Check live traces, START/STOP, parameter reads, and intended slider writes.
- Check application versus bootloader mode and disabled controls on failed reads.
- Check layout at 100%, 125%, and 150% display scaling where available.
- For updater changes, check invalid HEX rejection and update/recovery on test hardware.
- Confirm expected messages appear in the file log and status bar.

Keep UI changes in `Form1.cs` and the matching WinForms designer files consistent.
Describe reproduction steps, firmware build/configuration, Windows/runtime
environment, and manual verification in a pull request; include screenshots for
layout changes. Generated `bin/`, `obj/`, and publish output should stay out of
source commits. Repository firmware arithmetic tests do not test this PC app.
