# SSComm communication library

Windows/.NET Framework **4.8** class library used by the
[UniSolder desktop application](../README.md). It provides buffered USB transports,
device enumeration/interop, and shared logging. It does not implement UniSolder's
application/bootloader commands; those live in
[`UniSolderComm.cs`](../UniSolder/UniSolderComm.cs).

## Components and interfaces

| File/directory | Responsibility |
| --- | --- |
| `common.cs` | `IUniComm` connection, buffering, notification, and disposal interface |
| `usb_hid.cs` | `USBHID`: Windows HID discovery, handles, buffered reads/writes |
| `usb_hid/` | HID, SetupAPI/device-manager, and file-I/O interop declarations/helpers |
| `usb_generic.cs` | Alternative `USBGen_LibUSB` implementation using LibUsbDotNet |
| `Log.cs` | Shared file/debug logging and `MessageLogged` event |
| `LibUsbDotNet.dll` | Checked-in assembly referenced by `SSComm.csproj` |

The main application creates `USBHID` with VID `0x04D8` and PID `0x003C`.
The generic transport is compiled but is not the transport selected by `Form1`.

`IUniComm` exposes `Connect()`, `Disconnect()`, `Connected`, `Read`/`ReadByte`,
`Write`/`WriteByte`, buffer counts, byte counters, and `DataReceived`. Implementers
also provide `Init()` and `Dispose()`.

## Environment and build

Use Windows, Visual Studio/MSBuild, and the .NET Framework 4.8 targeting pack as
described in the PC README. There is no standalone executable or service.
From `software/PC/` in Developer PowerShell:

```powershell
msbuild UniSolder.sln /t:Build /p:Configuration=Release /p:Platform="Any CPU" /p:SignManifests=false
```

For an isolated library build from this directory:

```powershell
msbuild SSComm.csproj /p:Configuration=Release /p:Platform=AnyCPU
```

Output: `bin/Release/SSComm.dll`. `LibUsbDotNet.dll` is referenced through the
project-local hint path; no NuGet restore is configured. Use the main solution's
application output when distributing the PC tool so all runtime dependencies
are copied together.

## Buffering, threading, and ownership

- Subscribe to notifications before connecting, inspect connection/buffer state,
  and unsubscribe/dispose during shutdown. `Dispose()` disconnects the transport.
- Notifications can arrive off the UI thread. Marshal WinForms updates using
  the owning control's invocation mechanism; callbacks must not directly redraw
  controls or block waiting on the UI thread.
- Writes enqueue bytes, not completed device commands. UniSolder's protocol layer
  supplies complete reports and waits for replies/timeouts. Do not interpret a
  successful enqueue as confirmation that the device executed a command.
- The HID worker transmits complete report payloads and supplies the report-ID
  byte. In the station protocol the application payload is 64 bytes. Partial
  payloads can remain queued until enough data exists to form a report.
- `ReadByte()` on an empty FIFO and oversized writes can throw. Keep buffer/offset
  arguments valid and serialize command producers/consumers; the public API is
  not a promise of unrestricted concurrent access.

`Log.Open()` selects a file; `Info`, `Warn`, and `Error` write messages.
`MessageLogged` runs on the logging caller's thread and may therefore require UI
marshaling too. The main application chooses the log path and opens/closes it.

## Verification and maintenance

No automated SSComm tests are configured. Build the full solution after interface
or interop changes, then check connection failure, unplug/replug during a command,
timeouts, reply delivery, telemetry, and shutdown while I/O is active. Attach
the application's log to reports. Keep the generic transport's limitations
separate from behavior actually verified through the application's HID path.

Preserve native declaration widths, handle lifetime rules, and report framing
when changing architecture/runtime. Protocol parsing belongs above the transport;
chart/UI behavior belongs in the application or [SSControls](../SSControl/README.md).
