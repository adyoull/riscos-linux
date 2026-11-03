#!/usr/bin/env python3
"""Check the working tree against a revision of the Linux port for the usual
merge hazards:

  * assembler files whose [ / ] / IF / ENDIF nesting changed
  * conflict markers left behind
  * lines mentioning Linux, PVOps, KSWI, PagedMemory, ... that went missing
  * new raw MSR/MRS on CPSR/SPSR (the Linux port runs RISC OS in user mode:
    use SEI/CLI/WritePSRc/SavePSR or pvMSR/pvMRS instead) - fine only in
    code that is assembled out for Linux (:LNOT: Linux / PagedMemory)
  * new SWI instructions in the Kernel (the port uses KSWI in places)

    python3 nightly/check.py [REV]     (default HEAD)
"""
# Copyright (c) 2026 Andrew Youll. Apache License 2.0.
import collections, re, subprocess, sys

rev = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
git = lambda *a: subprocess.run(['git', *a], capture_output=True).stdout
files = git('diff', '--name-only', rev, '--', 'RiscOS').decode().split()
files += git('ls-files', '--others', '--exclude-standard', 'RiscOS').decode().split()

linuxy = re.compile(r'Linux|PVSys|PVirt|pvM[RS][RS]|IXFS|IXSupport|SocketKVM|MOVS_PC_LR|'
                    r'pvSWI|fvSWI|KSWI|LinuxSyscall|PagedMemory|\bix_', re.I)
raw = re.compile(r'^\s*\w*\s+(MSR|MRS)\w*\s+.*\b(CPSR|SPSR)', re.I)
swi = re.compile(r'^\s*\w*\s+SWI(\w\w)?\s+X?OS_', re.I)
asm = re.compile(r'(/s/|/hdr/|/Hdr/|,ffb$|\.s$)')

def depth(lines):
    d = 0
    for l in lines:
        m = re.match(r'^\s*(\[|\]|IF\b|ENDIF\b)(\s|$)', l.split(';')[0])
        if m:
            d += 1 if m.group(1) in ('[', 'IF') else -1
    return d

problems = 0
def report(f, what):
    global problems
    problems += 1
    print(f'{f}: {what}')

for f in files:
    try:
        cur = open(f, encoding='latin-1').read().split('\n')
    except (FileNotFoundError, IsADirectoryError):
        continue
    old = git('show', f'{rev}:{f}').decode('latin-1').split('\n')
    if any(l.startswith(('<<<<<<< ', '>>>>>>> ')) for l in cur) and \
       not any(l.startswith(('<<<<<<< ', '>>>>>>> ')) for l in old):
        report(f, 'conflict markers')
    if asm.search(f):
        if old != [''] and depth(old) != depth(cur):
            report(f, f'conditional nesting changed ({depth(old)} -> {depth(cur)})')
        oc = collections.Counter(l.split(';')[0].strip() for l in old)
        for n, l in enumerate(cur, 1):
            c = l.split(';')[0]
            k = c.strip()
            if raw.match(c) or ('/Kernel/' in f and swi.match(c)):
                if oc[k] > 0:
                    oc[k] -= 1
                else:
                    report(f'{f}:{n}', 'new ' + ('raw MSR/MRS' if raw.match(c) else 'SWI (KSWI?)') + f': {k}')
    cc = collections.Counter(cur)
    for l, n in collections.Counter(l for l in old if linuxy.search(l)).items():
        if cc[l] < n:
            report(f, f'lost port line: {l.strip()[:100]}')

print(f'{problems} thing(s) to look at' if problems else 'Nothing found.')
sys.exit(1 if problems else 0)
