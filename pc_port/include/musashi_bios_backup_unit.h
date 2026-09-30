#ifndef MUSASHI_BIOS_BACKUP_UNIT_H
#define MUSASHI_BIOS_BACKUP_UNIT_H

#include "musashi_bios_card.h"

typedef enum MusashiBiosBackupUnitStage {
    MUSASHI_BU_IDLE,
    MUSASHI_BU_WAIT_PORT0,
    MUSASHI_BU_WAIT_PORT1,
    MUSASHI_BU_DONE,
    MUSASHI_BU_FAULT
} MusashiBiosBackupUnitStage;

/* Native source-owned BIOS filesystem state. No host address or BIOS code
 * is installed in guest RAM. The two slots must be explicitly disconnected. */
typedef struct MusashiBiosBackupUnit {
    MusashiBiosKernel *kernel;
    MusashiBiosCard *card;
    MusashiBiosCardCompletion completion;
    uint32_t status[5];
    uint32_t operation[2];
    uint32_t auto_format;
    uint8_t directory[2][15][32];
    uint32_t broken[2][20];
    uint8_t buffer[2][128];
    MusashiBiosBackupUnitStage stage;
    int busy;
} MusashiBiosBackupUnit;

/* Zeroed fresh storage, before guest InitCARD is allowed. */
int musashi_bios_backup_unit_init(MusashiBiosBackupUnit *,
                                  MusashiBiosKernel *, MusashiBiosCard *);
int musashi_bios_backup_unit_begin(MusashiBiosBackupUnit *);
/* Accepted pending sets complete0 and preserves guest_result. Only both real
 * card timeout completions yield complete1/result0. This never pumps IRQs or
 * invents a timeout: the owner must supply actual VBlank safe points. */
int musashi_bios_backup_unit_poll(MusashiBiosBackupUnit *, int *complete,
                                  int32_t *guest_result);
/* After scheduler unhook; before card shutdown when idle, or after coherent
 * card shutdown on a fault retaining a CARD node. No guest completion. */
int musashi_bios_backup_unit_shutdown(MusashiBiosBackupUnit *);

#endif
