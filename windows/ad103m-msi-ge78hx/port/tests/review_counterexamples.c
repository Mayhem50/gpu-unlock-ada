/* SPDX-License-Identifier: GPL-2.0-only */
/* Audit only: these assertions reproduce known defects, not safe behavior.
 * Kept outside Build-Offline.ps1. Replace with rejection regressions when fixed. */
#define main baseline_test_main
#include "host_tests.c"
#undef main

static bool drift_transaction(void *v,MsiProof *proof) {
    bool result=transaction(v,proof);
    ((Mock *)v)->config[(MSI_PCIE+8)/4]^=0x20; /* Device Control MPS bit */
    return result;
}
static bool reenter_stage(void *v) {
    MsiOps nested=ops();
    (void)v;
    CHECK(S.state==MSI_NEW);
    nested.transaction=NULL;
    msi_run(&nested,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(S.state==MSI_REFUSED); /* Reentry reached and changed the same session. */
    return false;
}
static bool late_master_on_close(void *v) {
    bool result=close_log(v);
    ((Mock *)v)->config[1]|=4;
    return result;
}
int main(void) {
    FILE *f=fopen("build/prom.bin","rb");CHECK(f!=NULL);
    Prom=malloc(1048576);CHECK(Prom);
    CHECK(fread(Prom,1,1048576,f)==1048576&&fgetc(f)==EOF);fclose(f);

    setup(OK);
    M.observation.config[MSI_PCIE/4]^=1; /* PCIe capability ID 0x10 -> 0x11 */
    CHECK(msi_profile(&M.observation,Prom,1048576));
    puts("REPRODUCED: wrong PCIe capability ID accepted by profile");

    setup(OK);MsiOps o=ops();o.transaction=drift_transaction;
    if(!setjmp(Transfer))msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(M.windows==1&&S.state==MSI_WINDOWS_INVOKED);
    CHECK((M.config[(MSI_PCIE+8)/4]^CaptureConfig0[(MSI_PCIE+8)/4])&0x20);
    puts("REPRODUCED: transaction-time PCIe control drift restored and passed to Windows");

    setup(OK);o=ops();o.stage_windows=reenter_stage;
    msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(S.state==MSI_REFUSED&&!M.stops&&!M.transactions);
    puts("REPRODUCED: staging callback can reenter and change active session");

    setup(TX_FAIL);o=ops();o.close_log=late_master_on_close;
    msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(S.state==MSI_STOPPED&&S.dma_retained&&S.containment_verified&&(M.config[1]&4));
    puts("REPRODUCED: failure-log callback invalidates BME containment without detection");
    free(Prom);
    puts("Audit counterexamples confirmed; this is NOT a hardware qualification pass.");
    return 0;
}
