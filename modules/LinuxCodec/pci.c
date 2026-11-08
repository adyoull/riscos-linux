/* PCI: a stand-in for RISC OS's PCI module, with only its memory SWIs, for
   RISC OS for Linux running natively. There's no PCI bus to drive here
   (Linux has it); programs written for the Raspberry Pi's VideoCore
   (ReelHWAccel's vcdec and vcenc) use PCI_RAMAlloc for buffers the
   VideoCore copies data in and out of, and under Linux any memory will do
   for that: Linux's VCHIQ driver works with the program's own addresses.

   SWIs (chunk &50380, as RISC OS's PCI module):
     2  ReturnNumber   -> r0 = 0 (no PCI functions)
     16 RAMAlloc       r0 = size, r1 = alignment (a power of 2, or 0),
                       r2 = boundary (ignored) -> r0 -> the memory,
                       r1 = its "physical" address: the same as r0 (Linux
                       doesn't say; nothing here needs it)
     17 RAMFree        r0 -> memory from RAMAlloc
     18 LogicalAddress (not provided)
   The memory comes from the RMA (cacheable, unlike the real thing's).
   The others give an error: there's no PCI bus.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#include "rolsys.h"

#define BLOCK_MAGIC	0x4D415250u	/* "PRAM" */

ERROR_BLOCK (no_memory, 0x823630, "PCI_RAMAlloc: not enough memory");
ERROR_BLOCK (bad_block, 0x823631, "PCI_RAMFree: not memory from PCI_RAMAlloc");
ERROR_BLOCK (no_bus, 0x823632, "There's no PCI bus under RISC OS for Linux (only PCI_RAMAlloc and PCI_RAMFree are provided)");
ERROR_BLOCK (bad_swi, 0x1E6, "Unknown PCI SWI");

static unsigned blocks, bytes;

static oserror *
ram_alloc (regs *r)
{
  unsigned size = r->r[0], align = r->r[1];
  if (align < 16)
    align = 16;
  if (align & (align - 1) || align > 1024 * 1024 || !size || size > 256u * 1024 * 1024)
    return ERR (no_memory);
  unsigned char *p = rma_claim (size + align + 8);
  if (!p)
    return ERR (no_memory);
  unsigned a = ((unsigned) p + 8 + align - 1) & ~(align - 1);
  ((unsigned *) a)[-1] = BLOCK_MAGIC;
  ((unsigned *) a)[-2] = (unsigned) p;
  blocks++;
  bytes += size;
  r->r[0] = a;
  r->r[1] = a;
  return 0;
}

static oserror *
ram_free (regs *r)
{
  unsigned a = r->r[0];
  if (!a)
    return 0;
  if ((a & 15) || ((unsigned *) a)[-1] != BLOCK_MAGIC)
    return ERR (bad_block);
  ((unsigned *) a)[-1] = 0;
  rma_free ((void *) ((unsigned *) a)[-2]);
  if (blocks)
    blocks--;
  return 0;
}

oserror *
mod_swi (unsigned number, regs *r)
{
  switch (number)
    {
    case 2: r->r[0] = 0; return 0;
    case 16: return ram_alloc (r);
    case 17: return ram_free (r);
    }
  return number < 19 ? ERR (no_bus) : ERR (bad_swi);
}

oserror *
mod_init (const char *tail, int instance, void *pw)
{
  (void) tail; (void) instance; (void) pw;
  blocks = bytes = 0;
  return 0;
}

oserror *
mod_final (int fatal)
{
  (void) fatal;
  return 0;
}

void
mod_service (regs *r)
{
  (void) r;
}
