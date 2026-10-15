#include "cerf_fs_driver.h"
#include "cerf_regs_map.h"

#include <windows.h>

#include "cerf/peripherals/cerf_virt/cerf_virt_addr_map.h"

static volatile CerfFsChannel* g_chan = NULL;

volatile CerfFsChannel* CerfFsMapChannel(void) {
    if (g_chan) return g_chan;
    g_chan = (volatile CerfFsChannel*)CerfMapRegsPage(g_CerfVirtBase + CerfVirt::kFolderShareOffset, 0x1000);
    return g_chan;
}

unsigned long CerfFsCall(CerfFsServerPB* pb, unsigned long code) {
    unsigned long result;
    volatile CerfFsChannel* ch = CerfFsMapChannel();
    if (!ch) return CERF_FS_E_GENERAL;

    ch->ServerPB = (unsigned long)pb;
    ch->Code = code;
    while (ch->IOPending) ch->Code = CERF_FS_OP_POLL;
    result = ch->Result;

    pb->fResult = (unsigned short)result;
    return result;
}
