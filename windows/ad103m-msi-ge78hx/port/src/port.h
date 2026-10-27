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
    /* Remain false for the real observed candidate until reviewed separately.
     * Host tests use synthetic approval to exercise downstream states. */
    bool ownership_reviewed,console_callbacks_reviewed,windows_image_reviewed;
    bool assets_verified;
} MsiQualification;
typedef struct {
    void *context;
    bool (*observe)(void *,MsiObservation *);
    bool (*snapshot)(void *,MsiGraph *);
    bool (*stage_windows)(void *); /* must pin private image/path before release */
    bool (*silence_console)(void *);
    bool (*disconnect)(void *,unsigned driver);
    bool (*claim)(void *);
    bool (*restore_decode)(void *,unsigned io_memory_mask); /* protocol-aware IO/MEM only */
    bool (*transaction)(void *,MsiProof *); /* mock only; no payload linked */
    bool (*read_config)(void *,unsigned offset,unsigned width,uint32_t *);
    bool (*write_config)(void *,unsigned offset,unsigned width,uint32_t);
    void (*stall)(void *,unsigned microseconds);
    bool (*read_mmio)(void *,uint32_t offset,uint32_t *);
    bool (*release_dma)(void *);
    bool (*close_claim)(void *);
    bool (*close_log)(void *);
    void (*start_windows)(void *); /* any return is failure, never success */
} MsiOps;
enum MsiState {MSI_NEW,MSI_REFUSED,MSI_STAGED,MSI_RELEASED,MSI_TRANSACTION,
               MSI_RESET,MSI_CLEAN,MSI_WINDOWS_INVOKED,MSI_STOPPED};
typedef struct {
    enum MsiState state;
    bool mutated,started,claim_attempted,dma_retained,containment_verified;
    unsigned stops,transactions,flrs,windows_calls;
    unsigned failed_stage;
} MsiSession;
bool msi_graph_stage(const MsiGraph *,const MsiGraph *,unsigned stage,bool exclusive);
bool msi_reset(const MsiOps *,MsiSession *);
bool msi_post_reset(const MsiOps *);
void msi_run(const MsiOps *,MsiSession *,const MsiObservation *,
             const uint8_t *,size_t,const MsiQualification *,bool consent);
#endif
