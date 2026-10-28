/* SPDX-License-Identifier: GPL-2.0-only */
/* Pure numeric addresses; no UEFI imports, allocation or DMA hardware calls. */
static unsigned DmaCases;
static void dma_fixture(MsiDmaLedger *l) {
    memset(l,0,sizeof(*l));CHECK(msi_dma_init(l));DmaCases++;
}
static unsigned dma_resource(MsiDmaLedger *l,bool mapped) {
    unsigned id=0;uint64_t n=l->used;
    CHECK(msi_dma_alloc_begin(l,5000,&id));
    CHECK(msi_dma_alloc_end(l,id,MSI_DMA_OK,UINT64_C(0x100000)+n*0x10000));
    CHECK(l->resource[id-1].pages==2&&l->resource[id-1].capacity==8192);
    if(mapped) {
        CHECK(msi_dma_map_begin(l,id));
        CHECK(msi_dma_map_end(l,id,MSI_DMA_OK,UINT64_C(0x100000000)+n*0x10000,0x900+n,8192));
    }
    return id;
}
static void dma_unit_tests(void) {
    MsiDmaLedger l={0};MsiDmaInventory n;unsigned id,other;
    CHECK(!msi_dma_all_released(&l)&&!msi_dma_handoff_ready(&l));
    CHECK(!msi_dma_init(NULL)&&!msi_dma_inventory(NULL,&n));
    dma_fixture(&l);CHECK(msi_dma_handoff_ready(&l));
    CHECK(!msi_dma_inventory(&l,NULL));
    id=dma_resource(&l,true);other=dma_resource(&l,true);
    CHECK(id==1&&other==2&&msi_dma_inventory(&l,&n)&&n.live==2&&n.mapped==2);
    CHECK(msi_dma_cleanup_begin(&l,true));
    for(unsigned i=1;i<=2;i++) {
        CHECK(msi_dma_unmap_begin(&l,i)&&msi_dma_unmap_end(&l,i,MSI_DMA_OK));
        CHECK(msi_dma_free_begin(&l,i)&&msi_dma_free_end(&l,i,MSI_DMA_OK));
    }
    CHECK(msi_dma_handoff_ready(&l)&&l.used==2&&l.unmaps==2&&l.frees==2);
    CHECK(l.resource[0].host&&l.resource[0].mapping); /* tombstones preserved */
    CHECK(!msi_dma_cleanup_begin(&l,true)&&!msi_dma_handoff_ready(&l));

    /* Valid allocation rollback: no Map ever called, or independently proven
     * failed Map with NO effects. Cleanup needs an explicit synthetic proof. */
    for(unsigned failed_map=0;failed_map<2;failed_map++) {
        dma_fixture(&l);id=dma_resource(&l,false);
        if(failed_map) {
            CHECK(msi_dma_map_begin(&l,id));
            CHECK(!msi_dma_map_end(&l,id,MSI_DMA_NO_EFFECT,0,0,0));
        }
        CHECK(msi_dma_cleanup_begin(&l,true));
        CHECK(msi_dma_free_begin(&l,id)&&msi_dma_free_end(&l,id,MSI_DMA_OK));
        CHECK(msi_dma_all_released(&l)&&!l.unmaps&&l.frees==1);
        CHECK(msi_dma_handoff_ready(&l)==!failed_map); /* fault cannot become success */
    }
    const uint64_t bad_sizes[]={0,UINT64_MAX,(UINT64_C(1)<<49)};
    for(unsigned i=0;i<3;i++) {
        dma_fixture(&l);id=99;CHECK(!msi_dma_alloc_begin(&l,bad_sizes[i],&id)&&!id&&!l.used);
        CHECK(!msi_dma_handoff_ready(&l));
    }
    const uint64_t bad_hosts[]={0,1,UINT64_MAX-4095};
    for(unsigned i=0;i<3;i++) {
        dma_fixture(&l);CHECK(msi_dma_alloc_begin(&l,8192,&id));
        CHECK(!msi_dma_alloc_end(&l,id,MSI_DMA_OK,bad_hosts[i]));
        CHECK(l.resource[0].state==MSI_DMA_UNKNOWN&&!msi_dma_all_released(&l));
        CHECK(!msi_dma_cleanup_begin(&l,true)&&!l.frees);
    }
    dma_fixture(&l);id=dma_resource(&l,false);CHECK(msi_dma_alloc_begin(&l,4096,&other));
    CHECK(!msi_dma_alloc_end(&l,other,MSI_DMA_OK,l.resource[id-1].host+4096));
    CHECK(l.resource[other-1].host==l.resource[id-1].host+4096);

    /* Short, oversized, wrapping, misaligned, missing-token and aliased maps. */
    for(unsigned f=0;f<10;f++) {
        dma_fixture(&l);(void)dma_resource(&l,true);id=dma_resource(&l,false);
        uint64_t dev=UINT64_C(0x100010000),token=0x901,bytes=8192;
        if(f==0)bytes=4096;
        if(f==1)bytes=12288;
        if(f==2)dev=(UINT64_C(1)<<49)-4096;
        if(f==3)dev++;
        if(f==4)token=0;
        if(f==5)token=0x900;
        if(f==6)dev=UINT64_C(0x100001000); /* overlap first mapped allocation */
        if(f==7)dev=UINT64_MAX-4095;
        if(f==8)dev=0;
        if(f==9)bytes=0;
        CHECK(msi_dma_map_begin(&l,id));
        CHECK(!msi_dma_map_end(&l,id,MSI_DMA_OK,dev,token,bytes));
        CHECK(l.resource[id-1].device==dev&&l.resource[id-1].mapping==token&&
              l.resource[id-1].mapped_bytes==bytes&&l.resource[id-1].state==MSI_DMA_UNKNOWN);
        CHECK(!msi_dma_cleanup_begin(&l,true)&&!l.unmaps&&!l.frees);
    }
    /* Any output contradicts NO_EFFECT; failed/unknown Map without outputs
     * still cannot prove a nonexistent mapping. Retain the allocation. */
    for(unsigned f=0;f<5;f++) {
        dma_fixture(&l);id=dma_resource(&l,false);CHECK(msi_dma_map_begin(&l,id));
        CHECK(!msi_dma_map_end(&l,id,f==4?MSI_DMA_UNCERTAIN:MSI_DMA_NO_EFFECT,
                               f==0?0x200000:0,f==1?4:0,f==2?4096:f==3?UINT64_MAX:0));
        CHECK(l.resource[0].state==MSI_DMA_UNKNOWN&&!msi_dma_all_released(&l));
    }
    for(unsigned f=0;f<3;f++) {
        dma_fixture(&l);CHECK(msi_dma_alloc_begin(&l,1,&id));
        CHECK(!msi_dma_alloc_end(&l,id,f==0?MSI_DMA_NO_EFFECT:MSI_DMA_UNCERTAIN,f==2?0x100000:0));
        CHECK((l.resource[0].state==MSI_DMA_NOT_ALLOCATED)==(f==0));
        CHECK(msi_dma_all_released(&l)==(f==0));CHECK(!msi_dma_handoff_ready(&l));
    }
    /* Never FreeBuffer after unsuccessful Unmap, regardless of known effects. */
    for(unsigned phase=0;phase<2;phase++)for(unsigned unknown=0;unknown<2;unknown++) {
        dma_fixture(&l);id=dma_resource(&l,true);CHECK(msi_dma_cleanup_begin(&l,true));
        CHECK(msi_dma_unmap_begin(&l,id));
        MsiDmaResult error=unknown?MSI_DMA_UNCERTAIN:MSI_DMA_NO_EFFECT;
        if(!phase)CHECK(!msi_dma_unmap_end(&l,id,error));
        else {
            CHECK(msi_dma_unmap_end(&l,id,MSI_DMA_OK)&&msi_dma_free_begin(&l,id));
            CHECK(!msi_dma_free_end(&l,id,error));
        }
        uint64_t unmaps=l.unmaps,frees=l.frees;
        CHECK(l.cleanup_failed&&l.faulted&&!msi_dma_all_released(&l));
        CHECK(!msi_dma_free_begin(&l,id)&&!msi_dma_unmap_begin(&l,id));
        CHECK(l.unmaps==unmaps&&l.frees==frees&&l.resource[0].host&&l.resource[0].mapping);
    }
    /* Reentry/duplicate operation during an outstanding call is refused, but
     * the original completion still records outputs and confirmed effects. */
    for(unsigned phase=0;phase<4;phase++) {
        dma_fixture(&l);
        if(phase==0) {
            CHECK(msi_dma_alloc_begin(&l,5000,&id));
            CHECK(!msi_dma_alloc_begin(&l,5000,&other));
            CHECK(!msi_dma_alloc_end(&l,id,MSI_DMA_OK,0x100000));
            CHECK(l.resource[0].host==0x100000&&l.resource[0].state==MSI_DMA_ALLOCATED);
        } else {
            id=dma_resource(&l,phase>1);
            if(phase==1) {
                CHECK(msi_dma_map_begin(&l,id)&&!msi_dma_map_begin(&l,id));
                CHECK(!msi_dma_map_end(&l,id,MSI_DMA_OK,0x100000000,0x900,8192));
                CHECK(l.resource[0].mapping==0x900&&l.resource[0].state==MSI_DMA_MAPPED);
            } else {
                CHECK(msi_dma_cleanup_begin(&l,true)&&msi_dma_unmap_begin(&l,id));
                if(phase==2) {
                    CHECK(!msi_dma_unmap_begin(&l,id));
                    CHECK(!msi_dma_unmap_end(&l,id,MSI_DMA_OK)&&l.unmaps==1);
                } else {
                    CHECK(msi_dma_unmap_end(&l,id,MSI_DMA_OK)&&msi_dma_free_begin(&l,id));
                    CHECK(!msi_dma_free_begin(&l,id));
                    CHECK(!msi_dma_free_end(&l,id,MSI_DMA_OK)&&l.frees==1);
                }
            }
        }
        CHECK(l.violation&&!msi_dma_handoff_ready(&l));
    }
    for(unsigned phase=0;phase<4;phase++) {
        dma_fixture(&l);id=dma_resource(&l,true);
        if(phase==0)CHECK(!msi_dma_free_begin(&l,id)); /* premature */
        if(phase==1)CHECK(!msi_dma_cleanup_begin(&l,false));
        if(phase==2)CHECK(!msi_dma_init(&l)); /* no ledger reinitialization */
        if(phase==3){CHECK(msi_dma_cleanup_begin(&l,true));CHECK(!msi_dma_free_begin(&l,id));}
        CHECK(l.violation&&!msi_dma_all_released(&l)&&!l.unmaps&&!l.frees);
    }
    dma_fixture(&l);for(unsigned i=0;i<MSI_DMA_MAX;i++)CHECK(dma_resource(&l,true)==i+1);
    CHECK(!msi_dma_alloc_begin(&l,4096,&id)&&l.used==MSI_DMA_MAX);
    CHECK(msi_dma_inventory(&l,&n)&&n.live==MSI_DMA_MAX);
    /* Repeated completions and repeated cleanup are not idempotent success. */
    for(unsigned phase=0;phase<2;phase++) {
        dma_fixture(&l);id=dma_resource(&l,true);
        CHECK(msi_dma_cleanup_begin(&l,true)&&msi_dma_unmap_begin(&l,id));
        CHECK(msi_dma_unmap_end(&l,id,MSI_DMA_OK));
        if(!phase) {
            CHECK(!msi_dma_unmap_end(&l,id,MSI_DMA_OK));
            CHECK(!msi_dma_free_begin(&l,id)&&l.unmaps==1&&!l.frees);
        } else {
            CHECK(msi_dma_free_begin(&l,id)&&msi_dma_free_end(&l,id,MSI_DMA_OK));
            CHECK(!msi_dma_free_end(&l,id,MSI_DMA_OK)&&l.frees==1);
        }
        CHECK(!msi_dma_handoff_ready(&l)&&l.resource[0].mapping==0x900);
    }
    /* Exact upper DMA boundary accepted; crossing it was rejected above. */
    dma_fixture(&l);id=dma_resource(&l,false);CHECK(msi_dma_map_begin(&l,id));
    CHECK(msi_dma_map_end(&l,id,MSI_DMA_OK,(UINT64_C(1)<<49)-8192,0x900,8192));
    /* Invalid IDs/completions cannot make resources disappear. */
    dma_fixture(&l);id=dma_resource(&l,true);
    CHECK(!msi_dma_free_end(&l,id,MSI_DMA_OK));CHECK(l.resource[0].state==MSI_DMA_MAPPED);
    CHECK(!msi_dma_unmap_end(&l,0,MSI_DMA_OK)&&!l.unmaps&&!l.frees);
}
static bool premature_dma_cleanup(void *v,MsiProof *proof) {
    bool result=transaction(v,proof);Mock *m=v;
    CHECK(msi_dma_cleanup_begin(&m->dma,true));
    CHECK(msi_dma_unmap_begin(&m->dma,1));
    return result;
}
static void dma_workflow_tests(void) {
    for(unsigned f=D_ALLOC_NONE;f<=D_FREE_UNKNOWN;f++)for(unsigned id=1;id<=2;id++)
    for(unsigned effect=0;effect<2;effect++) {
        setup(OK);M.dma_fault=(enum DmaFault)f;M.dma_fault_id=id;M.dma_error_effect=effect!=0;
        MsiOps o=ops();execute_regression(&o);
        CHECK(S.state==MSI_STOPPED&&S.terminal_required&&S.dma_retained&&!M.windows&&!M.freed);
        CHECK(S.containment_verified&&!(M.config[1]&4)&&!M.life.claim_closes);
        CHECK(!msi_dma_handoff_ready(&M.dma));
        if(f<D_UNMAP_NONE) {
            CHECK(!S.flrs&&!M.dma_unmap_calls&&!M.dma_free_calls&&!M.life.unmaps&&!M.life.frees);
        } else {
            CHECK(S.flrs==1&&M.dma_unmap_calls==id&&M.life.frees==id-1);
            CHECK(M.dma_free_calls==(f<D_FREE_NONE?id-1:id));
            CHECK(M.life.unmaps==(f<D_FREE_NONE?id-1:id));
            CHECK(M.dma.resource[id-1].host&&M.dma.resource[id-1].mapping);
            if(f==D_FREE_UNKNOWN)CHECK(M.dma_allocated[id-1]==!effect);
            if(f==D_UNMAP_UNKNOWN)CHECK(M.dma_mapped[id-1]==!effect);
            /* Repeat the same commit with counters repaired: no cleanup retry. */
            unsigned unmaps=M.dma_unmap_calls,frees=M.dma_free_calls;
            S.state=MSI_WINDOWS_INVOKED;M.life.unmaps=M.life.frees=0;
            MsiGuard g={M.guard_epoch,MSI_GUARD_FINAL};commit_windows(&M,&g,&S.dma_retained);
            CHECK(!M.windows&&S.dma_retained&&M.dma_unmap_calls==unmaps&&M.dma_free_calls==frees);
        }
    }
    /* Resource ownership at stock commit must be checked independently of
     * aggregate counter repair, even when the lease is already invalid. */
    for(unsigned phase=0;phase<5;phase++) {
        setup(OK);unsigned id=dma_resource(&M.dma,phase==3);
        if(phase==1)CHECK(msi_dma_map_begin(&M.dma,id));
        if(phase==2) {
            CHECK(msi_dma_map_begin(&M.dma,id));
            CHECK(!msi_dma_map_end(&M.dma,id,MSI_DMA_UNCERTAIN,0,0,0));
        }
        if(phase==4) {
            CHECK(msi_dma_cleanup_begin(&M.dma,true));
            CHECK(msi_dma_free_begin(&M.dma,id)&&msi_dma_free_end(&M.dma,id,MSI_DMA_OK));
            CHECK(msi_dma_all_released(&M.dma)); /* tombstones still forbid clean stock history */
        }
        S.state=MSI_WINDOWS_INVOKED;M.guard_purpose=MSI_GUARD_STOCK;
        M.guard_active=true;M.guard_epoch=1;M.guard_broken=true;M.log_closed=true;
        MsiGuard g={1,MSI_GUARD_STOCK};commit_windows(&M,&g,&S.dma_retained);
        CHECK(S.dma_retained&&!M.windows&&!M.config_reads&&!M.mmio_reads);
        CHECK(!M.dma_unmap_calls&&!M.dma_free_calls);
        Scenarios++;
    }
    setup(OK);MsiOps o=ops();o.pre_reset.run=premature_dma_cleanup;execute_regression(&o);
    CHECK(S.terminal_required&&S.dma_retained&&!S.flrs&&!M.windows);
    CHECK(M.dma.resource[0].state==MSI_DMA_UNMAP_PENDING&&!M.life.unmaps&&!M.life.frees);
}
static void dma_regressions(void) {
    dma_unit_tests();dma_workflow_tests();
    printf("PASS: per-resource DMA states (%u ledger cases), partial effects and workflow retention.\n",DmaCases);
}
