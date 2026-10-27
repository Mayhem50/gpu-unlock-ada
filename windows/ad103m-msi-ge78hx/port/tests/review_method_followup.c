/* SPDX-License-Identifier: GPL-2.0-only */
/* Review only: zero exit reproduces gaps, not successful qualification.
 * Separate from the normal passing suite; no hardware code is linked. */
#define main baseline_test_main
#include "host_tests.c"
#undef main

static bool ExtraResetDone;
static bool duplicate_flr_read(void *v,unsigned offset,unsigned width,uint32_t *value) {
    Mock *m=v;
    if(m->flr_written&&!ExtraResetDone&&offset==0) {
        ExtraResetDone=true;
        CHECK(write_config(v,MSI_PCIE+8,2,0x8000));
        stall(v,100000); /* Respect quiet time for the additional modeled reset. */
    }
    return read_config(v,offset,width,value);
}
static bool dirty_stock_close(void *v) {
    bool result=close_log(v);((Mock *)v)->life.live_dma=1;return result;
}
int main(void) {
    FILE *f=fopen("build/prom.bin","rb");CHECK(f!=NULL);
    Prom=malloc(1048576);CHECK(Prom);
    CHECK(fread(Prom,1,1048576,f)==1048576&&fgetc(f)==EOF);fclose(f);

    setup(OK);ExtraResetDone=false;MsiOps o=ops();o.read_config=duplicate_flr_read;
    execute_regression(&o);
    CHECK(M.life.resets==2&&S.flrs==1&&!M.windows&&!M.freed&&S.dma_retained&&S.terminal_required);
    puts("REPRODUCED: raw backend accepts second FLR; lifecycle detects it after both effects");

    setup(OK);o=ops();o.close_log=dirty_stock_close;
    if(!setjmp(Transfer))msi_run(&o,&S,NULL,NULL,0,&SyntheticApproval,false);
    CHECK(M.windows==1&&M.life.live_dma==1&&!M.transactions&&!M.stops);
    puts("REPRODUCED: stock handoff accepts a lifecycle dirtied by log close");
    CHECK(!M.config_reads&&!M.config_writes); /* All effects are virtual bookkeeping. */
    free(Prom);
    puts("Review outcomes reproduced on CPU; no GPU/EFI/unlock was executed.");
    return 0;
}
