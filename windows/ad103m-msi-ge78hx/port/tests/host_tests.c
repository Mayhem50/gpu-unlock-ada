/* SPDX-License-Identifier: GPL-2.0-only */
/* All side effects are virtual PCI/register/graph state in this process. */
#include "../src/port.h"
#include "captured_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

static unsigned Checks,Scenarios;
static const uint32_t SafeRegisters[]={MSI_CHIP,0x10,0,MSI_FULL_SS0,MSI_FULL_SS1,0,0xffffffff};
#define CHECK(x) do{Checks++;if(!(x)){fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
enum Fault {OK,STAGE_FAIL,OBSERVE_DRIFT,EXTRA_OWNER,STOP1_FAIL,STOP2_FAIL,STOP3_FAIL,
 CONSOLE_DRIFT,BOUNDARY_DRIFT,USB_DRIFT,CLAIM_FAIL,TX_FAIL,MARKER_FAIL,HALT_FAIL,PERMISSION_FAIL,
 PENDING_STUCK,FLR_WRITE_FAIL,RESET_WRONG_ID,RESET_TIMEOUT,RESTORE_FAIL,POST_SELECTOR_FAIL,
 POST_HALT_FAIL,DMA_FREE_FAIL,CLOSE_CLAIM_FAIL,CLOSE_LOG_FAIL,FINAL_GRAPH_FAIL,FINAL_MMIO_FAIL,
 RETURNED_WINDOWS,DECODE_LOST,DECODE_RESTORE_FAIL,EXTRA_CONFIG_DRIFT,PRE_SEC2_INACCESSIBLE,
 FINAL_ID_DRIFT,FINAL_BAR_DRIFT,FINAL_MSI_DRIFT,CONTAINMENT_FAIL};
