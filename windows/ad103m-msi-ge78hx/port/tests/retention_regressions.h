/* SPDX-License-Identifier: GPL-2.0-only */
/* F5: former combined-fault review probes now require accurate retention. */
static unsigned CombinedFault;
static bool ExtraUnreadable,InjectResources;
static void combined_stock_commit(void *v,const MsiGuard *g,bool *retained) {
    Mock *m=v;MsiGuard token=*g;
    if(InjectResources)dirty_stock(m);
    if(ExtraUnreadable)m->lifecycle_unreadable=true;
    switch(CombinedFault) {
        case 0:m->guard_broken=true;break;
        case 1:m->guard_epoch++;break;
        case 2:m->guard_active=false;break;
        case 3:token.purpose=99;break;
        case 4:g=NULL;break;
        case 5:m->log_closed=false;break;
        case 6:S.state=MSI_STAGED;break;
        case 7:m->transfer_commit_spent=true;break;
        case 8:m->guard_purpose=99;break;
        case 9:m->guard_purpose=MSI_GUARD_FINAL;token.purpose=MSI_GUARD_FINAL;break;
        default:CHECK(false);
    }
    if(g)g=&token;
    commit_windows(v,g,retained);
}
static void require_combined_retention(void) {
    CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.guard_retained&&S.dma_retained);
    CHECK(!M.windows&&!M.freed&&!M.config_reads&&!M.config_writes&&!M.mmio_reads);
    CHECK(!M.transactions&&!M.stops&&!S.mutated&&!S.containment_verified);
    CHECK(M.closes==1&&M.guard_active==(CombinedFault!=2));
}
static void retention_regressions(void) {
    MsiLifecycle empty={0};bool retained=false;
    CHECK(msi_stock_retention(&retained,&empty,true)&&!retained);
    MsiLifecycle live=empty;live.live_dma=1;
    CHECK(!msi_stock_retention(&retained,&live,true)&&retained);
    CHECK(!msi_stock_retention(&retained,&empty,true)&&retained); /* Sticky after repair. */
    retained=false;CHECK(!msi_stock_retention(&retained,&empty,false)&&retained);
    retained=false;CHECK(!msi_stock_retention(&retained,NULL,true)&&retained);
    CHECK(!msi_stock_retention(NULL,&empty,true));

    /* Original three reproductions: broken/changed lease + live DMA, then
     * broken lease + both live DMA and unreadable bookkeeping. */
    InjectResources=true;StockField=5;
    for(unsigned n=0;n<3;n++) {
        setup(OK);CombinedFault=n==1?1:0;ExtraUnreadable=n==2;
        MsiOps o=ops();o.commit_windows=combined_stock_commit;run_stock_case(&o);
        require_combined_retention();CHECK(M.life.live_dma==1);
    }
    ExtraUnreadable=false;
    /* Every scoped uncertainty combined with every early-return category. */
    for(StockField=0;StockField<10;StockField++)for(CombinedFault=0;CombinedFault<10;CombinedFault++) {
        setup(OK);MsiOps o=ops();o.commit_windows=combined_stock_commit;
        run_stock_case(&o);require_combined_retention();
        unsigned stages=M.stages,closes=M.closes;
        msi_run(&o,&S,NULL,NULL,0,&SyntheticApproval,false);
        CHECK(!M.windows&&M.stages==stages&&M.closes==closes&&S.dma_retained);

        /* Journal repair plus attempted commit replay must not clear evidence. */
        memset(&M.life,0,sizeof(M.life));M.lifecycle_unreadable=false;M.reset_gate=(MsiResetGate){0};
        M.guard_active=true;M.guard_broken=false;M.guard_purpose=MSI_GUARD_STOCK;
        M.log_closed=true;S.state=MSI_WINDOWS_INVOKED;
        MsiGuard g={M.guard_epoch,MSI_GUARD_STOCK};
        commit_windows(&M,&g,&S.dma_retained);
        CHECK(!M.windows&&!M.freed&&S.dma_retained);
    }
    /* A clean inventory plus a lease/log failure need not invent live DMA. */
    InjectResources=false;
    const unsigned clean_faults[]={0,1,5};
    for(unsigned n=0;n<3;n++) {
        setup(OK);CombinedFault=clean_faults[n];MsiOps o=ops();o.commit_windows=combined_stock_commit;
        run_stock_case(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.guard_retained&&!S.dma_retained);
        CHECK(!M.windows&&!M.freed&&!M.config_reads&&!M.config_writes&&!M.mmio_reads);
    }
    puts("PASS: F5 combined lease/resource faults preserve retention on every stock exit.");
}
