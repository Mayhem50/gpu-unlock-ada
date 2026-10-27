/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSI_GE78HX_OFFLINE_PORT_H
#define MSI_GE78HX_OFFLINE_PORT_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#if !defined(MSI_OFFLINE_HOST) || !defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 1
#error This port currently supports host-only builds, never firmware execution
#endif
#ifdef GU_UEFI_PORT
#error No UEFI backend is available or authorized in this offline port
#endif

#define MSI_ID 0x271710deu
#define MSI_SUBSYSTEM 0x13cd1462u
#define MSI_CHIP 0x193000a1u
#define MSI_FULL_SS0 0x88888888u
#define MSI_FULL_SS1 8u
#define MSI_MARKER 0x67b00001u
enum { MSI_PCIE=0x78, MSI_PM=0x60, MSI_MSI=0x68 };
enum { REG_CHIP,REG_GFW_PLM,REG_GFW_BOOT,REG_GFW_CPUCTL,REG_WPR_LO,REG_WPR_HI,
       REG_WPR_CFG,REG_WPR,REG_FBPA,REG_FEAT,REG_READOUT0,REG_READOUT1,REG_SS0,REG_SS1,
       REG_FB_SIZE,REG_VGA,REG_MMU_PLM,REG_MMU_LO,REG_MMU_HI,REG_BOOT_FUSE,REG_FWSEC_FUSE,REG_SEC2 };
typedef struct {
    uint32_t config[64],reg[22];
    uint64_t bar0_base,bar0_bytes;
    unsigned segment,bus,device,function;
} MsiObservation;
bool msi_profile(const MsiObservation *,const uint8_t *prom,size_t prom_bytes);
bool msi_capabilities(const uint32_t config[64]);

/* These are semantic identities validated by the capture/ownership adapter,
 * not numeric firmware handles. The native adapter is not implemented. */
enum { NODE_PCI=1,NODE_GOP,NODE_CONSOLE };
enum { DRIVER_TEXT=1,DRIVER_GRAPHICS,DRIVER_NVIDIA };
enum { PROTO_PCI=1,PROTO_PATH,PROTO_TEXT,PROTO_GOP };
typedef struct {unsigned source,agent,controller,protocol,attributes,count;} MsiEdge;
typedef struct {
    MsiEdge edges[8];unsigned count;
    uint8_t boundary[32],console[32],logging[32];
    bool identities_verified,console_alive,exclusive;
} MsiGraph;
typedef struct {
    uint32_t marker;bool halted,permissions,selectors,completed;
    unsigned dma_live;
} MsiProof;
typedef struct {
    uint32_t plm[4],readout,chip_before,chip_after;
    unsigned attempted_mask,transport_mask,valid_mask;
    bool readout_valid;
} MsiPreFlrEvidence;
typedef struct {
    uint64_t resets,restores,unmaps,frees,claim_closes;
    unsigned live_dma;
    bool owner_violation;
} MsiLifecycle;
/* Only this phase boundary is supported. A complete upstream unlock function
 * also owns reset/unwind and is intentionally NOT an admissible callback. */
enum { MSI_PRE_RESET_RETAINED_V1=0x4d530101u,MSI_FULL_TRANSACTION=0x4d530102u };
typedef struct {
    unsigned boundary;
    bool (*run)(void *,MsiProof *);
} MsiPreResetStep;
typedef struct {
    /* Remain false for the real observed candidate until reviewed separately.
     * Host tests use synthetic approval to exercise downstream states. */
    bool ownership_reviewed,console_callbacks_reviewed,windows_image_reviewed;
    bool assets_verified;
} MsiQualification;
enum { MSI_GUARD_FINAL=1,MSI_GUARD_STOCK };
typedef struct {uint64_t epoch;unsigned purpose;} MsiGuard;
typedef struct {
    void *context;
    bool (*observe)(void *,MsiObservation *);
    bool (*snapshot)(void *,MsiGraph *);
    bool (*stage_windows)(void *); /* must pin private image/path before release */
    bool (*silence_console)(void *);
    bool (*disconnect)(void *,unsigned driver);
    bool (*claim)(void *);
    bool (*restore_decode)(void *,unsigned io_memory_mask); /* protocol-aware IO/MEM only */
    MsiPreResetStep pre_reset; /* no FLR, PCI restore, unmap/free or claim close */
    bool (*lifecycle)(void *,MsiLifecycle *); /* RAM bookkeeping only; fail closed */
    bool (*read_config)(void *,unsigned offset,unsigned width,uint32_t *);
    bool (*write_config)(void *,unsigned offset,unsigned width,uint32_t);
    void (*stall)(void *,unsigned microseconds);
    bool (*read_mmio)(void *,uint32_t offset,uint32_t *);
    /* Host-only contract: exclude competing mutations until guarded transfer
     * or terminal stop. A failed begin may be partial; never implicitly unlock.
     * Atomic guarded actions MUST revalidate lease, fresh hardware/ownership
     * and resource state at the action, not trust earlier caller readbacks.
     * The release/transfer commit is spent before validation/effects, even on failure;
     * a retained lease does not authorize replay of free or OS transfer.
     * No native implementation or TPL policy is supplied by this prototype. */
    bool (*begin_guard)(void *,unsigned purpose,const uint32_t reference[64],MsiGuard *);
    bool (*guard_valid)(void *,const MsiGuard *);
    /* Preparations must not free/unmap DMA or close the exclusive claim. */
    bool (*prepare_dma_release)(void *,const MsiGuard *);
    bool (*prepare_claim_close)(void *);
    bool (*close_log)(void *);
    /* Last compound action: validate lease/state, release DMA, close claim,
     * transfer. No ordinary external callback after actual resource release.
     * Update dma_retained only when ALL buffers/mappings are safely released.
     * Partial failure retains the guard and remaining resources. Any return
     * is failure. This abstract action has ONLY a CPU mock implementation. */
    void (*commit_windows)(void *,const MsiGuard *,bool *dma_retained);
} MsiOps;
enum MsiState {MSI_NEW,MSI_ENTERED,MSI_REFUSED,MSI_STAGED,MSI_RELEASED,MSI_TRANSACTION,MSI_RESET_READY,MSI_RESET_VERIFYING,
               MSI_RESET,MSI_READY_TO_COMMIT,MSI_WINDOWS_INVOKED,MSI_STOPPED};
typedef struct {
    enum MsiState state;
    bool mutated,started,claim_attempted,dma_retained,containment_verified;
    /* Host result only: never permission for an EFI entry point to return. */
    bool terminal_required,cleanup_failed;
    bool console_may_be_changed,guard_attempted,guard_retained;
    unsigned stops,transactions,flrs,windows_calls;
    unsigned failed_stage;
    MsiPreFlrEvidence pre_flr;
} MsiSession;
bool msi_graph_stage(const MsiGraph *,const MsiGraph *,unsigned stage,bool exclusive);
bool msi_config_matches(const MsiOps *,const uint32_t reference[64]);
bool msi_pre_flr_checks(const MsiOps *,const uint32_t reference[64],MsiPreFlrEvidence *);
bool msi_post_reset(const MsiOps *,const uint32_t reference[64]);
void msi_run(const MsiOps *,MsiSession *,const MsiObservation *,
             const uint8_t *,size_t,const MsiQualification *,bool consent);
#endif
