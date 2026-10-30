#!/usr/bin/env python3
"""gen-elementary-tests.py ROOL_VFPSUPPORT_DIR OUT.h: make the data for
tests/programs/t_elementary.c, which checks VFPSupport_ElementaryFunctions.

  - Sampled values: for each function and precision, inputs spread over its
    domain as RISC OS Open's own test bed (Test/Elementary/c/TestTable)
    spreads them (uniformly in the bit patterns between its ranges' ends),
    with the correctly rounded result worked out with mpmath at high
    precision. t_elementary allows 1 ulp, as ROOL's test does (QUIET1ULP).
  - Edge cases: ROOL's own table of special inputs, with the results and
    the exception flags the C standard asks for (Test/Elementary/c/
    TestTable, "edges"; BSD licence, RISC OS Open Ltd), copied as data.

The output is kept in the repository (tests/programs/elementary_data.h),
so building the tests needs neither mpmath nor ROOL's source.

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE); the edge-case
data is RISC OS Open's (see the header it writes)."""
import random
import re
import struct
import sys

import mpmath as mp

FUNCS = ["sin", "cos", "tan", "asin", "acos", "atan", "atan2", "log",
         "log10", "exp", "pow", "fma"]
N = 120          # samples per range (and sign) and precision

# Domains, as ROOL's TestTable (lo, hi bit patterns; double, float), plus
# atan2 (ROOL has no reference for it) and fma.
ALL64 = (0x0000000000000000, 0x7FEFFFFFFFFFFFFF)
ALL32 = (0x00000000, 0x7F7FFFFF)
UNIT64 = (0x0000000000000000, 0x3FF0000000000000)
UNIT32 = (0x00000000, 0x3F800000)
NORM64 = (0x0010000000000000, 0x7FEFFFFFFFFFFFFF)
NORM32 = (0x00800000, 0x7F7FFFFF)
POWY64 = (0x0000000000000000, 0x43FFFFFFFFFFFFFF)
POWY32 = (0x00000000, 0x5FFFFFFF)
# name: (arg0 (range64, range32, negatives too), arg1 or None)
DOMAINS = {
    "sin": ((ALL64, ALL32, True), None),
    "cos": ((ALL64, ALL32, True), None),
    "tan": ((ALL64, ALL32, True), None),
    "asin": ((UNIT64, UNIT32, True), None),
    "acos": ((UNIT64, UNIT32, True), None),
    "atan": ((ALL64, ALL32, True), None),
    "atan2": ((NORM64, NORM32, True), (NORM64, NORM32, True)),
    "log": ((ALL64, ALL32, False), None),
    "log10": ((ALL64, ALL32, False), None),
    "exp": ((ALL64, ALL32, True), None),
    "pow": ((ALL64, ALL32, False), (POWY64, POWY32, True)),
    "fma": ((NORM64, NORM32, True), (NORM64, NORM32, True)),
}


