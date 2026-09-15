# legion-gpu-rgb

RGB lighting control for **Lenovo OEM NVIDIA graphics cards**, with no Lenovo
software and no Lenovo motherboard.

If you pulled a GPU out of a Legion prebuilt and put it in your own build, its
lighting almost certainly stopped working. Legion Space only runs on Lenovo
systems, and OpenRGB cannot see the controller either. This project makes the
card light up anywhere.

Developed against an **RTX 5070 Ti, `10DE:2C05` / subsystem `17AA:C773`**, from a
Legion Tower 5 Gen 10 (30L).

---

## Why OpenRGB could not see it

The lighting MCU is **not** on the GPU's normal I2C bus. It sits on the **DDC
side** of it — the side normally used to talk to monitors.

OpenRGB's `i2c_smbus_nvapi` hardcodes `is_ddc_port = 0` in both of its transfer
functions, so no device on the DDC side is reachable through it, no matter how
hard you scan. (OpenRGB does report a device at `0x5B` on these cards. It is
unrelated — a red herring that costs people a lot of time.)

The controller answers at address **`0x49`** on **port 1 with `bIsDDCPort = 1`**.
That one flag is the whole reason this looked impossible.

Full details: **[docs/protocol.md](docs/protocol.md)**

---

## What's here

| | |
|---|---|
| **[tool/](tool/)** | `legion_gpu_rgb.py` — standalone control. Needs only Python and the NVIDIA driver. Start here. |
| **[plugin/](plugin/)** | OpenRGB plugin (C++). Adds the card as a real OpenRGB device. |
| **[openrgb-patch/](openrgb-patch/)** | Patch making the DDC flag selectable in OpenRGB core — the proper upstream fix. |
| **[docs/protocol.md](docs/protocol.md)** | Complete register map, write sequences, and how each value was confirmed. |
| **[docs/signalrgb.md](docs/signalrgb.md)** | Using it from SignalRGB (via a bridge — no native plugin is possible). |

---

## Quick start

No build required. Python 3 and an NVIDIA driver are the only prerequisites.

```
py -3 tool/legion_gpu_rgb.py --info                    # identify the card
py -3 tool/legion_gpu_rgb.py --color FF0000            # red
py -3 tool/legion_gpu_rgb.py --color 00FF00 --brightness 40
py -3 tool/legion_gpu_rgb.py --logo off                # NVIDIA logo off
py -3 tool/legion_gpu_rgb.py --off                     # everything dark
```

**Close Legion Space first** if it is installed — it reasserts its own profile
and will fight you for the bus.

To restore your lighting automatically at logon, run `tool/install_startup.bat`
as administrator. It registers a scheduled task pointing at wherever the script
currently lives, so put the folder somewhere permanent first.

---

## Hardware compatibility

| Card | PCI ID | Subsystem | Status |
|------|--------|-----------|--------|
| RTX 5070 Ti (Lenovo OEM) | `10DE:2C05` | `17AA:C773` | ✅ Confirmed working |

Other Lenovo OEM cards are **untested**. The transport is generic NvAPI, so
there is a fair chance the protocol is shared across the Legion GPU line — but
nobody has checked.

**If you have a different Lenovo OEM card, please open an issue** with the output
of `--info` and `--probe`. There's a template for it. Adding a confirmed card to
this table is the single most useful contribution here.

⚠️ Do not run this against a non-Lenovo card. Writing to an unknown I2C device is
a good way to find out what else lives on that bus.

---

## OpenRGB plugin

Gives you the card in OpenRGB proper, as **two devices**:

| Device | Control |
|--------|---------|
| `Lenovo Legion GPU (…) Strip` | RGB colour + brightness, `Static` mode |
| `Lenovo Legion GPU (…) Logo` | Mode dropdown: **Off / On** — no colour |

The logo is a *switch*, not a light — register `0x50` takes only `0x00`/`0x01`.
Modelling it as a colour zone would make "set it to black" the only way to turn
it off, which nobody would guess. As its own device, the mode dropdown *is* the
toggle.

Brightness is **global in hardware** — it dims the logo too. Confirmed by
sweeping it. There is no per-zone brightness to expose.

### Installing

Grab `OpenRGBLenovoGPUPlugin.dll` from
[Releases](../../releases), drop it in `%APPDATA%\OpenRGB\plugins\`, and restart
OpenRGB **as administrator**.

> **Match the release to your OpenRGB build.** Qt plugins are ABI-coupled to the
> host application. A mismatched Qt version or plugin API version means OpenRGB
> **ignores the DLL silently — no error, no log line, nothing**. Each release is
> tagged with the exact OpenRGB and Qt version it was built for. If the device
> doesn't appear, this is why.
>
> Current target: **OpenRGB 1.0rc3 / 1.0rc3.1, plugin API 4, Qt 5.15.0, MSVC.**
> Post-rc3 master is plugin API 5 and needs porting.

### Building

**MSVC only** — official OpenRGB builds are MSVC, and a MinGW-built DLL will not
load into them. From an *"x64 Native Tools Command Prompt for VS"*:

```
git clone https://gitlab.com/CalcProgrammer1/OpenRGB.git C:\src\OpenRGB
cd C:\src\OpenRGB && git checkout release_candidate_1.0rc3

cd path\to\legion-gpu-rgb\plugin
build_plugin.bat C:\Qt\5.15.0\msvc2019_64 C:\src\OpenRGB
```

Check which Qt your OpenRGB uses by looking for `Qt5Core.dll` in its install
folder and reading its file version.

---

## Known limits

**One colour change takes ~555 ms.** Lenovo paces register writes ~15 ms apart
and a full update is 37 writes. There is no Direct mode and there cannot be a
convincing one — anything that streams frames will look like a slideshow. The
plugin applies writes on a worker thread that keeps only the newest state and
discards what was superseded, so nothing backs up, but the hardware ceiling
stands.

**Only effect `0x01` (static) is implemented.** It is the only effect id ever
observed on the wire. Undefined ids **do not fail safely** — they leave the
controller in a runaway high-speed colour cycle that persists until a valid
frame is sent. Lenovo's animated effects need a proper capture before anyone
should send them.

**Brightness is global**, as above.

---

## Contributing

The most valuable contributions, in order:

1. **Confirmed hardware reports** for other Lenovo OEM cards — see the issue template.
2. **Captures of Lenovo's animated effects**, so the effect ids can be decoded
   safely. [docs/protocol.md](docs/protocol.md) describes the method that worked.
3. **Porting the plugin to OpenRGB plugin API 5** (post-1.0rc3 master).

See [CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

**GPL-2.0-or-later.** The plugin compiles OpenRGB's `RGBController.cpp` into the
DLL, making the binary a derivative work of OpenRGB, which is GPL-2.0-or-later.
The whole repository is licensed to match. See [LICENSE](LICENSE).

Not affiliated with, endorsed by, or supported by Lenovo or NVIDIA. Lenovo and
Legion are trademarks of Lenovo; NVIDIA and GeForce RTX are trademarks of NVIDIA
Corporation. Use at your own risk.
