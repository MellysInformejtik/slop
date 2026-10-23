#!/usr/bin/env python3
"""Exercise the patched RPMh transaction code against a fake ACTIVE TCS bank.

Usage: test_rpmh.py PATCHED_XNU/bsd/dev/negroni_rpmh.c
Only mapping, MMIO access and delays are replaced; the production lookup, ownership,
whitelist, command construction, completion and quarantine paths execute unchanged.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
start = source.index('static uint32_t\nrh_read(')
end = source.index('static bool\nrh_sync(', start)
source = source[:start] + '''static uint32_t rh_read(vm_offset_t b, uint32_t off) { return mock_read(b, off); }
static void rh_write(vm_offset_t b, uint32_t off, uint32_t v) { mock_write(b, off, v); }
''' + source[end:]

harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define ARM64_BOARD_CONFIG_NEGRONI 1
static uint32_t rsc[64], tcs[0x3000/4], db[0x20000/4];
static unsigned writes, submissions;
static bool complete_requests = true;
static uint32_t sent_addr[3], sent_value[3];
static unsigned sent_count;
static vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t size);
static uint32_t mock_read(vm_offset_t base, uint32_t off);
static void mock_write(vm_offset_t base, uint32_t off, uint32_t value);
static void delay(unsigned us) { (void)us; }
'''

tests = r'''
static vm_offset_t ml_io_map(vm_offset_t pa, vm_size_t size) {
    (void)size;
    if (pa == 0x17a20000) return (uintptr_t)rsc;
    if (pa == 0x17a20d00) return (uintptr_t)tcs;
    if (pa == 0x80860000) return (uintptr_t)db;
    assert(!"unexpected physical mapping");
    return 0;
}
static uint32_t mock_read(vm_offset_t base, uint32_t off) {
    assert(!(off & 3));
    return *(uint32_t *)(base + off);
}
static void mock_write(vm_offset_t base, uint32_t off, uint32_t value) {
    assert(base == (uintptr_t)tcs && off < sizeof(tcs));
    writes++;
    if (off == RH_IRQ_CLEAR) {
        tcs[RH_IRQ_STATUS/4] &= ~value;
        return;
    }
    tcs[off/4] = value;
    if (off % RH_TCS_STRIDE == RH_CONTROL && (value & RH_TRIGGER)) {
        unsigned b = off - RH_CONTROL, index = b / RH_TCS_STRIDE;
        submissions++;
        sent_count = 0;
        for (unsigned c=0; c<3; c++) if (tcs[(b+RH_ENABLE)/4] & (1u<<c)) {
            sent_addr[c] = tcs[(b+RH_ADDR+c*RH_CMD_STRIDE)/4];
            sent_value[c] = tcs[(b+RH_DATA+c*RH_CMD_STRIDE)/4];
            sent_count++;
            if (complete_requests) tcs[(b+RH_CMD_STATUS+c*RH_CMD_STRIDE)/4] = RH_ISSUED|RH_COMPLETE;
        }
        tcs[(b+RH_STATUS)/4] = complete_requests ? 1 : 0;
        if (complete_requests) tcs[RH_IRQ_STATUS/4] |= 1u << index;
    }
}
static void reset(void) {
    memset(rsc,0,sizeof(rsc)); memset(tcs,0,sizeof(tcs)); memset(db,0,sizeof(db));
    rh_rsc=rh_tcs=rh_db=0; rh_lock=0; rh_poisoned=false;
    writes=submissions=0; complete_requests=true;
    rsc[0]=2u<<16; rsc[0x0c/4]=(16u<<27)|(7u<<12);
    db[0]=1; db[1]=RH_DB_MAGIC;
    // slave 4, one entry starting immediately after the 144-byte header.
    ((uint16_t *)db)[4]=4; ((uint16_t *)db)[7]=1;
    memcpy((char *)db+144,"smpe2",5);
    db[(144+16)/4]=0x40500;
    for(unsigned t=0;t<RH_ACTIVE_N;t++) tcs[(t*RH_TCS_STRIDE+RH_STATUS)/4]=1;
}
int main(void) {
    reset();
    assert(negroni_rpmh_wifi_enable("smpe2",1013)==EINVAL && writes==0);
    assert(negroni_rpmh_wifi_enable("ldoc3",1012)==EINVAL && writes==0);
    assert(negroni_rpmh_wifi_enable(NULL,1012)==EINVAL && writes==0);
    // An inherited completed UEFI request is reusable, including its sticky IRQ.
    tcs[RH_CONTROL/4]=RH_AMC_EN; tcs[RH_ENABLE/4]=3; tcs[RH_WAIT/4]=3;
    tcs[RH_CMD_STATUS/4]=RH_ISSUED|RH_COMPLETE;
    tcs[(RH_CMD_STATUS+RH_CMD_STRIDE)/4]=RH_ISSUED|RH_COMPLETE;
    tcs[RH_IRQ_STATUS/4]=1;
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==0);
    assert(submissions==1 && sent_count==2);
    assert(sent_addr[0]==0x40500 && sent_value[0]==1012);
    assert(sent_addr[1]==0x40504 && sent_value[1]==1); // no touch-LDO mode write
    assert(tcs[RH_CONTROL/4]==0 && tcs[RH_ENABLE/4]==0 && tcs[RH_WAIT/4]==0);
    assert(!(tcs[RH_IRQ_STATUS/4]&1) && !rh_poisoned);
    // Pending transactions must not be mistaken for reusable inherited commands.
    reset();
    for(unsigned t=0;t<RH_ACTIVE_N;t++) tcs[(t*RH_TCS_STRIDE+RH_STATUS)/4]=0;
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==EBUSY && writes==0);
    reset();
    for(unsigned t=0;t<RH_ACTIVE_N;t++) {
        unsigned b=t*RH_TCS_STRIDE;
        tcs[(b+RH_ENABLE)/4]=1;
        tcs[(b+RH_CMD_STATUS)/4]=RH_ISSUED;
    }
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==EBUSY && writes==0);
    // Timeout quarantines the bank; a subsequent vote cannot overwrite it.
    reset(); complete_requests=false;
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==ETIMEDOUT && rh_poisoned);
    unsigned before=writes;
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==EIO && writes==before);
    reset(); db[1]=0;
    assert(negroni_rpmh_wifi_enable("smpe2",1012)==EINVAL && writes==0);
    puts("RPMh ownership, supply policy, completion and quarantine tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix='negroni-rpmh-test-') as temp:
    path = Path(temp) / 'test.c'
    path.write_text(harness + source + tests)
    executable = Path(temp) / 'test'
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(path), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
