#!/usr/bin/env python3
"""objasm2gas.py SRC_DIR OUT_DIR VARIANT: translate RISC OS Open's VFPSupport
elementary functions (s/Trig32, Trig64, ArcTrig32, ArcTrig64, Power32,
Power64, Fused32, Fused64, CMath; objasm syntax) to GNU as, for the test
bed's VFPSupport. Used once to make the .s files in this directory (they
are kept, so building needs no ROOL source); kept to show how they were
made, and to make them again from a newer ROOL release.

What it does:
  - keeps the files' copyright and licence headers;
  - expands ROOL's macros (s/Macros) for a module that has to run on any
    VFP: the variants for cores without ARMv6T2 (no UBFX/SBFX); and, as
    ROOL builds a ROM for each kind of machine, two variants: VARIANT v3
    for cores without VFPv4 (FMA done in software by fused32/64_muladd)
    and v4 for VFPv4 cores (the VFMA instruction). VFPSupport picks one at
    start-up from Linux's HWCAP;
  - evaluates the conditional assembly with those settings
    (NoARMT2 TRUE, NoARMVv4 as the variant, UseSharedCLib FALSE,
    BehaveLikeVFP TRUE);
  - turns objasm syntax into GNU as syntax: labels, local labels and
    %F/%B references, DCD/DCW, * (EQU), ^ / # storage maps, ASSERT,
    expression operators, Push/Pull;
  - prefixes every label with the variant, and each file's own labels with
    the file name too (it's all one assembly unit).

Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE)."""
import os
import re
import sys

FILES = ["CMath", "Trig32", "Trig64", "ArcTrig32", "ArcTrig64",
         "Power32", "Power64", "Fused32", "Fused64"]
FLAGS = {"NoARMT2": True, "NoARMVv4": True, "UseSharedCLib": False,
         "BehaveLikeVFP": True, "{TRUE}": True, "{FALSE}": False}
# Names other files use: ROOL's EXPORTs.
COND = r"(?:EQ|NE|CS|CC|HS|LO|MI|PL|VS|VC|HI|LS|GE|LT|GT|LE|AL)?"


def expr(e):
    """An objasm expression in GNU as syntax."""
    e = e.replace(":SHL:", "<<").replace(":SHR:", ">>")
    e = e.replace(":OR:", "|").replace(":AND:", "&").replace(":EOR:", "^")
    e = re.sub(r"&([0-9A-Fa-f]+)", r"0x\1", e)
    return e


def split_comment(line):
    out, q = "", False
    for i, c in enumerate(line):
        if c == '"':
            q = not q
        if c == ";" and not q:
            return out, line[i + 1:]
        out += c
    return out, None


def dnum(reg):
    m = re.fullmatch(r"\s*[dDsS](\d+)\s*", reg)
    if not m:
        raise ValueError(f"not a VFP register: {reg}")
    return int(m.group(1))


