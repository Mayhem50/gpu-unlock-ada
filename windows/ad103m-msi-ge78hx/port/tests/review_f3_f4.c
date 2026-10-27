/* SPDX-License-Identifier: GPL-2.0-only */
/* Review of db881c1: zero exit reproduces a retention-reporting gap.
 * Excluded from the normal qualification suite; CPU/RAM only. */
#define main baseline_test_main
#include "host_tests.c"
#undef main
static unsigned CombinedFault;
static void invalid_lease_dirty_stock(void *v,const MsiGuard *g,bool *retained) {
    Mock *m=v;m->life.live_dma=1;
    if(CombinedFault==0)m->guard_broken=true;
    if(CombinedFault==1)m->guard_epoch++;
    if(CombinedFault==2){m->guard_broken=true;m->lifecycle_unreadable=true;}
    commit_windows(v,g,retained);
}
int main(void) {
    for(CombinedFault=0;CombinedFault<3;CombinedFault++) {
        setup(OK);MsiOps o=ops();o.commit_windows=invalid_lease_dirty_stock;
        run_stock_case(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.guard_retained&&M.guard_active);
        CHECK(!M.windows&&!M.freed&&M.life.live_dma==1&&!S.dma_retained);
        CHECK(!M.config_reads&&!M.config_writes&&!M.mmio_reads&&!M.transactions);
    }
    puts("REPRODUCED: invalid stock lease skips dirty/unreadable lifecycle classification");
    puts("Handoff is refused and terminal guard retained, but dma_retained is incorrectly false.");
    puts("No free, GPU access, EFI or unlock; this is a CPU fault-model observation.");
    return 0;
}
