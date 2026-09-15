#!/usr/bin/env python3
"""
legion_gpu_rgb.py - drive the RGB on a Lenovo OEM RTX 5070 Ti (10DE:2C05 /
17AA:C773) directly, without Legion Space or any Lenovo motherboard.

WHY THIS WORKS ANYWHERE
-----------------------
The lighting controller lives on the GRAPHICS CARD, reachable through NVIDIA's
NvAPI I2C interface. Nothing Lenovo-specific is involved at the transport layer:
NvAPI talks to the GPU, and the GPU talks to the MCU. Move the card to any
motherboard and this still works.

Captured from SmartEngineHost64.exe (Legion Space's hardware service) with Frida:

    NV_I2C_INFO_V3, version 0x00030040 (64 bytes)
      displayMask   0x00000100
      bIsDDCPort    1
      i2cDevAddress 0x92  (8-bit)  == 0x49 7-bit
      regAddrSize   1
      cbSize        1
      i2cSpeedKhz   4
      portId        1     (bIsPortIdSet = 1)

Every transaction is a single register byte + a single data byte.

NOTE ON WHY OPENRGB MISSED THIS: OpenRGB scans the GPU's I2C bus with
bIsDDCPort = 0 on the default port, so it never probes port 1 / DDC and only
ever reports the unrelated device at 0x5B. That address is a red herring.

REGISTER MAP - confirmed by isolating one control at a time. Two captured
bursts, taken while ONLY the logo was toggled, were byte-for-byte identical
except register 0x50, which is what pins it down:

    0x30 <- 0xB0   begin frame
    0x31 <- 0xB1   commit
    0x32 <- 0xB2   apply / latch
    0x40 <- 0x01   select
    0x14 <- 0x01   constant in every capture
    0x15 <- eff    effect id.  0x01 = STATIC COLOUR (confirmed)
    0x16 <- 0x00
    0x17 <- 0..100 brightness as a PERCENTAGE (100 = max, confirmed)
    0x18..0x1A     R, G, B  (confirmed by isolation: five colour changes tracked
                   exactly - ff0000, 0000ff, 00ff00, ff0000, 0000ff)
    0x1B..0x1D <- 00 C8 FF   CONSTANT. Not a secondary colour - held these exact
                   values across five captured colour changes. Must be sent as-is.
    0x20..0x23 <- 0x00
    0x50 <- 0x01   NVIDIA LOGO ON,  0x00 = logo off.  Sent 4x, each wrapped in
                   its own B0 / B1 / B2 frame.

The logo is on/off only and has no colour of its own; the strip and backplate
are the zone that takes RGB.

EARLIER MISTAKES, recorded so they are not repeated: effect ids 11, 16 and 14
were all guessed from the animationId field in Lenovo's profile JSON and are all
wrong - the real static effect is 0x01. And 0x50 was assumed to be a zone index
and hardcoded to 0x00, which meant every earlier run was explicitly switching
the logo OFF.

SAFETY
------
Writes go ONLY to device 0x92 on port 1 with bIsDDCPort=1 - byte for byte the
same transactions Lenovo's own signed service issues. This does not touch the
VBIOS EEPROM and does not sweep the bus. Do not add a blind address scan to
this script; a stray write in the 0x50-0x57 EEPROM range can brick a card.

USAGE
-----
    py -3 legion_gpu_rgb.py --info
    py -3 legion_gpu_rgb.py --logo on          # logo only, nothing else touched
    py -3 legion_gpu_rgb.py --logo off
    py -3 legion_gpu_rgb.py --color FF0000     # strip red, logo stays on
    py -3 legion_gpu_rgb.py --color 0000FF --brightness 100
    py -3 legion_gpu_rgb.py --off
"""

import argparse
import ctypes
import struct
import sys
import time
from ctypes import CFUNCTYPE, POINTER, byref, c_char, c_int32, c_uint32, c_void_p

# Lenovo's own service paces its register writes ~15 ms apart. Writing faster
# than the MCU accepts is the most likely reason a second colour change is
# ignored, so we match its cadence by default.
DEFAULT_DELAY_MS = 15

