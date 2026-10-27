/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../diagnostic/src/core.h"
#include "port.h"
#include "vbios_610.h"
#include <string.h>

static bool digest(const uint8_t *bytes,size_t count,const char *expected) {
    uint8_t hash[32];static const char hex[]="0123456789abcdef";
    Sha256(bytes,count,hash);
    for(size_t i=0;i<32;i++)if(hex[hash[i]>>4]!=expected[2*i]||hex[hash[i]&15]!=expected[2*i+1])return false;
    return true;
}
bool msi_capabilities(const uint32_t c[64]) {
    /* Exact captured chain, including versions/layout and vendor-cap length.
     * Four bounded steps; no guessed offsets or hidden extra capabilities. */
    static const unsigned offsets[]={MSI_PM,MSI_MSI,MSI_PCIE,0xb4};
    static const uint32_t headers[]={0x48036801,0x00807805,0x0012b410,0x01140009};
    if(!c||((c[3]>>16)&255)!=0x80||!(c[1]&(1u<<20))||c[13]!=MSI_PM)return false;
    unsigned next=c[13];
    for(unsigned i=0;i<4;i++) {
        if(next<0x40||next>0xfc||(next&3)||next!=offsets[i]||c[next/4]!=headers[i])return false;
        next=(c[next/4]>>8)&255;
    }
    return next==0;
}
bool msi_profile(const MsiObservation *o,const uint8_t *prom,size_t bytes) {
    static const uint32_t stock[22]={
        MSI_CHIP,0x8b8f,0x3ff,0x10,0x1ffffe00,0,0x4cb8f,0x4cb8f,0xffffff0f,0xffffff8f,
        0x233,0x410080,0,0,0x3ff8,1,0x45cb8f,0x100000,0x3cf7ff0,1,1,0xbadf5620};
    static const uint32_t bars[]={0x5e000000,0xc,0x60,0xc,0x64,0x5001};
    struct gpu_unlock_fwsec fw;
    if(!o||!prom||bytes!=1048576||o->segment||o->bus!=1||o->device||o->function||
       o->config[0]!=MSI_ID||o->config[11]!=MSI_SUBSYSTEM||o->config[2]!=0x030000a1||
       (o->config[1]&7)!=3||(o->config[12]&1)||
       o->bar0_base!=0x5e000000ull||o->bar0_bytes!=0x1000000||
       memcmp(o->config+4,bars,sizeof(bars))||!msi_capabilities(o->config))return false;
    if((o->config[(MSI_PM+4)/4]&3)||((o->config[MSI_MSI/4]>>16)&1)||
       !(o->config[(MSI_PCIE+4)/4]&(1u<<28))||
       (o->config[(MSI_PCIE+8)/4]&0x8000))return false;
    for(unsigned i=0;i<22;i++)
        if(i!=REG_SS0&&o->reg[i]!=stock[i])return false;
    /* Both observed values are admitted, no guessed bitmask broadening. */
    if(o->reg[REG_SS0]!=0x04003065&&o->reg[REG_SS0]!=0x04013065)return false;
    if(!digest(prom,bytes,"24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076")||
       gpu_unlock_vbios_fwsec(prom,bytes,&fw))return false;
    if(fw.image_off!=0x45788||fw.image_size!=0x10300||fw.imem_pa||fw.imem_va||fw.dmem_pa||
       fw.imem_size!=0xf500||fw.data_off!=0xf500||fw.dmem_size!=0xd80||fw.pkc_off!=0xb24||
       fw.interface_off!=0x1c||fw.ucode_id!=9||fw.engine_mask!=0x400||fw.sig_count!=2||
       fw.sig_versions!=3||fw.sig_off!=0x45488||fw.sig_bytes!=0x300)return false;
    return digest(prom+fw.image_off,fw.image_size,
        "fe5a76bee71e1545850f2883822c4a69dc678e187e05962f434029d1d9b6b568");
}
