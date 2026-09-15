# OpenRGB plugin

Adds the Lenovo OEM GPU to OpenRGB as two devices. See the
[root README](../README.md) for what it does and how to install a prebuilt DLL.
This file covers building it.

## Target — read first

| | |
|---|---|
| OpenRGB | **1.0rc3 / 1.0rc3.1** |
| Plugin API | **4** |
| Qt | **5.15.0** |
| Compiler | **MSVC** |

All four must match the OpenRGB binary you are loading into. A mismatch means
the DLL is **ignored silently** — no error, no log entry.

Post-rc3 master is plugin API **5**: the interface IID changed from
`com.OpenRGBPluginInterface` to `org.openrgb.OpenRGBPluginInterface`, plugins
moved to virtual controllers with callback function pointers, and
`RGBController` members became protected behind accessors. Porting is required,
not optional.

To check your OpenRGB's plugin API version, look at `OpenRGBPluginInterface.h`
in its source tree. To check its Qt version, read the file version of
`Qt5Core.dll` in its install folder.

**MSVC only.** Qt plugins are ABI-coupled to the host application and official
OpenRGB releases are MSVC builds. A MinGW-built DLL will not load into them —
this was learned the slow way.

## Build

From an **"x64 Native Tools Command Prompt for VS"** (not plain `cmd`, not
PowerShell, not MSYS2 — the VS prompt is what puts `nmake` and `cl` on PATH):

```
git clone https://gitlab.com/CalcProgrammer1/OpenRGB.git C:\src\OpenRGB
cd C:\src\OpenRGB
git checkout release_candidate_1.0rc3

cd path\to\legion-gpu-rgb\plugin
build_plugin.bat C:\Qt\5.15.0\msvc2019_64 C:\src\OpenRGB
```

Or by hand:

```
qmake OPENRGB_PATH=C:/src/OpenRGB OpenRGBLenovoGPUPlugin.pro
nmake
```

Then copy `release\OpenRGBLenovoGPUPlugin.dll` to `%APPDATA%\OpenRGB\plugins\`
and restart OpenRGB **as administrator** (I2C access needs it).

## Build notes

**Three include paths are required, not one.** OpenRGB's
`ResourceManagerInterface.h` includes `i2c_smbus.h`, so `i2c_smbus/` has to be on
the include path even though this plugin never uses OpenRGB's I2C layer. The
complete set is the OpenRGB root, `RGBController/`, and `i2c_smbus/`.

**`RGBController.cpp` must be compiled in.** It implements the base class this
plugin subclasses — the non-pure virtuals plus the mode/zone/led constructors.
Without it the link fails with ~44 unresolved externals. It is self-contained
(only `<cstring>` and `RGBController.h`), so nothing else is dragged in. This is
also why the resulting DLL is a derivative work of OpenRGB and must be GPL —
see [LICENSE](../LICENSE).

**No NVIDIA SDK needed.** `nvapi64.dll` is loaded at runtime with
`LoadLibraryA`, so there is no import library to link.

## Source layout

| File | Role |
|------|------|
| `LenovoGPUNvAPI.{h,cpp}` | NvAPI layer — loads `nvapi64.dll`, resolves functions by id, enumerates GPUs, writes registers |
| `LenovoGPUController.{h,cpp}` | The protocol: register map, write sequences, and the coalescing worker thread |
| `RGBController_LenovoGPU.{h,cpp}` | Strip device — colour + brightness |
| `RGBController_LenovoGPULogo.{h,cpp}` | Logo device — Off/On modes |
| `LenovoGPUPlugin.{h,cpp}` | Plugin entry point (API 4) |

Both RGB devices share **one** `LenovoGPUController`, which owns the logo state.
This matters: a colour burst also carries register `0x50`, so without shared
state a colour change would clobber the logo.

## Threading

`SetColor` and `SetLogo` do not touch the bus. They record the wanted state and
wake a worker thread, which applies the newest state and drops whatever was
superseded while it was busy.

This is not premature optimisation. A colour burst takes ~555 ms; the OpenRGB
SDK — and SignalRGB through the bridge — will call `UpdateLEDs` up to 60 times a
second. Applying synchronously would block OpenRGB's UI thread and build an
unbounded backlog of stale colours.
