/* Global/HALDevice.h: the HAL device structure (Kernel hdr/HALDevice) and
   the audio type numbers. Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef SC_HALDEVICE_H
#define SC_HALDEVICE_H
#include <stdbool.h>
#include "stdint.h"
#define HALDeviceType_Audio   (2 << 8)
#define HALDeviceAudio_AudC   1
#define HALDeviceAudio_Mixer  2
struct device
{
  uint16_t type;
  uint16_t id;
  uint32_t location;
  uint32_t version;
  const char *description;
  void *address;
  uint32_t reserved1[3];
  bool (*Activate) (struct device *);
  void (*Deactivate) (struct device *);
  void (*Reset) (struct device *);
  int32_t (*Sleep) (struct device *, int32_t);
  int32_t devicenumber;
  bool (*TestIRQ) (struct device *);
  void (*ClearIRQ) (struct device *);
  uint32_t reserved2;
};
#endif
