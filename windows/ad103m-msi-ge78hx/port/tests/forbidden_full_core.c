/* SPDX-License-Identifier: GPL-2.0-only */
/* Negative compilation fixture: signature of the complete upstream entry,
 * without linking or executing any upstream transaction or hardware code. */
#include "../src/port.h"
struct pci_dev;struct gpu_unlock_610_shared;
static int complete_core(struct pci_dev *p,const struct gpu_unlock_610_shared *s) {
    (void)p;(void)s;return 0;
}
void forbidden_implicit_binding(void) {
    MsiPreResetStep invalid={MSI_PRE_RESET_RETAINED_V1,complete_core};
    (void)invalid;
}