# --- NvAPI ids (NVIDIA/nvapi nvapi_interface.h) ---------------------------
ID_INITIALIZE          = 0x0150E828
ID_UNLOAD              = 0xD22BDD7E
ID_GET_ERROR_MESSAGE   = 0x6C2D048C
ID_ENUM_PHYSICAL_GPUS  = 0xE5AC921F
ID_GPU_GET_FULL_NAME   = 0xCEEE8E9F
ID_GPU_GET_PCI_IDS     = 0x2DDFB66E
ID_I2C_WRITE           = 0xE812EB07
ID_I2C_READ            = 0x2FDE12C5

NVAPI_OK = 0

# --- NV_I2C_INFO_V3, exactly as captured ----------------------------------
I2C_STRUCT_SIZE = 0x40
I2C_VERSION     = (I2C_STRUCT_SIZE & 0xFFFF) | (3 << 16)   # 0x00030040

OFF_VERSION       = 0x00
OFF_DISPLAYMASK   = 0x04
OFF_ISDDCPORT     = 0x08
OFF_DEVADDR       = 0x09
OFF_PREGADDR      = 0x10
OFF_REGADDRSIZE   = 0x18
OFF_PDATA         = 0x20
OFF_CBSIZE        = 0x28
OFF_SPEED         = 0x2C
OFF_SPEEDKHZ      = 0x30
OFF_PORTID        = 0x34
OFF_ISPORTIDSET   = 0x38

DISPLAY_MASK = 0x00000100
# Every mask from 0x01 to 0x100 was ACCEPTED by the driver with bIsDDCPort=1,
# which hints that displayMask may be ignored once bIsPortIdSet/portId are set.
# --automask walks these until something takes, for a machine where the
# captured 0x100 turns out to be wrong.
MASK_CANDIDATES = (0x100, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80)
IS_DDC_PORT  = 1
DEV_ADDR_8   = 0x92      # 0x49 in 7-bit form
SPEED_KHZ    = 4
PORT_ID      = 1

# --- register names -------------------------------------------------------
REG_BEGIN      = 0x30
REG_COMMIT     = 0x31
REG_APPLY      = 0x32
REG_SELECT     = 0x40
# The logo lives at 0x50, not 0x14 - see the register map above.
REG_UNK14      = 0x14   # always 0x01 in captures
REG_LOGO       = 0x50   # CONFIRMED: 0x01 = logo on, 0x00 = logo off
REG_EFFECT     = 0x15
REG_UNK16      = 0x16
REG_BRIGHTNESS = 0x17
REG_R1, REG_G1, REG_B1 = 0x18, 0x19, 0x1A
REG_R2, REG_G2, REG_B2 = 0x1B, 0x1C, 0x1D
# 0x1B-0x1D held 00 C8 FF in all five captured colour changes (red, blue,
# green, red, blue) - they do NOT track the chosen colour. Writing the primary
# colour here, as this script previously did, is a deviation from Lenovo.
SECONDARY_CONST = (0x00, 0xC8, 0xFF)

MAGIC_BEGIN, MAGIC_COMMIT, MAGIC_APPLY = 0xB0, 0xB1, 0xB2

EFFECT_STATIC = 0x01   # CONFIRMED: 0x01 = static colour


class NvAPI:
    def __init__(self):
        try:
            dll = ctypes.WinDLL("nvapi64.dll")
        except OSError:
            sys.exit("nvapi64.dll not found (need 64-bit Python + NVIDIA driver).")
        q = None
        for nm in ("nvapi_QueryInterface", "NvAPI_QueryInterface"):
            try:
                q = getattr(dll, nm)
                break
            except AttributeError:
                pass
        if q is None:
            sys.exit("nvapi_QueryInterface not exported.")
        q.restype = c_void_p
        q.argtypes = [c_uint32]
        self._q = q
        self._cache = {}

    def fn(self, fid, restype=c_int32, argtypes=()):
        if fid not in self._cache:
            addr = self._q(fid)
            self._cache[fid] = CFUNCTYPE(restype, *argtypes)(addr) if addr else None
        return self._cache[fid]

    def err(self, status):
        f = self.fn(ID_GET_ERROR_MESSAGE, c_int32, (c_int32, POINTER(c_char * 64)))
        if not f:
            return str(status)
        buf = (c_char * 64)()
        f(status, byref(buf))
        return f"{status} ({buf.value.decode(errors='replace')})"


