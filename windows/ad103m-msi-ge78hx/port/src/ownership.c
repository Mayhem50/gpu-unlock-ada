/* SPDX-License-Identifier: GPL-2.0-only */
#include "port.h"
#include <string.h>

static const MsiEdge expected[]={
    {NODE_PCI,DRIVER_NVIDIA,NODE_GOP,PROTO_PATH,8,1},
    {NODE_PCI,DRIVER_NVIDIA,NODE_PCI,PROTO_PCI,16,1},
    {NODE_GOP,DRIVER_TEXT,NODE_GOP,PROTO_TEXT,16,1},
    {NODE_GOP,DRIVER_TEXT,NODE_CONSOLE,PROTO_TEXT,8,1},
    {NODE_GOP,DRIVER_GRAPHICS,NODE_GOP,PROTO_GOP,16,1}
};
bool msi_graph_stage(const MsiGraph *baseline,const MsiGraph *now,unsigned stage,bool exclusive) {
    if(!baseline||!now||stage>3||now->count>8||!now->identities_verified||
       !now->console_alive||now->exclusive!=exclusive||
       memcmp(baseline->boundary,now->boundary,32)||
       memcmp(baseline->console,now->console,32)||memcmp(baseline->logging,now->logging,32))return false;
    unsigned required=0;
    for(unsigned i=0;i<5;i++) {
        const MsiEdge *e=&expected[i];
        bool removed=(stage>=1&&e->agent==DRIVER_TEXT)||(stage>=2&&e->agent==DRIVER_GRAPHICS)||
                     (stage>=3&&e->agent==DRIVER_NVIDIA);
        unsigned matches=0;
        for(unsigned j=0;j<now->count;j++)if(!memcmp(e,&now->edges[j],sizeof(*e)))matches++;
        if(matches!=(removed?0u:1u))return false;
        if(!removed)required++;
    }
    return now->count==required;
}
