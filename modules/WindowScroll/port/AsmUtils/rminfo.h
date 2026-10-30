/* AsmUtils/rminfo.h: the module's own base address (CMHG's Image_RO_Base),
   which the Current Active Object is while the module's task runs. */
#ifndef WS_RMINFO_H
#define WS_RMINFO_H
extern char module_start[];
#define Image_RO_Base ((unsigned long) module_start)
#endif
