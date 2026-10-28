/* SPDX-License-Identifier: GPL-2.0-only */
#include "dma.h"

static bool fail(MsiDmaLedger *l) {
    if(l){l->faulted=true;l->violation=true;}
    return false;
}
bool msi_dma_init(MsiDmaLedger *l) {
    if(!l)return false;
    const unsigned char *raw=(const unsigned char *)l;
    for(size_t i=0;i<sizeof(*l);i++)if(raw[i])return fail(l);
    l->initialized=true;return true;
}
static bool pending(MsiDmaState s) {
    return s==MSI_DMA_ALLOC_PENDING||s==MSI_DMA_MAP_PENDING||
           s==MSI_DMA_UNMAP_PENDING||s==MSI_DMA_FREE_PENDING;
}
bool msi_dma_inventory(const MsiDmaLedger *l,MsiDmaInventory *out) {
    if(!l||!out||!l->initialized||l->used>MSI_DMA_MAX)return false;
    *out=(MsiDmaInventory){0};
    for(unsigned i=0;i<MSI_DMA_MAX;i++) {
        const MsiDmaResource *r=&l->resource[i];
        if(i>=l->used){if(r->state!=MSI_DMA_UNUSED)return false;continue;}
        if(r->state<=MSI_DMA_UNUSED||r->state>MSI_DMA_UNKNOWN)return false;
        if(r->state!=MSI_DMA_FREED&&r->state!=MSI_DMA_NOT_ALLOCATED)out->live++;
        if(r->state==MSI_DMA_MAPPED||r->state==MSI_DMA_UNMAP_PENDING)out->mapped++;
        if(r->state==MSI_DMA_UNKNOWN)out->unknown++;
        if(pending(r->state))out->pending++;
    }
    return true;
}
bool msi_dma_all_released(const MsiDmaLedger *l) {
    MsiDmaInventory n;
    return msi_dma_inventory(l,&n)&&!l->violation&&!n.live;
}
bool msi_dma_handoff_ready(const MsiDmaLedger *l) {
    return msi_dma_all_released(l)&&!l->faulted&&!l->cleanup_failed;
}
static bool idle(const MsiDmaLedger *l) {
    MsiDmaInventory n;
    return msi_dma_inventory(l,&n)&&!l->violation&&!n.pending&&!n.unknown;
}
static MsiDmaResource *slot(MsiDmaLedger *l,unsigned id,MsiDmaState state) {
    /* An outstanding completion MUST still record outputs after a refused
     * reentrant attempt. Violation blocks new operations, not accounting. */
    if(!l||!l->initialized||l->used>MSI_DMA_MAX||
       !id||id>l->used||l->resource[id-1].state!=state) {
        (void)fail(l);return NULL;
    }
    return &l->resource[id-1];
}
static bool range(uint64_t base,uint64_t bytes,uint64_t limit) {
    return base&&!(base&4095)&&base<limit&&bytes&&bytes<=limit-base;
}
static bool overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn) {
    /* Called only after validated non-wrapping ranges. */
    return a<b+bn&&b<a+an;
}
bool msi_dma_alloc_begin(MsiDmaLedger *l,uint64_t bytes,unsigned *id) {
    if(id)*id=0;
    if(!l||!id||!idle(l)||l->faulted||l->cleanup_spent||l->used==MSI_DMA_MAX||
       !bytes||bytes>(UINT64_C(1)<<49)-4095)return fail(l);
    MsiDmaResource *r=&l->resource[l->used++];
    r->requested=bytes;r->pages=(bytes+4095)/4096;r->capacity=r->pages*4096;
    r->state=MSI_DMA_ALLOC_PENDING;*id=l->used;return true;
}
bool msi_dma_alloc_end(MsiDmaLedger *l,unsigned id,MsiDmaResult result,uint64_t host) {
    MsiDmaResource *r=slot(l,id,MSI_DMA_ALLOC_PENDING);if(!r)return false;
    r->host=host;
    bool valid=range(host,r->capacity,UINT64_MAX);
    for(unsigned i=0;i<l->used;i++)if(i!=id-1) {
        const MsiDmaResource *other=&l->resource[i];
        if(valid&&other->host&&overlap(host,r->capacity,other->host,other->capacity))valid=false;
    }
    if(result==MSI_DMA_OK&&valid){r->state=MSI_DMA_ALLOCATED;return !l->violation;}
    r->state=result==MSI_DMA_NO_EFFECT&&!host?MSI_DMA_NOT_ALLOCATED:MSI_DMA_UNKNOWN;
    l->faulted=true;return false;
}
bool msi_dma_map_begin(MsiDmaLedger *l,unsigned id) {
    if(!idle(l)||l->faulted||l->cleanup_spent)return fail(l);
    MsiDmaResource *r=slot(l,id,MSI_DMA_ALLOCATED);
    if(!r)return false;
    if(r->map_spent)return fail(l);
    r->map_spent=true;r->state=MSI_DMA_MAP_PENDING;return true;
}
bool msi_dma_map_end(MsiDmaLedger *l,unsigned id,MsiDmaResult result,
                     uint64_t device,uint64_t mapping,uint64_t bytes) {
    MsiDmaResource *r=slot(l,id,MSI_DMA_MAP_PENDING);if(!r)return false;
    /* Preserve every output even when the call failed or validation refuses it. */
    r->device=device;r->mapping=mapping;r->mapped_bytes=bytes;
    bool valid=bytes==r->capacity&&mapping&&range(device,bytes,UINT64_C(1)<<49);
    for(unsigned i=0;i<l->used;i++)if(i!=id-1) {
        const MsiDmaResource *other=&l->resource[i];
        if((mapping&&mapping==other->mapping)||
           (valid&&other->device&&overlap(device,bytes,other->device,other->mapped_bytes)))valid=false;
    }
    if(result==MSI_DMA_OK&&valid){r->state=MSI_DMA_MAPPED;return !l->violation;}
    r->state=result==MSI_DMA_NO_EFFECT&&!device&&!mapping&&!bytes?MSI_DMA_ALLOCATED:MSI_DMA_UNKNOWN;
    l->faulted=true;return false;
}
bool msi_dma_cleanup_begin(MsiDmaLedger *l,bool authorized) {
    if(!l||!l->initialized||l->cleanup_spent)return fail(l);
    l->cleanup_spent=true; /* spent before checking: never rearm */
    if(!authorized||!idle(l)||l->cleanup_failed)return fail(l);
    l->cleaning=true;return true;
}
static bool cleanup_allowed(MsiDmaLedger *l) {
    if(!l||!l->cleaning||!l->cleanup_spent||l->cleanup_failed||!idle(l))return fail(l);
    return true;
}
bool msi_dma_unmap_begin(MsiDmaLedger *l,unsigned id) {
    if(!cleanup_allowed(l))return false;
    MsiDmaResource *r=slot(l,id,MSI_DMA_MAPPED);if(!r)return false;
    if(r->unmap_spent)return fail(l);
    r->unmap_spent=true;r->state=MSI_DMA_UNMAP_PENDING;return true;
}
bool msi_dma_unmap_end(MsiDmaLedger *l,unsigned id,MsiDmaResult result) {
    MsiDmaResource *r=slot(l,id,MSI_DMA_UNMAP_PENDING);if(!r)return false;
    if(result==MSI_DMA_OK){r->state=MSI_DMA_UNMAPPED;l->unmaps++;return !l->violation;}
    r->state=result==MSI_DMA_NO_EFFECT?MSI_DMA_MAPPED:MSI_DMA_UNKNOWN;
    l->faulted=l->cleanup_failed=true;return false;
}
bool msi_dma_free_begin(MsiDmaLedger *l,unsigned id) {
    if(!cleanup_allowed(l))return false;
    if(!id||id>l->used)return fail(l);
    MsiDmaResource *r=&l->resource[id-1];
    if((r->state!=MSI_DMA_ALLOCATED&&r->state!=MSI_DMA_UNMAPPED)||r->free_spent)return fail(l);
    r->free_spent=true;r->free_from=r->state;r->state=MSI_DMA_FREE_PENDING;return true;
}
bool msi_dma_free_end(MsiDmaLedger *l,unsigned id,MsiDmaResult result) {
    MsiDmaResource *r=slot(l,id,MSI_DMA_FREE_PENDING);if(!r)return false;
    if(result==MSI_DMA_OK){r->state=MSI_DMA_FREED;l->frees++;return !l->violation;}
    r->state=result==MSI_DMA_NO_EFFECT?r->free_from:MSI_DMA_UNKNOWN;
    l->faulted=l->cleanup_failed=true;return false;
}
