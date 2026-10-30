/* rolsys.h: Linux system calls from the LinuxCodec family of modules
   (LinuxCodec, and the VCHIQ, BCMSupport and PCI stand-ins), through RISC
   OS for Linux's IXSupport_LinuxSyscall SWI.
   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#ifndef ROLSYS_H
#define ROLSYS_H

#include "cmodule.h"

#define IXSupport_LinuxSyscall	0x59EC0

/* Linux (32-bit ARM EABI) */
#define NR_open		5
#define NR_close	6
#define NR_ioctl	54
#define O_RDWR		2
#define O_CLOEXEC	02000000
#define EINTR		4
#define EAGAIN		11
#define ENOSYS		38

/* r0 = the result, or -errno (-ENOSYS without IXSupport) */
static inline int
lsys (int nr, unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f)
{
  regs r = {{ a, b, c, d, e, f, 0, (unsigned) nr }};
  if (swix_regs (IXSupport_LinuxSyscall, &r))
    return -ENOSYS;
  return (int) r.r[0];
}

static inline int
lioctl (int fd, unsigned req, void *arg)
{
  return lsys (NR_ioctl, (unsigned) fd, req, (unsigned) arg, 0, 0, 0);
}

static inline int
lopen (const char *path)
{
  return lsys (NR_open, (unsigned) path, O_RDWR | O_CLOEXEC, 0, 0, 0, 0);
}

static inline void
lclose (int fd)
{
  lsys (NR_close, (unsigned) fd, 0, 0, 0, 0, 0);
}

/* Linux's device ioctls are only passed through when RISC OS runs natively
   (not under QEMU), which is when NativeSupport is loaded (it refuses to
   start under QEMU). */
static inline int
rol_native (void)
{
  regs r = {{ 18, (unsigned) "NativeSupport" }};
  return swix_regs (OS_Module, &r) == 0;
}

#define IOC(dir, type, nr, size)	(((unsigned) (dir) << 30) | ((unsigned) (size) << 16) | ((unsigned) (type) << 8) | (unsigned) (nr))

#endif
