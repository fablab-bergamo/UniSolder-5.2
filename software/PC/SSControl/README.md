# SSControls chart library

Custom **WinForms/.NET Framework 4.8** chart control used by the
[UniSolder Windows application](../README.md) to plot live telemetry. The directory
is named `SSControl`, while the project, namespace, and output assembly use
**`SSControls`**.

This library renders supplied data; it does not connect to USB, decode firmware
packets, tune PID parameters, or control heaters.

## Features and source layout

`SSChart` draws configurable scales and multiple point series using
`System.Drawing`/GDI+, a bitmap back buffer, and WinForms painting. It supports
series visibility, colors/pens, optional point markers, page margins, and resize
behavior. The application decides what each trace means and converts raw packet
values into chart units.

| File | Responsibility |
| --- | --- |
| `SSChart.cs` | Chart model, scales/series, transforms, back-buffer rendering |
| `SSChart.Designer.cs` | WinForms designer-generated control setup |
| `SSControls.csproj` | Classic .NET Framework library project |

## Environment and build

Use Windows and the Visual Studio/MSBuild environment described in the PC README,
including .NET Framework 4.8 targeting support. No external NuGet packages are
declared. From `software/PC/` in Developer PowerShell:

```powershell
msbuild UniSolder.sln /t:Build /p:Configuration=Release /p:Platform="Any CPU" /p:SignManifests=false
```

For the library alone, run from this directory:

```powershell
msbuild SSControls.csproj /p:Configuration=Release /p:Platform=AnyCPU
```

Output: `bin/Release/SSControls.dll`. There is no standalone executable; the main
application is the integration/demo host.

## Using the control

| API | Purpose |
| --- | --- |
| `Page`, `Page.Margins` | Logical drawing area and margins |
| `ScaleNum`, `Scales(index)` | Allocate/access axes and configure their ranges/style |
| `DrawingsNum`, `Drawings(index)`, `Drawings1` | Allocate/access plotted series |
| `GDrawing.XScale`, `GDrawing.Yscale` | Associate a series with its axes; note existing casing |
| `GDrawing.PointsNum`, `Points` | Allocate and populate the `PointF` array |
| `GDrawing.Visible`, `Pen`, `ShowPoints` | Series presentation |
| `ScaleOnResize`, `_Redraw()` | Resize policy and immediate rendering |

See [`Form1_Load` and `LiveDataReceived` in the application](../UniSolder/Form1.cs)
for actual initialization, scaling, and rolling sample updates.

Configure array counts before indexing them, set each series' axes, and keep
axis ranges nonzero (`ValFrom != ValTo`). GDI+ line rendering needs at least two
points in a populated series. The constructor seeds demonstration points; replace
them with application data rather than treating them as measurements.

## Threading and rendering constraints

Own the chart model and rendering from the UI thread. The arrays and drawing
resources are mutable and are not synchronized for concurrent USB callbacks;
the application marshals incoming data before updating them.

`_Redraw()` renders and presents the back buffer and calls
`Application.DoEvents()`. Its `redrawactive` guard does not make the API generally
thread-safe; account for message-loop reentrancy. Resize changes discard the old
bitmap and schedule a redraw through a short WinForms timer. Rendering/resource
changes should preserve bitmap, graphics, pen, and font ownership/lifetimes.

## Verification and maintenance

There is no automated chart test suite. Build the solution and exercise live
updates, trace visibility, START/STOP behavior in the host, window resizing,
minimize/restore, and 100%/125%/150% display scaling. Check that plotted values
retain their units and that disconnect/reconnect or form closure during updates
does not access disposed controls.

For visual changes include before/after screenshots, scaling settings, and the
host build/firmware used for testing. Keep `SSChart.cs` and designer changes
consistent. USB/protocol fixes belong in the application or
[SSComm](../SSComm/README.md), not in this rendering library.
