/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MSI_OFFLINE_DMA_H
#define MSI_OFFLINE_DMA_H
#include "port.h" /* host-only compilation guard */

/* Pure RAM model: numeric addresses/tokens are never dereferenced. Every
 * begin reserves its operation before a backend could be called. IDs are
 * session-local, never reused; completed records remain as tombstones.
 * NO_EFFECT requires independent proof: an EFI error alone is UNCERTAIN. */
enum {MSI_DMA_MAX=128};
typedef enum {MSI_DMA_OK,MSI_DMA_NO_EFFECT,MSI_DMA_UNCERTAIN} MsiDmaResult;
typedef enum {MSI_DMA_UNUSED,MSI_DMA_ALLOC_PENDING,MSI_DMA_ALLOCATED,
    MSI_DMA_MAP_PENDING,MSI_DMA_MAPPED,MSI_DMA_UNMAP_PENDING,MSI_DMA_UNMAPPED,
    MSI_DMA_FREE_PENDING,MSI_DMA_FREED,MSI_DMA_NOT_ALLOCATED,MSI_DMA_UNKNOWN} MsiDmaState;
typedef struct {
    uint64_t requested,capacity,pages,host,device,mapping,mapped_bytes;
    MsiDmaState state,free_from;
    bool map_spent,unmap_spent,free_spent;
} MsiDmaResource;
typedef struct {
    MsiDmaResource resource[MSI_DMA_MAX];
    unsigned used;
    bool initialized,faulted,violation,cleanup_spent,cleaning,cleanup_failed;
    uint64_t unmaps,frees; /* confirmed successes only, never authorization */
} MsiDmaLedger;
typedef struct {unsigned live,mapped,unknown,pending;} MsiDmaInventory;

bool msi_dma_init(MsiDmaLedger *); /* only a fresh zero-initialized ledger */
bool msi_dma_inventory(const MsiDmaLedger *,MsiDmaInventory *);
bool msi_dma_all_released(const MsiDmaLedger *);
bool msi_dma_handoff_ready(const MsiDmaLedger *);
bool msi_dma_alloc_begin(MsiDmaLedger *,uint64_t bytes,unsigned *id);
bool msi_dma_alloc_end(MsiDmaLedger *,unsigned id,MsiDmaResult,uint64_t host);
bool msi_dma_map_begin(MsiDmaLedger *,unsigned id);
bool msi_dma_map_end(MsiDmaLedger *,unsigned id,MsiDmaResult,uint64_t device,
                     uint64_t mapping,uint64_t mapped_bytes);
/* Authorization is a synthetic host predicate, NOT a UEFI quiescence proof.
 * No cleanup attempt is permitted with pending/unknown ownership. The first
 * failed cleanup stops the whole session; no retry or implicit recovery. */
bool msi_dma_cleanup_begin(MsiDmaLedger *,bool authorized);
bool msi_dma_unmap_begin(MsiDmaLedger *,unsigned id);
bool msi_dma_unmap_end(MsiDmaLedger *,unsigned id,MsiDmaResult);
bool msi_dma_free_begin(MsiDmaLedger *,unsigned id);
bool msi_dma_free_end(MsiDmaLedger *,unsigned id,MsiDmaResult);
#endif
