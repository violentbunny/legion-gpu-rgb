# SignalRGB

**Short answer: no native SignalRGB plugin is possible for this hardware. Use a
bridge through OpenRGB.**

---

## Why no plugin can exist

SignalRGB's plugin API supports exactly four transports:

- HID
- Raw USB (libusb)
- Hybrid (both)
- Serial (COM port)

There is no I2C or SMBus path, and no way to call NvAPI from a plugin. This
card's controller lives on the DDC side of the GPU's I2C bus and is reachable
only through NvAPI. Nothing in SignalRGB's plugin surface can get there.

This is not a gap that can be coded around. Don't spend time on it.

(Reference: <https://docs.signalrgb.com/developer/plugins/>)

---

## What does work

```
SignalRGB → SignalRGB-To-OpenRGB-Bridge → OpenRGB SDK server → this plugin → GPU
```

The bridge is a SignalRGB *Third Party Service* that speaks the OpenRGB SDK
protocol over TCP, registers OpenRGB's devices inside SignalRGB, and streams
`UPDATELEDS` to them. Once the OpenRGB plugin is installed, this card is an
ordinary OpenRGB device and appears there like any other.

### Setup

1. **OpenRGB** → Settings → General → **Enable SDK Server** (default
   `127.0.0.1:6742`). Restart OpenRGB **as administrator**.
2. **SignalRGB** → Third Party Services → install the bridge addon
   (<https://github.com/Fefedu973/SignalRGB-To-OpenRGB-Bridge>). Point it at that
   host and port, refresh, and tick `Lenovo Legion GPU (…) Strip`.
3. Leave the `Logo` device **unticked** — it has no colour, so SignalRGB has
   nothing meaningful to send it. Keep using OpenRGB's Off/On dropdown for the
   logo.

---

## What to expect

**A slideshow, not an animation.**

The bridge streams frames at effect rate. The hardware accepts roughly **one
colour every 0.6 seconds** — 37 register writes paced 15 ms apart.

The plugin's controller coalesces: it applies the newest requested colour and
discards everything superseded while the bus was busy. So nothing backs up, the
UI never blocks, and there is no growing backlog of stale colours. But fast
effects will land on scattered samples of themselves.

Slow ambient effects look fine. A fast rainbow does not.

This ceiling is Lenovo's write pacing. No software layer can lift it, and the
bridge is not at fault.

If you want the GPU animating in sync with fast effects, this hardware will not
do it — driving it from OpenRGB directly is the less frustrating way to live
with that.