def d2b(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def b2d(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def f2b(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def b2f(b):
    return struct.unpack("<f", struct.pack("<I", b))[0]


def round_to(v, mant, emin, emax):
    """mpf -> the nearest binary floating-point value with MANT significand
    bits, minimum normal exponent EMIN and maximum EMAX (ties to even;
    subnormals and overflow to infinity as IEEE 754), as a Python float.
    Rounded once, from the high-precision value."""
    if mp.isnan(v):
        return float("nan")
    if mp.isinf(v) or v == 0:
        return float(v)
    sign = -1 if v < 0 else 1
    a = abs(v)
    if a >= mp.mpf(2) ** (emax + 1):
        return sign * float("inf")
    if a < mp.mpf(2) ** (emin - mant - 1):
        return sign * 0.0
    m, e2 = mp.frexp(a)                     # a = m * 2^e2, 0.5 <= m < 1
    e = int(e2) - 1                         # 2^e <= a < 2^(e+1)
    q = max(e, emin) - (mant - 1)          # the quantum at this exponent
    n = mp.nint(mp.ldexp(a, -q))            # nint: ties to even
    r = mp.ldexp(n, q)
    if r >= mp.mpf(2) ** (emax + 1):
        return sign * float("inf")
    return sign * float(r)                  # exact: it fits a double


def to_double(v):
    return round_to(v, 53, -1022, 1023)


def to_float(v):
    return round_to(v, 24, -126, 127)


def ref(name, args):
    a = [mp.mpf(x) for x in args]
    if name == "sin":
        return mp.sin(a[0])
    if name == "cos":
        return mp.cos(a[0])
    if name == "tan":
        return mp.tan(a[0])
    if name == "asin":
        return mp.asin(a[0])
    if name == "acos":
        return mp.acos(a[0])
    if name == "atan":
        return mp.atan(a[0])
    if name == "atan2":
        return mp.atan2(a[0], a[1])     # (y, x)
    if name == "log":
        return mp.log(a[0]) if a[0] > 0 else (mp.ninf if a[0] == 0 else mp.nan)
    if name == "log10":
        return mp.log10(a[0]) if a[0] > 0 else (mp.ninf if a[0] == 0 else mp.nan)
    if name == "exp":
        return mp.exp(a[0])
    if name == "pow":
        if a[0] == 0:
            return mp.mpf(0) if a[1] > 0 else (mp.mpf(1) if a[1] == 0 else mp.inf)
        return mp.power(a[0], a[1])
    if name == "fma":
        return a[0] * a[1] + a[2]
    raise KeyError(name)


def pick(rng, lo, hi, n):
    return [min(hi, lo + (hi - lo) * i // (n - 1) + rng.randrange(0, max(1, (hi - lo) // (n * 4))))
            for i in range(n)]


def samples(rng, name):
    """[(precision, [args bits], result bits)]"""
    out = []
    a0, a1 = DOMAINS[name]
    nargs = 3 if name == "fma" else (2 if a1 else 1)
    for prec in (64, 32):
        sign = 1 << (prec - 1)
        conv = (b2d, d2b) if prec == 64 else (b2f, f2b)
        r0 = a0[0] if prec == 64 else a0[1]
        xs = pick(rng, r0[0], r0[1], N)
        if a0[2]:
            xs += [x | sign for x in pick(rng, r0[0], r0[1], N)]
        for x in xs:
            args = [x]
            if a1:
                r1 = a1[0] if prec == 64 else a1[1]
                y = rng.randrange(r1[0], r1[1])
                if a1[2] and rng.random() < 0.5:
                    y |= sign
                args.append(y)
            if nargs == 3:
                z = rng.randrange(r0[0], r0[1]) | (sign if rng.random() < 0.5 else 0)
                # keep z near x*y so the addition matters
                if rng.random() < 0.7:
                    p = conv[0](args[0]) * conv[0](args[1])
                    big = 1e300 if prec == 64 else 1e37
                    if p != 0 and 1 / big < abs(p) < big:
                        z = conv[1](-p * (1 + rng.uniform(-1e-6, 1e-6)))
                args.append(z)
            vals = [conv[0](b) for b in args]
            if any(v != v or v in (float("inf"), float("-inf")) for v in vals):
                continue
            with mp.workprec(4000 if name in ("sin", "cos", "tan") else 400):
                r = ref(name, vals)
                res = to_double(r) if prec == 64 else to_float(r)
            out.append((prec, args, conv[1](res)))
    return out


def edges(rool):
    t = open(f"{rool}/Test/Elementary/c/TestTable", encoding="latin-1").read()
    body = t[t.index("edges[] ="):]
    body = body[body.index("{") + 1:body.rindex("};")]
    consts = {"INFP32": 0x7F800000, "INFM32": 0xFF800000, "SNaN32": 0x7F800666,
              "QNaN32": 0x7FC00000, "INFP64": 0x7FF0000000000000,
              "INFM64": 0xFFF0000000000000, "SNaN64": 0x7FF0000000000666,
              "QNaN64": 0x7FF8000000000000, "FPSCR_IOC": 1, "FPSCR_DZC": 2,
              "FPSCR_OFC": 4, "FPSCR_UFC": 8, "FPSCR_IXC": 16, "FPSCR_IDC": 128,
              "true": 1, "false": 0}
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    out = []
    for m in re.finditer(r"CALL_NAME\((\w+)\),\s*\{([^}]*)\}\s*,(.*?)(?=CALL_NAME|\Z)", body, re.S):
        name = m.group(1)
        p = [x.strip() for x in m.group(2).split(",") if x.strip()]
        rest = [x.strip() for x in m.group(3).replace("\n", " ").split(",") if x.strip()]
        def val(x):
            x = x.replace("uLL", "").replace("ULL", "")
            return eval(x, {}, consts)
        pv = [val(x) for x in p]
        rv = [val(x) for x in rest]
        out.append((name, pv, rv))
    return out


def main():
    rool, outp = sys.argv[1], sys.argv[2]
    rng = random.Random(20261017)
    lines = ["/* Generated by tests/gen-elementary-tests.py: data for t_elementary.c.",
             " * Sampled values: correctly rounded results from mpmath.",
             " * Edge cases: from RISC OS Open's VFPSupport Test/Elementary/c/TestTable:",
             " *   Copyright (c) 2021 RISC OS Open Limited. All rights reserved.",
             " *   Redistribution and use in source and binary forms, with or without",
             " *   modification, are permitted provided that the conditions in the",
             " *   BSD licence of that file are met (retain this notice; in binary",
             " *   form, reproduce it in the documentation). THIS DATA IS PROVIDED",
             " *   \"AS IS\" WITHOUT WARRANTY OF ANY KIND (see the full text in",
             " *   LICENCES.md).",
             " */",
             "enum { " + ", ".join(f"F_{f}" for f in FUNCS) + ", F_COUNT };",
             "static const char *const fn_names[] = { " + ", ".join(f'"{f}"' for f in FUNCS) + " };",
             "typedef struct { unsigned char fn, prec, nargs; unsigned long long a[3], r; } sample_t;",
             "static const sample_t samples[] = {"]
    count = 0
    for i, f in enumerate(FUNCS):
        for prec, args, r in samples(rng, f):
            a = args + [0] * (3 - len(args))
            lines.append(f"  {{ {i}, {prec}, {len(args)}, {{ 0x{a[0]:x}uLL, 0x{a[1]:x}uLL, 0x{a[2]:x}uLL }}, 0x{r:x}uLL }},")
            count += 1
    lines.append("};")
    lines.append("typedef struct { unsigned char fn, nargs; unsigned long long in64[2]; unsigned in32[2];"
                 " unsigned long long out64; unsigned out32; unsigned char mask, flags; } edge_t;")
    lines.append("static const edge_t edges[] = {")
    ne = 0
    for name, p, r in edges(rool):
        # p: present0, in64_0, in32_0, present1, in64_1, in32_1
        nargs = 1 + (p[3] if len(p) > 3 else 0)
        in64 = [p[1], p[4] if len(p) > 4 else 0]
        in32 = [p[2], p[5] if len(p) > 5 else 0]
        lines.append(f"  {{ F_{name}, {nargs}, {{ 0x{in64[0]:x}uLL, 0x{in64[1]:x}uLL }}, {{ 0x{in32[0]:x}, 0x{in32[1]:x} }},"
                     f" 0x{r[0]:x}uLL, 0x{r[1]:x}, 0x{r[2]:x}, 0x{r[3]:x} }},")
        ne += 1
    lines.append("};")
    open(outp, "w").write("\n".join(lines) + "\n")
    print(f"{count} samples, {ne} edge cases")


if __name__ == "__main__":
    main()
