# OpenRGB core patch — selectable DDC port

`openrgb-nvapi-ddc.patch` makes `is_ddc_port` selectable in OpenRGB's NvAPI I2C
backend instead of hardcoded to `0`, and registers a **second bus per GPU** with
the flag set.

## Why this matters more than the plugin

The plugin in this repository is a workaround. It carries its own NvAPI layer
purely to bypass OpenRGB's I2C code, which is wasteful and only helps this one
card.

This patch is the actual fix. Every OEM card whose RGB controller sits on the
DDC side becomes visible to OpenRGB's normal detection, and existing detectors
get a chance to find hardware they currently cannot reach. The existing bus is
left untouched, so nothing that works today changes.

## What it changes

| File | Change |
|------|--------|
| `i2c_smbus/Windows/i2c_smbus_nvapi.h` | Constructor takes `bool use_ddc_port = false`; member added |
| `i2c_smbus/Windows/i2c_smbus_nvapi.cpp` | Both transfer functions honour the flag; detection registers a second bus per GPU named `… (DDC)` |

Default behaviour is unchanged — `use_ddc_port` defaults to `false`.

## Applying it

```
cd /path/to/OpenRGB
git apply /path/to/openrgb-nvapi-ddc.patch
```

Built against OpenRGB `release_candidate_1.0rc3`. On newer master the
surrounding code has moved; the change itself is small enough to reapply by
hand.

## Status

**Not yet submitted upstream.** If you are reading this and want to help, taking
this to OpenRGB as a merge request is the highest-value contribution available:

<https://gitlab.com/CalcProgrammer1/OpenRGB>

Reviewers will reasonably want to know the cost of registering a second bus per
GPU, since detection then probes twice as many buses. Worth measuring before
proposing.
