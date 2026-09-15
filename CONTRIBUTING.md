# Contributing

## Most useful contributions

**1. Hardware reports for other Lenovo OEM cards.**

Only one card is confirmed. The transport is generic NvAPI, so the protocol may
well be shared across the Legion GPU line — but nobody has checked. Open an
issue using the *New card report* template with the output of:

```
py -3 tool/legion_gpu_rgb.py --info
py -3 tool/legion_gpu_rgb.py --probe
```

A confirmed card added to the compatibility table helps more people than any
code change here.

**2. Captures of Lenovo's animated effects.**

Register `0x15` selects the effect. Only `0x01` (static) has been captured.
Undefined values leave the controller in a runaway colour cycle, so these must
be **captured, not guessed** — see
[docs/protocol.md](docs/protocol.md#how-this-was-found).

**3. Porting the plugin to OpenRGB plugin API 5.**

**4. Taking the core patch upstream** — see
[openrgb-patch/README.md](openrgb-patch/README.md).

---

## If you are decoding registers

One rule, learned expensively:

> **Change exactly one thing. Capture. Diff. Repeat.**

Every value in this project that was inferred from Lenovo's config files was
wrong. Every value that came from isolating a single control was right. Legion
Space's JSON describes its UI model, not the wire protocol — the two do not
correspond.

Do not send a register value you have not seen on the wire. This controller does
not fail safely on undefined input.

Please say in your PR **how** each value was confirmed. "Observed in N captures
where only X changed" is the standard. Anything softer than that should be
marked as unconfirmed in the table.

---

## Code style

Match the surrounding code. The C++ follows OpenRGB's conventions (aligned
declarations, `return(x)`, block comment banners) so that pieces of it can move
upstream without reformatting.

---

## Safety

Do not run the tool against non-Lenovo cards, and do not encourage anyone else
to. Writing to an unknown I2C device address is how you discover what else lives
on that bus.

---

## License

Contributions are accepted under **GPL-2.0-or-later**, matching the repository.