def build_i2c_info(reg_buf, data_buf, data_len):
    b = ctypes.create_string_buffer(I2C_STRUCT_SIZE)
    ctypes.memset(b, 0, I2C_STRUCT_SIZE)
    b[OFF_VERSION:OFF_VERSION + 4]         = struct.pack("<I", I2C_VERSION)
    b[OFF_DISPLAYMASK:OFF_DISPLAYMASK + 4] = struct.pack("<I", DISPLAY_MASK)
    b[OFF_ISDDCPORT]                       = IS_DDC_PORT
    # NOTE: DISPLAY_MASK and IS_DDC_PORT are module globals so --displaymask
    # and --ddc can override them at runtime. displayMask selects WHICH display
    # head's DDC bus the transaction rides; 0x100 was what Lenovo used on this
    # machine with this monitor layout, and it is not guaranteed to be right on
    # a different board or with displays plugged into different outputs.
    b[OFF_DEVADDR]                         = DEV_ADDR_8
    b[OFF_PREGADDR:OFF_PREGADDR + 8]       = struct.pack("<Q", ctypes.addressof(reg_buf))
    b[OFF_REGADDRSIZE:OFF_REGADDRSIZE + 4] = struct.pack("<I", 1)
    b[OFF_PDATA:OFF_PDATA + 8]             = struct.pack("<Q", ctypes.addressof(data_buf))
    b[OFF_CBSIZE:OFF_CBSIZE + 4]           = struct.pack("<I", data_len)
    b[OFF_SPEED:OFF_SPEED + 4]             = struct.pack("<I", 0xFFFF)
    b[OFF_SPEEDKHZ:OFF_SPEEDKHZ + 4]       = struct.pack("<I", SPEED_KHZ)
    b[OFF_PORTID:OFF_PORTID + 4]           = struct.pack("<I", PORT_ID)
    b[OFF_ISPORTIDSET:OFF_ISPORTIDSET + 4] = struct.pack("<I", 1)
    return b


