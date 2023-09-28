// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <unistd.h>
#include <cstdint>

#include "risc.h"
#include "risc_common.h"
#include "noc_overlay_parameters.h"
#include "ckernel_structs.h"
#include "stream_io_map.h"
#include "c_tensix_core.h"
#include "tdma_xmov.h"
#include "noc_nonblocking_api.h"
#include "ckernel_globals.h"
#include "tools/profiler/kernel_profiler.hpp"
#include "dataflow_api.h"
#include "debug_print.h"
#include "noc_addr_ranges_gen.h"

#include <kernel.cpp>

#include "debug_print.h"

CBInterface cb_interface[NUM_CIRCULAR_BUFFERS];
CQReadInterface cq_read_interface;

#ifdef NOC_INDEX
uint8_t loading_noc = NOC_INDEX;
#else
uint8_t loading_noc = 0;
#endif

// to reduce the amount of code changes, both BRISC and NRISCS instatiate these counter for both NOCs (ie NUM_NOCS)
// however, atm NCRISC uses only NOC-1 and BRISC uses only NOC-0
// this way we achieve full separation of counters / cmd_buffers etc.
uint32_t noc_reads_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_acked[NUM_NOCS];

// dram channel to x/y lookup tables
// The number of banks is generated based off device we are running on --> controlled by allocator
uint8_t dram_bank_to_noc_x[NUM_DRAM_BANKS];
uint8_t dram_bank_to_noc_y[NUM_DRAM_BANKS];
uint32_t dram_bank_to_noc_xy[NUM_DRAM_BANKS];

uint8_t l1_bank_to_noc_x[NUM_L1_BANKS];
uint8_t l1_bank_to_noc_y[NUM_L1_BANKS];
uint32_t l1_bank_to_noc_xy[NUM_L1_BANKS];

extern uint64_t dispatch_addr;
extern uint8_t kernel_noc_id_var;

void kernel_launch() {

    firmware_kernel_common_init((void tt_l1_ptr *)MEM_BRISC_INIT_LOCAL_L1_BASE);

#if defined(IS_DISPATCH_KERNEL)
    setup_cq_read_write_interface();
#else
    setup_cb_read_write_interfaces();                // done by both BRISC / NCRISC
#endif

    init_dram_bank_to_noc_coord_lookup_tables();  // done by both BRISC / NCRISC
    init_l1_bank_to_noc_coord_lookup_tables();  // done by both BRISC / NCRISC

    noc_init(loading_noc);

    kernel_profiler::mark_time(CC_KERNEL_MAIN_START);
    kernel_main();
    kernel_profiler::mark_time(CC_KERNEL_MAIN_END);

    // FW needs to notify device dispatcher when we are done
    // Some information needed is known here, pass it back
    kernel_noc_id_var = loading_noc;

#if defined(TT_METAL_SLOW_DISPATCH_MODE)
    dispatch_addr = 0;
#else
    dispatch_addr = (my_x[loading_noc] == NOC_X(DISPATCH_CORE_X) && my_y[loading_noc] == NOC_Y(DISPATCH_CORE_Y)) ?
        0 :
        get_noc_addr(DISPATCH_CORE_X, DISPATCH_CORE_Y, DISPATCH_MESSAGE_ADDR);
#endif
}
