# Protocol

Complete wire protocol for the lighting controller on the Lenovo OEM RTX 5070 Ti
(`10DE:2C05` / `17AA:C773`).

Everything here was recovered by hooking Lenovo's own hardware service
(`SmartEngineHost64.exe`) with Frida and capturing real traffic. Nothing in this
document is a guess — see [How this was found](#how-this-was-found) for why that
distinction matters more than it sounds.

---

## Transport

The controller is reached through **NVIDIA's NvAPI I2C interface**. Nothing
Lenovo-specific is involved at the transport layer: NvAPI talks to the GPU, the
GPU talks to the MCU. That is why the card works in any motherboard.

| Property | Value |
|----------|-------|
| Function | `NvAPI_I2CWrite`, id `0xE812EB07` |
| Struct | `NV_I2C_INFO_V3` — size `0x40`, version `0x00030040` |
| `i2cDevAddress` | `0x92` (8-bit) = **`0x49`** 7-bit |
| `portId` | `1`, with `bIsPortIdSet = 1` |
| **`bIsDDCPort`** | **`1`** ← the whole trick |
| `regAddrSize` | `1` |
| `cbSize` | `1` |
| `i2cSpeedKhz` | `4` |
| `displayMask` | `0x00000100` as captured — **irrelevant**, see below |

Every transaction is one register byte plus one data byte.

`nvapi64.dll` exports only `nvapi_QueryInterface`. Every other function is
resolved by numeric id at runtime, so no NVIDIA SDK is needed to build against
it.

### displayMask does not matter

Once `bIsPortIdSet` and `portId` are set, `displayMask` is ignored. Verified by
issuing writes with deliberately wrong masks and watching them apply normally.
Any tool that tries to enumerate the correct display mask first is solving a
problem that does not exist.

### Why OpenRGB cannot reach it

`i2c_smbus_nvapi::i2c_smbus_xfer` and `i2c_smbus_nvapi::i2c_xfer` both contain:

```c
i2c_data.is_ddc_port = 0;
```

hardcoded. No device on the DDC side is reachable through OpenRGB's I2C layer,
so no scan will ever find this controller. OpenRGB *does* report a device at
`0x5B` on these cards — it is unrelated, and chasing it is a dead end.

[`../openrgb-patch/openrgb-nvapi-ddc.patch`](../openrgb-patch/) makes the flag
selectable and registers a second bus per GPU with it set.

---

## Register map

| Register | Value | Meaning | Confidence |
|----------|-------|---------|------------|
| `0x30` | `0xB0` | Begin frame | Confirmed |
| `0x31` | `0xB1` | Commit | Confirmed |
| `0x32` | `0xB2` | Apply / latch | Confirmed |
| `0x40` | `0x01` | Select | Constant in every capture |
| `0x14` | `0x01` | — | Constant in every capture |
| `0x15` | `0x01` | Effect id — **static colour** | `0x01` confirmed; see [Effects](#effects) |
| `0x16` | `0x00` | — | Constant in every capture |
| `0x17` | `0x00`–`0x64` | Brightness, **percent, global** | Confirmed by sweep |
| `0x18` | R | Strip red | Confirmed by isolation |
| `0x19` | G | Strip green | Confirmed by isolation |
| `0x1A` | B | Strip blue | Confirmed by isolation |
| `0x1B` | `0x00` | **Constant — not a colour** | Held across 5 colour changes |
| `0x1C` | `0xC8` | **Constant — not a colour** | Held across 5 colour changes |
| `0x1D` | `0xFF` | **Constant — not a colour** | Held across 5 colour changes |
| `0x20`–`0x23` | `0x00` | — | Constant in every capture |
| `0x50` | `0x01` / `0x00` | **Logo on / off** | Confirmed by isolation + sweep |

### Traps in this table

**`0x1B`–`0x1D` look exactly like a secondary colour and are not.** They held
`00 C8 FF` unchanged across five captured colour changes. Writing the user's
colour there causes intermittent, maddening failures where some colours apply
and others silently don't. Send `00 C8 FF` verbatim.

**`0x17` is a percentage, not a byte range.** Max is `100` (`0x64`), not `255`.

**`0x50` is boolean.** Swept 0–255; every value other than `0x00` and `0x01` is
ignored. The logo has no colour of its own.

**Brightness is global.** `0x17` dims the strip *and* the logo together.
Confirmed by sweeping it and watching both. There is no per-zone brightness.

---

## Write sequences

Writes are paced **~15 ms apart**. Going faster is associated with updates being
silently dropped.

### Full colour update — 37 writes, ≈555 ms

```
# colour block
0x30 <- 0xB0      begin
0x40 <- 0x01      select
0x15 <- 0x01      effect: static
0x16 <- 0x00
0x17 <- bright    0..100
0x18 <- R
0x19 <- G
0x1A <- B
0x1B <- 0x00      constant
0x1C <- 0xC8      constant
0x1D <- 0xFF      constant
0x20 <- 0x00
0x21 <- 0x00
0x22 <- 0x00
0x23 <- 0x00
0x31 <- 0xB1      commit
0x32 <- 0xB2      apply

# trailing frame — note: NO apply after this commit
0x30 <- 0xB0
0x40 <- 0x01
0x14 <- 0x01
0x31 <- 0xB1

# logo frame, repeated 4x
0x30 <- 0xB0
0x50 <- 0x01/0x00
0x31 <- 0xB1
0x32 <- 0xB2
```

The missing apply after the trailing frame's commit, and the fourfold repetition
of the logo frame, both match Lenovo's captured traffic exactly. They may not all
be necessary — but the sequence is known-good as written, and an earlier attempt
to "clean it up" by adding a second commit made things worse.

### Logo-only update — 16 writes

Just the logo frame above, four times. Used when toggling the logo without
touching colour.

---

## Effects

**Only `0x01` (static colour) is safe to send.**

It is the only value ever observed in register `0x15` on the wire. Undefined ids
**do not fail safely** — they leave the controller in a runaway high-speed colour
cycle that persists until a valid frame is sent. This is recoverable (send a
valid static frame) but alarming.

Lenovo's animated effects exist, but their ids have not been captured. The ids
listed in Legion Space's profile JSON (`animationId` fields — 11, 16, 14) are
**not** the wire values. Every one of them was tried and every one was wrong.

To decode them properly: hook `NvAPI_I2CWrite`, set one animated effect in
Legion Space, capture, then set exactly one more and diff. See below.

---

## How this was found

Worth reading if you plan to extend this to another card, because the failures
were systematic rather than random.

### What did not work

**Reading meaning out of Lenovo's config files.** Legion Space ships JSON
profiles with fields that look authoritative — `animationId`, zone names,
brightness scales. These describe its **UI model**. They do not correspond to
the wire protocol. Every value taken from them was wrong:

- Effect ids 11, 16, 14 from `animationId` → all wrong, all caused runaway cycling
- Register `0x14` guessed as the logo toggle → wrong, it is `0x50`
- `0x1B`–`0x1D` assumed to be a secondary colour → wrong, they are constants

**Inferring from bursts where several things changed at once.** If you change
colour *and* brightness and diff two captures, you learn nothing reliable about
which register did what.

### What worked, every time

**Change exactly one thing. Capture. Diff. Repeat.**

The decisive test for the logo: toggle it four times, changing nothing else. The
resulting bursts were byte-for-byte identical except register `0x50`. That
pinned it immediately, after hours of failed inference.

Same method gave the colour registers (five colour changes tracked exactly:
`ff0000`, `0000ff`, `00ff00`, `ff0000`, `0000ff`) and proved `0x1B`–`0x1D` were
constants rather than a second colour.

### Capture rig

- **Frida** attached to `SmartEngineHost64.exe`, hooking `NvAPI_I2CWrite` by
  resolving function id `0xE812EB07` through `nvapi_QueryInterface`
- Log flushed and `fsync`'d per write — an early capture died at 64 seconds and
  was mistakenly read as complete, which wasted a whole round
- Target a single process, and re-attach on detach — Legion Space restarts its
  service
- Close Legion Space entirely when testing your own writes, or it will reassert
  its profile mid-test and you will blame your code

---

## Verifying independence from Lenovo software

Confirmed working with the entire Lenovo stack shut down:

```
taskkill /f /im LegionSpace.exe
taskkill /f /im SmartEngineHost64.exe
taskkill /f /im SmartEngineHostN64.exe
net stop LenovoVantageService
```

Colour, brightness and logo control all continued to work. The only requirement
is the NVIDIA driver.