class LegionGpuRgb:
    def __init__(self, verbose=False, delay_ms=DEFAULT_DELAY_MS):
        self.api = NvAPI()
        self.verbose = verbose
        self.delay = delay_ms / 1000.0

        st = self.api.fn(ID_INITIALIZE)()
        if st != NVAPI_OK:
            sys.exit(f"NvAPI_Initialize failed: {self.api.err(st)}")

        handles = (c_void_p * 64)()
        count = c_uint32(0)
        enum = self.api.fn(ID_ENUM_PHYSICAL_GPUS, c_int32,
                           (POINTER(c_void_p * 64), POINTER(c_uint32)))
        st = enum(byref(handles), byref(count))
        if st != NVAPI_OK or count.value == 0:
            sys.exit(f"No GPUs: {self.api.err(st)}")

        self.gpu = handles[0]
        self._write = self.api.fn(ID_I2C_WRITE, c_int32, (c_void_p, c_void_p))
        self._read = self.api.fn(ID_I2C_READ, c_int32, (c_void_p, c_void_p))
        if self._write is None:
            sys.exit("NvAPI_I2CWrite not available in this driver.")

    def info(self):
        name = (c_char * 64)()
        self.api.fn(ID_GPU_GET_FULL_NAME, c_int32,
                    (c_void_p, POINTER(c_char * 64)))(self.gpu, byref(name))
        dev, sub, rev, ext = (c_uint32(), c_uint32(), c_uint32(), c_uint32())
        self.api.fn(ID_GPU_GET_PCI_IDS, c_int32,
                    (c_void_p, POINTER(c_uint32), POINTER(c_uint32),
                     POINTER(c_uint32), POINTER(c_uint32)))(
            self.gpu, byref(dev), byref(sub), byref(rev), byref(ext))
        print(f"GPU        : {name.value.decode(errors='replace')}")
        print(f"PCI        : {dev.value >> 16:04X}:{dev.value & 0xFFFF:04X}")
        print(f"Subsystem  : {sub.value >> 16:04X}:{sub.value & 0xFFFF:04X}")
        print(f"RGB target : 0x{DEV_ADDR_8 >> 1:02X} (7-bit) on port {PORT_ID}, DDC={IS_DDC_PORT}")

    def w(self, reg, val):
        reg_buf = ctypes.create_string_buffer(bytes([reg]), 1)
        data_buf = ctypes.create_string_buffer(bytes([val]), 1)
        info = build_i2c_info(reg_buf, data_buf, 1)
        st = self._write(self.gpu, ctypes.cast(info, c_void_p))
        if self.verbose or st != NVAPI_OK:
            flag = "" if st == NVAPI_OK else f"   <-- {self.api.err(st)}"
            print(f"  reg 0x{reg:02X} <- 0x{val:02X}{flag}")
        if self.delay:
            time.sleep(self.delay)
        return st == NVAPI_OK

    def r(self, reg):
        """Read one register back. Returns int, or None if the read failed."""
        if self._read is None:
            return None
        reg_buf = ctypes.create_string_buffer(bytes([reg]), 1)
        data_buf = ctypes.create_string_buffer(1)
        info = build_i2c_info(reg_buf, data_buf, 1)
        st = self._read(self.gpu, ctypes.cast(info, c_void_p))
        if self.delay:
            time.sleep(self.delay)
        if st != NVAPI_OK:
            return None
        return data_buf[0][0] if isinstance(data_buf[0], bytes) else data_buf[0]

    def dump(self):
        """Read back the registers we care about, to see whether writes stick."""
        groups = [
            ("frame/control", [0x14, 0x15, 0x16, 0x17, 0x40, 0x50]),
            ("primary RGB",   [0x18, 0x19, 0x1A]),
            ("secondary RGB", [0x1B, 0x1C, 0x1D]),
            ("aux",           [0x20, 0x21, 0x22, 0x23]),
        ]
        print("\nregister read-back:")
        any_ok = False
        for label, regs in groups:
            parts = []
            for reg in regs:
                v = self.r(reg)
                if v is None:
                    parts.append(f"0x{reg:02X}=--")
                else:
                    any_ok = True
                    parts.append(f"0x{reg:02X}=0x{v:02X}")
            print(f"  {label:<14} " + "  ".join(parts))
        if not any_ok:
            print("  (every read failed - this MCU may be write-only, which is")
            print("   normal for these controllers and not itself a problem)")

    def set_logo(self, on, level=None):
        """Minimal frame that only toggles the NVIDIA logo (on/off, no colour).

        This is the smallest useful command on the card and the one that matters
        for getting the glass panel lit in a non-Lenovo build.
        """
        value = level if level is not None else (0x01 if on else 0x00)
        ok = True
        for _ in range(4):          # Lenovo sends this frame four times
            ok &= self.w(REG_BEGIN, MAGIC_BEGIN)
            ok &= self.w(REG_LOGO, value)
            ok &= self.w(REG_COMMIT, MAGIC_COMMIT)
            ok &= self.w(REG_APPLY, MAGIC_APPLY)
        return ok

    # --- the captured apply sequence --------------------------------------
    def apply(self, effect, r, g, b, r2, g2, b2, brightness, logo=True):
        ok = True
        ok &= self.w(REG_BEGIN, MAGIC_BEGIN)
        ok &= self.w(REG_SELECT, 0x01)
        ok &= self.w(REG_EFFECT, effect)
        ok &= self.w(REG_UNK16, 0x00)
        ok &= self.w(REG_BRIGHTNESS, brightness)
        ok &= self.w(REG_R1, r)
        ok &= self.w(REG_G1, g)
        ok &= self.w(REG_B1, b)
        ok &= self.w(REG_R2, r2)
        ok &= self.w(REG_G2, g2)
        ok &= self.w(REG_B2, b2)
        for reg in (0x20, 0x21, 0x22, 0x23):
            ok &= self.w(reg, 0x00)
        ok &= self.w(REG_COMMIT, MAGIC_COMMIT)
        ok &= self.w(REG_APPLY, MAGIC_APPLY)

        # trailing sequence, replayed verbatim from the capture
        ok &= self.w(REG_BEGIN, MAGIC_BEGIN)
        ok &= self.w(REG_SELECT, 0x01)
        ok &= self.w(REG_UNK14, 0x01)
        ok &= self.w(REG_COMMIT, MAGIC_COMMIT)
        for _ in range(4):
            ok &= self.w(REG_BEGIN, MAGIC_BEGIN)
            ok &= self.w(REG_LOGO, 0x01 if logo else 0x00)
            ok &= self.w(REG_COMMIT, MAGIC_COMMIT)
            ok &= self.w(REG_APPLY, MAGIC_APPLY)
        return ok

    def close(self):
        f = self.api.fn(ID_UNLOAD)
        if f:
            f()


