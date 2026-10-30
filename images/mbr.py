#!/usr/bin/env python3
"""mbr.py IMG DISKID BOOT_MB ROOT_MB [START_MB]: an MBR on IMG: partition 1
FAT32 (LBA, bootable) from START_MB MiB (default 1), partition 2 Linux after it."""
import struct, sys
img, diskid, boot_mb, root_mb = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3]), int(sys.argv[4])
start = int(sys.argv[5]) if len(sys.argv) > 5 else 1
S = 2048  # sectors per MiB
def entry(boot, ptype, start, count):
    # CHS fields set to the "beyond 1024 cylinders" value; LBA is what's used
    return struct.pack('<B3sB3sII', 0x80 if boot else 0, b'\xfe\xff\xff', ptype, b'\xfe\xff\xff', start, count)
mbr = bytearray(512)
struct.pack_into('<I', mbr, 440, diskid)
mbr[446:462] = entry(True, 0x0c, start * S, boot_mb * S)
mbr[462:478] = entry(False, 0x83, (start + boot_mb) * S, root_mb * S)
mbr[510:512] = b'\x55\xaa'
with open(img, 'r+b') as f:
    f.write(mbr)