typedef struct {
    MsiObservation observation;MsiGraph graph;uint32_t config[64];enum Fault fault;
    unsigned stops,transactions,stages,snapshots,observations,claims,freed,closes,windows;
    uint64_t clock,flr_at;bool silenced,flr_written,log_closed,bar_restore_seen;
    unsigned restored_bars,mmio_reads;
    bool guard_active,guard_broken,engine_running;
    uint64_t guard_epoch;unsigned guard_purpose;
    uint32_t guard_reference[64];
    MsiGraph guard_graph;
    unsigned config_reads,config_writes;
    uint32_t safety_regs[7];
    bool dma_commit_spent,transfer_commit_spent;
    bool dma_prepared,claim_prepared;
    MsiLifecycle life;
    uint32_t fbpa;
} Mock;
static Mock M;
static MsiSession S;
static uint8_t *Prom;
static jmp_buf Transfer;
static const MsiQualification SyntheticApproval={true,true,true,true};
static bool ProbeReentry;
static unsigned Reentries;
static MsiOps ops(void);
static void reentry_probe(void) {
    if(!ProbeReentry)return;
    MsiSession before=S;MsiOps nested=ops();nested.pre_reset.run=NULL;
    CHECK(S.state!=MSI_NEW);Reentries++;
    msi_run(&nested,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(!memcmp(&before,&S,sizeof(S)));
}

static void setup(enum Fault f) {
    memset(&M,0,sizeof(M));memset(&S,0,sizeof(S));M.fault=f;
    memcpy(M.observation.config,CaptureConfig0,sizeof(M.observation.config));
    memcpy(M.observation.reg,CaptureRegisters0,sizeof(M.observation.reg));
    M.observation.bar0_base=0x5e000000;M.observation.bar0_bytes=0x1000000;M.observation.bus=1;
    memcpy(M.config,CaptureConfig0,sizeof(M.config));
    M.fbpa=CaptureRegisters0[REG_FBPA];
    memcpy(M.graph.edges,CaptureEdges,sizeof(CaptureEdges));M.graph.count=5;
    M.graph.console_alive=M.graph.identities_verified=true;
    memset(M.graph.console,0x11,32);memset(M.graph.boundary,0x22,32);memset(M.graph.logging,0x33,32);
}
static void touch(Mock *m) {
    reentry_probe();
    if(m->flr_written)CHECK(m->clock-m->flr_at>=100000);
}
static bool observe(void *v,MsiObservation *out) {
    reentry_probe();
    Mock *m=v;m->observations++;*out=m->observation;memcpy(out->config,m->config,sizeof(m->config));
    if(m->fault==OBSERVE_DRIFT&&m->observations>=2)out->reg[REG_SS0]^=1;
    return true;
}
static bool snapshot(void *v,MsiGraph *out) {
    reentry_probe();
    Mock *m=v;m->snapshots++;*out=m->graph;
    if(m->fault==EXTRA_OWNER&&!m->stops) {
        out->edges[out->count++]=(MsiEdge){NODE_GOP,99,NODE_GOP,PROTO_GOP,16,1};
    }
    if(m->stops) {
        if(m->fault==CONSOLE_DRIFT)out->console[0]^=1;
        if(m->fault==BOUNDARY_DRIFT)out->boundary[0]^=1;
        if(m->fault==USB_DRIFT)out->logging[0]^=1;
    }
    if(m->log_closed&&m->fault==FINAL_GRAPH_FAIL)out->console_alive=false;
    return true;
}
static bool stage_windows(void *v){reentry_probe();Mock *m=v;m->stages++;CHECK(!m->stops&&!m->flr_written);return m->fault!=STAGE_FAIL;}
static bool silence(void *v){reentry_probe();Mock *m=v;CHECK(m->stages==1);m->silenced=true;return true;}
static bool disconnect(void *v,unsigned driver) {
    reentry_probe();
    Mock *m=v;CHECK(m->silenced&&m->stages==1&&driver==m->stops+1);m->stops++;
    unsigned out=0;
    for(unsigned i=0;i<m->graph.count;i++)if(m->graph.edges[i].agent!=driver)m->graph.edges[out++]=m->graph.edges[i];
    m->graph.count=out;
    if(driver==3&&(m->fault==DECODE_LOST||m->fault==DECODE_RESTORE_FAIL))m->config[1]&=~3u;
    if(driver==3&&m->fault==EXTRA_CONFIG_DRIFT)m->config[4]^=0x1000000;
    return !((driver==1&&m->fault==STOP1_FAIL)||(driver==2&&m->fault==STOP2_FAIL)||
             (driver==3&&m->fault==STOP3_FAIL));
}
static bool claim(void *v){reentry_probe();Mock *m=v;CHECK(m->stops==3&&!m->graph.count);m->claims++;m->graph.exclusive=true;return m->fault!=CLAIM_FAIL;}
static bool restore_decode(void *v,unsigned mask) {
    reentry_probe();
    Mock *m=v;CHECK(m->graph.exclusive&&m->stops==3&&mask&&!(mask&~3u)&&!(m->config[1]&4));
    if(m->fault==DECODE_RESTORE_FAIL)return false;
    m->config[1]|=mask;return true;
}
static bool transaction(void *v,MsiProof *proof) {
    reentry_probe();
    Mock *m=v;CHECK(m->graph.exclusive&&m->stops==3);m->transactions++;m->config[1]|=4;
    memcpy(m->safety_regs,SafeRegisters,sizeof(SafeRegisters));
    m->life.live_dma=2;
    *proof=(MsiProof){MSI_MARKER,true,true,true,true,2};
    if(m->fault==MARKER_FAIL)proof->marker^=1;
    if(m->fault==HALT_FAIL)proof->halted=false;
    if(m->fault==PERMISSION_FAIL)proof->permissions=false;
    return m->fault!=TX_FAIL&&m->fault!=CONTAINMENT_FAIL;
}
static bool read_config(void *v,unsigned offset,unsigned width,uint32_t *value) {
    Mock *m=v;touch(m);m->config_reads++;CHECK(offset<256&&(width==1||width==2||width==4)&&((offset&3)+width<=4));
    if(m->fault==CONTAINMENT_FAIL&&m->transactions&&offset==4)return false;
    if(m->log_closed&&m->fault==FINAL_ID_DRIFT&&offset==0){*value=0x275710de;return true;}
    if(m->log_closed&&m->fault==FINAL_BAR_DRIFT&&offset==0x10){*value=0x60000000;return true;}
    if(offset==0&&m->flr_written) {
        if(m->fault==RESET_WRONG_ID){*value=0x275710de;return true;}
        if(m->fault==RESET_TIMEOUT){*value=0xffffffff;return true;}
    }
    *value=m->config[offset/4]>>((offset&3)*8);
    if(width!=4)*value&=(1u<<(width*8))-1;
    if(m->log_closed&&m->fault==FINAL_MSI_DRIFT&&offset==MSI_MSI+2)*value|=1;
    if(offset==MSI_PCIE+10&&!m->flr_written&&m->fault==PENDING_STUCK)*value|=32;
    return true;
}
static bool write_config(void *v,unsigned offset,unsigned width,uint32_t value) {
    Mock *m=v;touch(m);m->config_writes++;CHECK(offset<256&&(width==1||width==2||width==4)&&((offset&3)+width<=4));
    if(offset==MSI_PCIE+8&&width==2&&(value&0x8000)) {
        if(S.state!=MSI_RESET){m->life.owner_violation=true;return false;}
        m->life.resets++;
        CHECK(!(m->config[1]&4));m->flr_written=true;m->flr_at=m->clock;
        m->config[1]&=~7u;
        memset(m->config+4,0,6*sizeof(uint32_t));
        return m->fault!=FLR_WRITE_FAIL;
    }
    if(offset>=0x10&&offset<=0x24) {
        CHECK(m->flr_written&&!(m->config[1]&7)&&offset==0x24-4*m->restored_bars);
        if(!m->restored_bars)m->life.restores++;
        m->restored_bars++;m->bar_restore_seen=true;
        if(m->fault==RESTORE_FAIL&&offset==0x18)return false;
    }
    if(offset==4)CHECK(!(value&4)); /* no reset/restoration may enable BME */
    uint32_t mask=width==4?0xffffffffu:((1u<<(width*8))-1);
    unsigned shift=(offset&3)*8;
    m->config[offset/4]=(m->config[offset/4]&~(mask<<shift))|((value&mask)<<shift);
    return true;
}
static void stall(void *v,unsigned usec){reentry_probe();Mock *m=v;m->clock+=usec;}
static bool read_mmio(void *v,uint32_t offset,uint32_t *value) {
    Mock *m=v;touch(m);m->mmio_reads++;CHECK(m->transactions==1);
    switch(offset) {
        case 0:*value=m->safety_regs[0];break;
        case 0x110100:*value=m->fault==POST_HALT_FAIL?0xbadf0000:m->engine_running?0:m->safety_regs[1];break;
        case 0x1fa828:*value=m->safety_regs[2];break;
        case 0x82381c:*value=m->fault==POST_SELECTOR_FAIL&&m->flr_written?0:m->safety_regs[3];break;
        case 0x823820:*value=m->log_closed&&m->fault==FINAL_MMIO_FAIL?0:m->safety_regs[4];break;
        case 0x823818:*value=m->safety_regs[5];break;
        case 0x823804:*value=m->safety_regs[6];break;
        case 0x1fa7c4:*value=0xffffffff;break;
        case 0x1fa7cc:*value=0xfffff0ff;break;
        case 0x9a0148:*value=m->fbpa;break;
        case 0x840100:*value=m->fault==PRE_SEC2_INACCESSIBLE?0xbadf5620:0x10;break;
        default:CHECK(false);return false;
    }
    return true;
}
static bool guard_current(Mock *m,const MsiGuard *g) {
    return g&&g->epoch&&m->guard_active&&!m->guard_broken&&
        m->guard_epoch==g->epoch&&m->guard_purpose==g->purpose;
}
static bool atomic_safe(Mock *m,const MsiGuard *g,bool exclusive) {
    /* No external calls between this validation and the modeled action. */
    return guard_current(m,g)&&g->purpose==MSI_GUARD_FINAL&&
        !memcmp(m->config,m->guard_reference,sizeof(m->config))&&
        !(m->config[1]&4)&&m->flr_written&&m->clock-m->flr_at>=100000&&
        !m->life.owner_violation&&m->life.resets==1&&m->life.restores==1&&
        !m->life.unmaps&&!m->life.frees&&!m->life.claim_closes&&m->life.live_dma==2&&
        (m->fbpa&0xffff0000u)!=0xbadf0000u&&
        !m->engine_running&&!memcmp(m->safety_regs,SafeRegisters,sizeof(SafeRegisters))&&
        m->graph.identities_verified&&m->graph.console_alive&&
        !m->graph.count&&m->graph.exclusive==exclusive&&
        !memcmp(m->graph.boundary,m->guard_graph.boundary,32)&&
        !memcmp(m->graph.console,m->guard_graph.console,32)&&
        !memcmp(m->graph.logging,m->guard_graph.logging,32);
}
static bool begin_guard(void *v,unsigned purpose,const uint32_t *reference,MsiGuard *g) {
    reentry_probe();Mock *m=v;
    if(m->guard_active||!g||(purpose!=MSI_GUARD_FINAL&&purpose!=MSI_GUARD_STOCK)||
       (purpose==MSI_GUARD_FINAL&&!reference))return false;
    m->guard_active=true;m->guard_purpose=purpose;m->guard_epoch++;
    if(reference)memcpy(m->guard_reference,reference,sizeof(m->guard_reference));
    m->guard_graph=m->graph;
    *g=(MsiGuard){m->guard_epoch,purpose};return true;
}
static bool guard_valid(void *v,const MsiGuard *g){reentry_probe();return guard_current(v,g);}
static bool prepare_dma_release(void *v,const MsiGuard *g) {
    reentry_probe();Mock *m=v;
    if(m->dma_commit_spent)return false;
    m->dma_commit_spent=true;
    if(!atomic_safe(m,g,true)||m->fault==DMA_FREE_FAIL)return false;
    CHECK(m->restored_bars==6&&!m->freed);m->dma_prepared=true;return true;
}
static bool prepare_claim_close(void *v){reentry_probe();Mock *m=v;CHECK(m->guard_active&&m->dma_prepared&&!m->freed&&m->graph.exclusive);m->claim_prepared=true;return m->fault!=CLOSE_CLAIM_FAIL;}
static bool close_log(void *v){reentry_probe();Mock *m=v;CHECK(!m->log_closed);m->closes++;m->log_closed=true;return m->fault!=CLOSE_LOG_FAIL;}
static bool lifecycle(void *v,MsiLifecycle *out){reentry_probe();*out=((Mock *)v)->life;return true;}
static void commit_windows(void *v,const MsiGuard *g,bool *dma_retained) {
    Mock *m=v;
    if(S.state!=MSI_WINDOWS_INVOKED){m->life.owner_violation=true;return;}
    if(m->transfer_commit_spent)return;
    m->transfer_commit_spent=true;
    reentry_probe();
    if(!guard_current(m,g)||!m->log_closed)return;
    if(g->purpose==MSI_GUARD_FINAL) {
        if(!atomic_safe(m,g,true)||m->freed||m->stops!=3||!m->dma_prepared||!m->claim_prepared||!dma_retained||!*dma_retained)return;
    } else if(g->purpose!=MSI_GUARD_STOCK||m->stops||m->transactions)return;
    /* Model one indivisible guarded commit. No callback or hook from here
     * through resource release/claim close and transfer. */
    if(g->purpose==MSI_GUARD_FINAL){
        m->life.unmaps++;m->life.frees++;m->life.claim_closes++;m->life.live_dma=0;
        m->freed=1;*dma_retained=false;m->graph.exclusive=false;
    }
    m->windows++;
    if(m->fault!=RETURNED_WINDOWS){m->guard_active=false;longjmp(Transfer,1);}
}
static MsiOps ops(void) {
    MsiOps o={&M,observe,snapshot,stage_windows,silence,disconnect,claim,restore_decode,
              {MSI_PRE_RESET_RETAINED_V1,transaction},lifecycle,
              read_config,write_config,stall,read_mmio,begin_guard,guard_valid,prepare_dma_release,prepare_claim_close,close_log,commit_windows};
    return o;
}
static void run(enum Fault fault,bool consent,const MsiQualification *q) {
    setup(fault);MsiOps o=ops();Scenarios++;
    if(!setjmp(Transfer))msi_run(&o,&S,&M.observation,Prom,1048576,q,consent);
    CHECK(M.windows<=1&&S.flrs<=1&&M.transactions<=1&&M.stops<=3&&M.closes<=1);
}
static void profile_tests(void) {
    setup(OK);CHECK(msi_profile(&M.observation,Prom,1048576));
    memcpy(M.observation.config,CaptureConfig1,sizeof(M.observation.config));
    memcpy(M.observation.reg,CaptureRegisters1,sizeof(M.observation.reg));
    CHECK(msi_profile(&M.observation,Prom,1048576));
    MsiObservation valid=M.observation;
    for(unsigned i=0;i<22;i++) {
        M.observation=valid;M.observation.reg[i]^=1;
        /* SS0 has precisely two observed values, not a permissive mask. */
        CHECK(!msi_profile(&M.observation,Prom,1048576));
    }
    unsigned indices[]={0,1,2,4,5,6,7,8,9,11};
    for(unsigned i=0;i<sizeof(indices)/sizeof(indices[0]);i++) {
        M.observation=valid;M.observation.config[indices[i]]^=1;
        CHECK(!msi_profile(&M.observation,Prom,1048576));
    }
    M.observation=valid;M.observation.config[0]=0x275710de;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[1]|=4;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[1]&=~2u;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.bus=2;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.bar0_bytes/=2;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[(MSI_PCIE+4)/4]&=~(1u<<28);CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[(MSI_PCIE+8)/4]|=0x8000;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[(MSI_PM+4)/4]|=3;CHECK(!msi_profile(&M.observation,Prom,1048576));
    M.observation=valid;M.observation.config[MSI_MSI/4]|=1u<<16;CHECK(!msi_profile(&M.observation,Prom,1048576));
    CHECK(!msi_profile(&valid,Prom,1048575));
    Prom[0x45788]^=1;CHECK(!msi_profile(&valid,Prom,1048576));Prom[0x45788]^=1;
}
static void workflow_tests(void) {
    for(volatile unsigned f=OK;f<=CONTAINMENT_FAIL;f++) {
        run((enum Fault)f,true,&SyntheticApproval);
        if(f==OK||f==DECODE_LOST) {
            CHECK(S.state==MSI_WINDOWS_INVOKED&&M.windows==1&&S.flrs==1&&!S.dma_retained&&M.stops==3);
            CHECK(M.restored_bars==6&&M.clock>=100000);
        } else {
            CHECK(S.state==MSI_STOPPED||S.state==MSI_REFUSED);
            CHECK(M.windows==(f==RETURNED_WINDOWS?1u:0u));
            if(f==CONTAINMENT_FAIL)CHECK(S.dma_retained&&!S.containment_verified);
            else if(S.mutated)CHECK(S.containment_verified&&!(M.config[1]&4));
            if(S.mutated)CHECK(S.terminal_required);
            if(S.started&&!M.freed)CHECK(S.dma_retained);
        }
        if(f==FLR_WRITE_FAIL)CHECK(S.flrs==1&&M.clock-M.flr_at>=100000);
        if(f==PENDING_STUCK)CHECK(S.flrs==0&&M.clock==100000);
        if(f==RESET_TIMEOUT)CHECK(M.clock==1100000);
        if(f==TX_FAIL||f==MARKER_FAIL||f==HALT_FAIL||f==PERMISSION_FAIL)CHECK(S.flrs==0&&!M.freed);
        if(f==PRE_SEC2_INACCESSIBLE)CHECK(S.flrs==0&&!M.freed&&S.dma_retained);
    }
    run(OK,false,&SyntheticApproval);CHECK(M.windows==1&&!M.stops&&!M.transactions&&!S.flrs);
    run(RETURNED_WINDOWS,false,&SyntheticApproval);
    CHECK(S.state==MSI_STOPPED&&M.windows==1&&!M.stops&&!M.transactions&&!S.flrs);
    MsiQualification q=SyntheticApproval;
    for(unsigned i=0;i<4;i++) {
        q=SyntheticApproval;
        if(i==0)q.ownership_reviewed=false;
        if(i==1)q.console_callbacks_reviewed=false;
        if(i==2)q.windows_image_reviewed=false;
        if(i==3)q.assets_verified=false;
        run(OK,true,&q);CHECK(S.state==MSI_REFUSED&&!M.windows&&!M.stops&&!M.transactions);
    }
    run(RETURNED_WINDOWS,true,&SyntheticApproval);
    unsigned stops=M.stops,tx=M.transactions;
    MsiOps o=ops();msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(M.stops==stops&&M.transactions==tx&&M.windows==1);
    setup(OK);o=ops();o.pre_reset.run=NULL;
    msi_run(&o,&S,&M.observation,Prom,1048576,&SyntheticApproval,true);
    CHECK(S.state==MSI_REFUSED&&!M.stops);
}
static void ownership_tests(void) {
    setup(OK);MsiGraph base=M.graph,now=base;
    CHECK(msi_graph_stage(&base,&now,0,false));
    for(unsigned i=0;i<5;i++) {
        now=base;now.edges[i].count++;
        CHECK(!msi_graph_stage(&base,&now,0,false));
        now=base;now.edges[i].attributes^=8;
        CHECK(!msi_graph_stage(&base,&now,0,false));
        now=base;now.edges[i].controller=99;
        CHECK(!msi_graph_stage(&base,&now,0,false));
    }
    now=base;now.edges[1]=now.edges[0];CHECK(!msi_graph_stage(&base,&now,0,false));
    now=base;now.identities_verified=false;CHECK(!msi_graph_stage(&base,&now,0,false));
    now=base;now.console_alive=false;CHECK(!msi_graph_stage(&base,&now,0,false));
    now=base;now.exclusive=true;CHECK(!msi_graph_stage(&base,&now,0,false));
}
#include "regression_tests.h"
#include "followup_regressions.h"
#include "method_regressions.h"
int main(void) {
    FILE *f=fopen("build/prom.bin","rb");CHECK(f!=NULL);Prom=malloc(1048576);CHECK(Prom);
    CHECK(fread(Prom,1,1048576,f)==1048576&&fgetc(f)==EOF);fclose(f);
    profile_tests();ownership_tests();workflow_tests();regression_tests();followup_regressions();method_regressions();free(Prom);
    printf("PASS: %u host assertions, %u workflow scenarios.\n",Checks,Scenarios);
    puts("ROM/FWSEC profile, staged release, FLR quiet time/restore, DMA retention and Windows invocation: mocked only.");
    puts("No GPU access, firmware execution, payload or native UEFI backend; real candidate remains refused.");
    return 0;
}