def expand_macro(name, args, ind):
    """ROOL's s/Macros, NoARMT2 and NoARMVv4 variants. Returns lines."""
    a = [x.strip() for x in args.split(",")]
    L = []
    if name == "ExpBits64":
        L = [f"mov\t{a[0]}, {a[1]}, lsl #1", f"mov\t{a[0]}, {a[0]}, lsr #21"]
    elif name == "ExpBits32":
        L = [f"mov\t{a[0]}, {a[1]}, lsl #1", f"mov\t{a[0]}, {a[0]}, lsr #24"]
    elif name == "LoadExp64":
        L = [f"ldr\t{a[0]}, =(1023 + ({expr(a[1])})) << 20"]
    elif name == "LoadExp32":
        L = [f"ldr\t{a[0]}, =(127 + ({expr(a[1])})) << 23"]
    elif name in ("IsNaNorINF64", "IsZeroOrSubNormal64"):
        hi = dnum(a[0]) * 2 + 1
        L = [f"vmov\t{a[1]}, s{hi}", f"mov\t{a[1]}, {a[1]}, lsl #1"]
        L += ([f"mov\t{a[1]}, {a[1]}, asr #21", f"cmp\t{a[1]}, #-1"]
              if name == "IsNaNorINF64" else [f"movs\t{a[1]}, {a[1]}, lsr #21"])
    elif name in ("IsNaNorINF32", "IsZeroOrSubNormal32"):
        L = [f"vmov\t{a[1]}, {a[0]}", f"mov\t{a[1]}, {a[1]}, lsl #1"]
        L += ([f"mov\t{a[1]}, {a[1]}, asr #24", f"cmp\t{a[1]}, #-1"]
              if name == "IsNaNorINF32" else [f"movs\t{a[1]}, {a[1]}, lsr #24"])
    elif name in ("VFused64", "VFused32") and not FLAGS["NoARMVv4"]:
        t = "f64" if name == "VFused64" else "f32"
        L = [f"vfma.{t}\t{a[0]}, {a[1]}, {a[2]}"]
    elif name in ("VFused64", "VFused32"):
        d = name == "VFused64"
        r, size, fn = ("d", 8, "fused64_muladd") if d else ("s", 4, "fused32_muladd")
        add, x, y = dnum(a[0]), dnum(a[1]), dnum(a[2])
        maxreg = max(7 if d else 15, x, y, add)
        L = [f"vpush\t{{{r}0-{r}{maxreg}}}"]
        if x != 0:
            L.append(f"vldr\t{r}0, [sp, #{x * size}]")
        if y != 1:
            L.append(f"vldr\t{r}1, [sp, #{y * size}]")
        if add != 2:
            L.append(f"vldr\t{r}2, [sp, #{add * size}]")
        L += ["push\t{a1-a4, ip, lr}", f"bl\t{fn}", "pop\t{a1-a4, ip, lr}"]
        if add != 0:
            L += [f"vstr\t{r}0, [sp, #{add * size}]", f"vpop\t{{{r}0-{r}{maxreg}}}"]
        else:
            L += [f"add\tsp, sp, #{size}", f"vpop\t{{{r}1-{r}{maxreg}}}"]
    else:
        return None
    return [ind + "\t" + x + (f"\t@ {name} {args.strip()}" if i == 0 else "")
            for i, x in enumerate(L)]


def cond_value(c):
    c = c.strip()
    neg = False
    if c.startswith(":LNOT:"):
        neg, c = True, c[6:].strip()
    if c not in FLAGS:
        raise ValueError(f"unknown condition {c}")
    return FLAGS[c] != neg