def parse_rgb(text):
    t = text.lstrip("#")
    if len(t) != 6:
        raise argparse.ArgumentTypeError("colour must be RRGGBB hex, e.g. FF0000")
    return int(t[0:2], 16), int(t[2:4], 16), int(t[4:6], 16)


def main():
    ap = argparse.ArgumentParser(description="Drive Lenovo OEM RTX 5070 Ti lighting via NvAPI I2C.")
    ap.add_argument("--info", action="store_true", help="print GPU identity and exit")
    ap.add_argument("--color", type=parse_rgb, help="primary colour, RRGGBB hex")
    ap.add_argument("--color2", type=parse_rgb,
                    help="secondary colour (default 00C8FF, the constant Lenovo always sends)")
    ap.add_argument("--effect", type=lambda s: int(s, 0), default=EFFECT_STATIC,
                    help="effect id (default 1 = static colour, confirmed)")
    ap.add_argument("--brightness", type=lambda s: int(s, 0), default=100,
                    help="brightness 0-100 (default 100). 100 is the maximum - "
                         "the byte is a percentage, not 0-255.")
    ap.add_argument("--off", action="store_true", help="all zones black")
    ap.add_argument("-v", "--verbose", action="store_true", help="log every register write")
    ap.add_argument("--delay", type=int, default=DEFAULT_DELAY_MS,
                    help=f"ms between register writes (default {DEFAULT_DELAY_MS}, "
                         "matching Lenovo's own pacing; try 30 if changes are ignored)")
    ap.add_argument("--read", action="store_true",
                    help="read the registers back after applying, to prove writes landed")
    ap.add_argument("--repeat", type=int, default=1,
                    help="apply the sequence N times (default 1, as Lenovo does)")
    ap.add_argument("--displaymask", type=lambda s: int(s, 0), default=None,
                    help="override displayMask (captured value 0x100)")
    ap.add_argument("--ddc", type=int, choices=(0, 1), default=None,
                    help="override bIsDDCPort (captured value 1)")
    ap.add_argument("--logo-level", type=lambda v: int(v, 0), default=None,
                    help="EXPERIMENTAL: write an arbitrary value to 0x50 instead of "
                         "0/1. Only 0x00 and 0x01 were ever observed on the wire, so "
                         "intermediate values may act as brightness, may be clamped to "
                         "on/off, or may confuse the controller. Recoverable: pick any "
                         "theme in Legion Space to reset.")
    ap.add_argument("--logo", choices=("on", "off"), default=None,
                    help="toggle the NVIDIA logo only (on/off, it has no colour). "
                         "Used alone this sends the minimal logo frame and nothing else.")
    ap.add_argument("--automask", action="store_true",
                    help="send the command once per candidate displayMask "
                         "(0x100, 0x01..0x80). Use if the card does not respond "
                         "on a different motherboard or monitor layout.")
    ap.add_argument("--probe", action="store_true",
                    help="try every plausible displayMask/DDC combination and "
                         "report which ones the driver accepts (writes only the "
                         "harmless 0x30<-0xB0 begin byte)")
    args = ap.parse_args()

    if ctypes.sizeof(c_void_p) != 8:
        sys.exit("Use 64-bit Python.")

    if not 0 <= args.brightness <= 100:
        sys.exit(f"--brightness must be 0-100 (got {args.brightness}); "
                 "100 is full brightness, this byte is a percentage.")

    global DISPLAY_MASK, IS_DDC_PORT
    if args.displaymask is not None:
        DISPLAY_MASK = args.displaymask
    if args.ddc is not None:
        IS_DDC_PORT = args.ddc

    dev = LegionGpuRgb(verbose=args.verbose, delay_ms=args.delay)
    dev.info()

    if args.probe:
        print("\nprobing displayMask / DDC combinations")
        print("(writes only the 0x30 <- 0xB0 begin byte, which is inert on its own)\n")
        saved_mask, saved_ddc = DISPLAY_MASK, IS_DDC_PORT
        hits = []
        for ddc in (1, 0):
            for mask in (0x100, 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                         0x200, 0x400, 0x800, 0x1000, 0x0):
                DISPLAY_MASK, IS_DDC_PORT = mask, ddc
                ok = dev.w(REG_BEGIN, MAGIC_BEGIN)
                mark = "OK  <=== accepted" if ok else "rejected"
                print(f"  ddc={ddc}  mask=0x{mask:04X}  {mark}")
                if ok:
                    hits.append((ddc, mask))
        DISPLAY_MASK, IS_DDC_PORT = saved_mask, saved_ddc
        print(f"\n{len(hits)} accepted combination(s): "
              + (", ".join(f"ddc={d}/mask=0x{m:X}" for d, m in hits) or "none"))
        print("An accepted write only means the driver reached a bus - it does not")
        print("prove the lighting MCU is on that bus. Try setting a colour on each.")
        dev.close()
        return

    if args.info:
        if args.read:
            dev.dump()
        dev.close()
        return

    def run_over_masks(fn):
        """Run fn() once, or once per candidate displayMask with --automask."""
        global DISPLAY_MASK
        if not args.automask:
            return fn()
        ok = True
        original = DISPLAY_MASK
        for mask in MASK_CANDIDATES:
            DISPLAY_MASK = mask
            print(f"\n--- displayMask 0x{mask:04X} ---")
            ok &= fn()
            time.sleep(0.4)
        DISPLAY_MASK = original
        print("\nIf the card responded, note which mask was live when it did,"
              "\nthen use --displaymask 0xNN from now on.")
        return ok

    # --logo-level: probe whether 0x50 is boolean or a scale
    if args.logo_level is not None:
        if not 0 <= args.logo_level <= 255:
            sys.exit("--logo-level must be 0-255")
        print(f"\nlogo register 0x50 <- 0x{args.logo_level:02X} ({args.logo_level})")
        ok = dev.set_logo(True, level=args.logo_level)
        print("done" if ok else "one or more writes failed")
        dev.close()
        return

    # --logo on its own = minimal logo frame, nothing else touched
    if args.logo is not None and not args.color and not args.off:
        want = args.logo == "on"
        print(f"\nlogo -> {'ON' if want else 'OFF'} (minimal frame, no colour registers)")
        ok = run_over_masks(lambda: dev.set_logo(want))
        print("done" if ok else "one or more writes failed")
        if args.read:
            dev.dump()
        dev.close()
        return

    if args.off:
        r = g = b = r2 = g2 = b2 = 0
        effect = EFFECT_STATIC
    else:
        if not args.color:
            dev.close()
            sys.exit("\nNothing to do. Pass --color RRGGBB, or --off, or --info.")
        r, g, b = args.color
        r2, g2, b2 = args.color2 if args.color2 else SECONDARY_CONST
        effect = args.effect

    print(f"\nApplying effect 0x{effect:02X}  primary #{r:02X}{g:02X}{b:02X}"
          f"  secondary #{r2:02X}{g2:02X}{b2:02X}  brightness 0x{args.brightness:02X}"
          f"  ({args.delay} ms pacing)")

    ok = True
    for i in range(max(1, args.repeat)):
        if args.repeat > 1:
            print(f"  pass {i + 1}/{args.repeat}")
        ok &= run_over_masks(lambda: dev.apply(
            effect, r, g, b, r2, g2, b2, args.brightness,
            logo=(args.logo != "off")))

    print("done" if ok else "one or more writes failed (see above)")

    if args.read:
        dev.dump()

    dev.close()


if __name__ == "__main__":
    main()
