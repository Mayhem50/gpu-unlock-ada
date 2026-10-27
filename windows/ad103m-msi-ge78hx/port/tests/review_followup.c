/* SPDX-License-Identifier: GPL-2.0-only */
/* Review probes for c26b1c3. Separate from the normal validation suite:
 * zero exit means unsafe outcomes were reproduced, not qualification. */
#define main baseline_test_main
#include "host_tests.c"
#undef main

static bool FreedWithMaster;
static bool master_after_reset_snapshot(void *v,MsiGraph *out) {
    Mock *m=v;bool result=snapshot(v,out);
    if(m->flr_written&&m->restored_bars==6&&!m->freed)m->config[1]|=4;
    return result;
}
static bool record_dma_release(void *v) {
    FreedWithMaster=(((Mock *)v)->config[1]&4)!=0;
    return release_dma(v);
}
static bool master_during_final_mmio(void *v,uint32_t offset,uint32_t *value) {
    bool result=read_mmio(v,offset,value);Mock *m=v;
    if(m->log_closed)m->config[1]|=4;
    return result;
}
static bool partial_silence(void *v) {
    Mock *m=v;m->silenced=true;m->graph.console_alive=false;
    return false;
}
int main(void) {
    FILE *f=fopen("build/prom.bin","rb");CHECK(f!=NULL);
    Prom=malloc(1048576);CHECK(Prom);
    CHECK(fread(Prom,1,1048576,f)==1048576&&fgetc(f)==EOF);fclose(f);

    setup(OK);MsiOps o=ops();o.snapshot=master_after_reset_snapshot;o.release_dma=record_dma_release;
    execute_regression(&o);
    CHECK(FreedWithMaster&&M.freed==1&&!S.dma_retained&&!M.windows&&S.terminal_required);
    puts("REPRODUCED: DMA freed while BME is enabled after post-reset graph callback");

    setup(OK);o=ops();o.read_mmio=master_during_final_mmio;
    execute_regression(&o);
    CHECK(M.windows==1&&S.state==MSI_WINDOWS_INVOKED&&(M.config[1]&4));
    puts("REPRODUCED: Windows handoff invoked with BME enabled during final MMIO checks");

    setup(OK);o=ops();o.silence_console=partial_silence;
    execute_regression(&o);
    CHECK(S.state==MSI_REFUSED&&!S.terminal_required&&!S.mutated&&!M.graph.console_alive);
    CHECK(!M.stops&&!M.transactions&&!M.windows);
    puts("REPRODUCED: partial console failure returns REFUSED without terminal or rollback state");
    free(Prom);
    puts("Review probes reproduced unsafe fault-model outcomes; no hardware was accessed.");
    return 0;
}