def convert(name, src):
    lines = open(src, encoding="latin-1").read().split("\n")
    out, exports, labels, maps = [], set(), set(), {}
    # Pass 1: exports and labels.
    for raw in lines:
        code, _ = split_comment(raw)
        m = re.match(r"\s+EXPORT\s+(\w+)", code)
        if m:
            exports.add(m.group(1))
        m = re.match(r"([A-Za-z_]\w*)", code)
        if m and not raw[:1].isspace():
            labels.add(m.group(1))
    local = labels - exports - set(FLAGS) - {"maxreg", "hiword", "sreg"}
    for raw in lines:   # register aliases aren't labels
        m = re.match(r"(\w+)\s+(DN|RN|SN)\b", raw)
        if m:
            local.discard(m.group(1))

    stack = []          # conditional assembly: (active, taken)
    aliases = {}        # register aliases (DN, RN), as they stand
    asserts = []        # checked at the end (they can name later labels)
    map_base = None
    header = True
    for raw in lines:
        code, comment = split_comment(raw)
        if header and (code.strip() == "" and comment is not None):
            out.append("@" + comment)
            continue
        header = False
        stripped = code.strip()
        cmt = f"\t@{comment}" if comment is not None and comment.strip() else ""
        # conditional assembly
        m = re.match(r"\[\s*(.+)", stripped)
        if m and not raw[:1].isalpha():
            parent = all(s[0] for s in stack)
            v = cond_value(m.group(1)) if parent else False
            stack.append([v, v])
            continue
        if stripped == "|":
            s = stack[-1]
            s[0] = not s[1]
            s[1] = True
            continue
        m = re.match(r"ELIF\s+(.+)", stripped)
        if m:
            s = stack[-1]
            v = (not s[1]) and cond_value(m.group(1))
            s[0] = v
            s[1] = s[1] or v
            continue
        if stripped == "]":
            stack.pop()
            continue
        if not all(s[0] for s in stack):
            continue
        if not stripped:
            out.append(cmt.strip() if cmt else "")
            continue
        if re.match(r"(GET|GBLL|GBLA|LCLA|LCLS|IMPORT|END)\b", stripped) \
                or re.match(r"\w+\s+SET[LAS]\b", stripped):
            continue
        m = re.match(r"EXPORT\s+(\w+)", stripped)
        if m:
            # (No .global: all of it is one assembly unit, and a global
            # symbol would leave a relocation for every call.)
            out.append(f"@ EXPORT {m.group(1)}")
            continue
        if re.match(r"AREA\b", stripped):
            out.append("\t.text")
            continue
        if stripped in ("ARM", "CODE32"):
            out.append("\t.arm")
            continue
        if stripped == "LTORG":
            out.append("\t.ltorg")
            continue
        if stripped.startswith("ALIGN"):
            out.append("\t.balign\t" + (stripped.split()[1] if len(stripped.split()) > 1 else "4"))
            continue
        m = re.match(r"ASSERT\s+(.+)", stripped)
        if m:
            e = expr(m.group(1)).replace("<>", "!=")
            e = re.sub(r"(?<![<>=!])=(?!=)", "==", e)
            if re.search(r"\b(v\d|a\d|ip|yisint)\b", e):
                continue        # about register aliases: checked by hand
            if "==" in e and e.count("==") == 1:
                l, r = e.split("==")
                e = f"(({r}) - ({l})) == 0"     # label differences: absolute
            asserts.append(f"\t.if !({e})\n\t.error \"ASSERT failed: {m.group(1)}\"\n\t.endif")
            continue
        # register aliases: "name DN n", "name RN reg"
        m = re.match(r"(\w+)\s+(DN|RN|SN)\s+(\w+)", stripped)
        if m and not raw[:1].isspace():
            kind, v = m.group(2), m.group(3)
            if v.isdigit():
                v = {"DN": "d", "SN": "s", "RN": "r"}[kind] + v
            aliases[m.group(1)] = aliases.get(v, v)
            continue
        # storage map: "^ 0, sp" then "name # size"
        m = re.match(r"\^\s*([^,]+),\s*(\w+)", stripped)
        if m:
            map_base = [int(m.group(1)), m.group(2)]
            continue
        m = re.match(r"(\w+)\s+#\s*(.+)", stripped)
        if m and not raw[:1].isspace() and map_base is not None:
            maps[m.group(1)] = (map_base[1], map_base[0])
            map_base[0] += int(m.group(2))
            continue
        m = re.match(r"(\w+)\s+\*\s+:INDEX:@", stripped)
        if m:
            out.append(f"\t.equ\t{m.group(1)}, {map_base[0]}")
            continue
        # EQU
        m = re.match(r"(\w+)\s+\*\s+(.+)", stripped)
        if m and not raw[:1].isspace():
            out.append(f"\t.equ\t{m.group(1)}, {expr(m.group(2))}{cmt}")
            continue

        # label?
        label, rest = "", code
        if not raw[:1].isspace():
            m = re.match(r"(\w+)(.*)", code)
            label, rest = m.group(1), m.group(2)
            rest = re.sub(r"^\s*ROUT\b", "", rest)
            if label.isdigit():
                label = str(int(label))
        lab = f"{label}:" if label else ""
        rest_s = rest.strip()
        if not rest_s:
            out.append(lab + cmt)
            continue
        for k, v in aliases.items():
            rest_s = re.sub(rf"(?<![\w.]){re.escape(k)}(?!\w)", v, rest_s)
        # macros
        m = re.match(r"(\w+)\s*(.*)", rest_s)
        mn, margs = m.group(1), m.group(2)
        exp = expand_macro(mn, margs, "")
        if exp is not None:
            if lab:
                out.append(lab)
            out += exp
            continue
        m = re.match(r"(Push|Pull)\s+\"([^\"]*)\"\s*(?:,\s*(\w+))?\s*$", rest_s)
        if m:
            # Push/Pull "regs"[, condition]
            op = "push" if m.group(1) == "Push" else "pop"
            c = (m.group(3) or "").lower()
            out.append(f"{lab}\t{op}{c}\t{{{m.group(2)}}}{cmt}")
            continue
        if re.match(r"(Push|Pull)\b", rest_s):
            raise ValueError(f"{name}: can't translate {rest_s}")
        m = re.match(r"DC([DWB])\s+(.*)", rest_s)
        if m:
            d = {"D": ".word", "W": ".hword", "B": ".byte"}[m.group(1)]
            out.append(f"{lab}\t{d}\t{expr(m.group(2))}{cmt}")
            continue
        m = re.match(r"DCI\s+(.*)", rest_s)
        if m:
            out.append(f"{lab}\t.inst\t{expr(m.group(1))}{cmt}")
            continue
        # an instruction
        ins = expr(rest_s)
        for k, v in aliases.items():
            ins = re.sub(rf"(?<![\w.]){re.escape(k)}(?!\w)", v, ins)
        ins = re.sub(r"^(VPUSH|VPOP)(\w*)\s+([^{\s].*)$", r"\1\2\t{\3}", ins)
        ins = re.sub(r"%[FB]?([FB])T?(\d+)",
                     lambda mm: f"{int(mm.group(2))}{mm.group(1).lower()}", ins)
        # storage-map operands
        mm = re.match(r"(ADR\w*)\s+(\w+),\s*(\w+)\s*$", ins)
        if mm and mm.group(3) in maps:
            reg, off = maps[mm.group(3)]
            ins = f"add\t{mm.group(2)}, {reg}, #{off}"
        else:
            mm = re.match(r"(V?(?:LDR|STR)\w*)\s+(\w+),\s*(\w+)\s*$", ins)
            if mm and mm.group(3) in maps:
                reg, off = maps[mm.group(3)]
                ins = f"{mm.group(1)}\t{mm.group(2)}, [{reg}, #{off}]"
        out.append(f"{lab}\t{ins}{cmt}")

    out.append("\t.ltorg\t\t\t\t@ objasm's literal pool at the end of the AREA")
    out += asserts
    text = "\n".join(out)
    # File prefix for this file's own labels (not in comments).
    names = {l: f"{VARIANT}_{name}_{l}" for l in local}
    names.update({l: f"{VARIANT}_{l}" for l in ALL_EXPORTS})
    pat = re.compile(r"(?<![\w.])(" + "|".join(re.escape(k) for k in
                     sorted(names, key=len, reverse=True)) + r")(?!\w)")
    def prefix_line(line):
        code, sep, c = line.partition("@")
        return pat.sub(lambda m: names[m.group(1)], code) + sep + c
    text = "\n".join(prefix_line(l) for l in text.split("\n"))
    return text, exports


ALL_EXPORTS = set()
VARIANT = ""


def main():
    global VARIANT
    src, dst, VARIANT = sys.argv[1], sys.argv[2], sys.argv[3]
    FLAGS["NoARMVv4"] = VARIANT == "v3"
    for f in FILES:
        for raw in open(os.path.join(src, f), encoding="latin-1"):
            m = re.match(r"\s+EXPORT\s+(\w+)", raw)
            if m:
                ALL_EXPORTS.add(m.group(1))
    ALL_EXPORTS.add("RaiseException")
    os.makedirs(dst, exist_ok=True)
    for f in FILES:
        text, exports = convert(f, os.path.join(src, f))
        with open(os.path.join(dst, f + ".s"), "w") as o:
            o.write(f"@ {f}: from RISC OS Open's VFPSupport s/{f}, translated to GNU as\n"
                    f"@ by objasm2gas.py (riscos-linux-testbed), {VARIANT} variant\n"
                    f"@ ({'no VFPv4: FMA in software' if VARIANT == 'v3' else 'VFPv4: VFMA'}). "
                    f"The original's\n@ copyright and licence follow; they apply to this file.\n")
            o.write(text + "\n")
        print(f, "exports", " ".join(sorted(exports)))


if __name__ == "__main__":
    main()
