#pragma once

#include <functional>
#include <iostream>
#include <random>
#include <tuple>
#include <utility>
#include <variant>

#include "tools/profiler/profiler.hpp"
#include "hostdevcommon/common_runtime_address_map.h"
#include "hostdevcommon/registers.hpp"
#include "tt_metal/impl/allocator/allocator.hpp"
#include "tt_metal/impl/buffers/buffer.hpp"
#include "tt_metal/impl/buffers/circular_buffer.hpp"
#include "tt_metal/impl/buffers/semaphore.hpp"
#include "tt_metal/impl/device/device.hpp"
#include "tt_metal/impl/device/host.hpp"
#include "tt_metal/impl/kernels/kernel.hpp"
#include "tt_metal/impl/program.hpp"
#include "llrt/llrt.hpp"

/** @file */

/** \mainpage tt-metal Internal C++ Documentation
 *
 * Welcome. Please navigate using the Files menu. All APIs are documented
 * under the files listed in the Files menu.
 *
 * If you want to contribute to the documentation and are looking for a good
 * resource for generating Markdown tables, refer to
 * https://www.tablesgenerator.com/markdown_tables.
 * */

namespace tt {

namespace tt_metal {

// ==================================================
//                  HOST API: profiler
// ==================================================

/**
 * Dump host side profiler results into the host side CSV log
 *
 * Return value: void
 *
 * | Argument       | Description                                                                  |  Data type  | Valid range  | required |
 * |----------------|------------------------------------------------------------------------------|-------------|--------------|----------|
 * | name_prepend   | The name or description to be prepended to all rows in the CSV for this dump | std::string | Any string   | No       |
 * */
void DumpHostProfileResults(std::string name_prepend = "");

/**
 * Read device side profiler data and dump results into device side CSV log
 *
 * Return value: void
 *
 * | Argument      | Description                                       | Type       | Valid Range                                            | Required |
 * |---------------|---------------------------------------------------|------------|--------------------------------------------------------|----------|
 * | device        | The device where the L1 buffer resides.           | Device *   |                                                        | True     |
 * | program       | The program to which buffer will be added to.     | Program *  |                                                        | True     |
 * */
void DumpDeviceProfileResults(Device *device, Program *program);

/**
 * Set the directory for all CSV logs produced by the profiler instance in the tt-metal module
 *
 * Return value: void
 *
 * | Argument     | Description                                             |  Data type  | Valid range              | required |
 * |--------------|---------------------------------------------------------|-------------|--------------------------|----------|
 * | output_dir   | The output directory that will hold the outpu CSV logs  | std::string | Any valid directory path | No       |
 * */
void SetProfilerDir(std::string output_dir = "");

/**
 * Start a fresh log for the host side profile results
 *
 * Return value: void
 *
 * | Argument     | Description                                             |  Data type  | Valid range              | required |
 * |--------------|---------------------------------------------------------|-------------|--------------------------|----------|
 * */
void FreshProfilerHostLog();

/**
 * Start a fresh log for the device side profile results
 *
 * Return value: void
 *
 * | Argument     | Description                                             |  Data type  | Valid range              | required |
 * |--------------|---------------------------------------------------------|-------------|--------------------------|----------|
 * */
void FreshProfilerDeviceLog();


// ==================================================
//                  HOST API: host and device
// ==================================================
Host *GetHost();

/**
 * Instantiates a device object.
 *
 * Return value: Device *
 *
 * | Argument       | Description                                                      | Data type | Valid range                                         | required |
 * |----------------|------------------------------------------------------------------|-----------|-----------------------------------------------------|----------|
 * | device_type    | Type of Tenstorrent device to be used                            | ARCH enum | “tt::ARCH::GRAYSKULL”                               | Yes      |
 * | pcie_slot      | The number of the PCIexpress slot in which the device is located | int       | 0 to 7                                              | Yes      |
 * */
Device *CreateDevice(tt::ARCH arch, int pcie_slot);

/**
 * Initializes a device by creating a tt_cluster object and memory manager. Puts device into reset.
 *
 * Currently device has a 1:1 mapping with tt_cluster and memory manager only allocates DRAM addresses.
 *
 * Return value: bool
 *
 * | Argument                    | Description                                 | Type                 | Valid Range         | Required |
 * |-----------------------------|---------------------------------------------|----------------------|---------------------|----------|
 * | device                      | Pointer to device object                    | Device *             |                     | Yes      |
 * | memory_allocator            | Type of memory allocator scheme to use      | MemoryAllocator enum | BASIC or L1_BANKING | No       |
 */
bool InitializeDevice(Device *device, const MemoryAllocator &memory_allocator = MemoryAllocator::BASIC);

/**
 * Resets device and closes device
 *
 * Return value: bool
 *
 * | Argument | Description                | Type     | Valid Range | Required |
 * |----------|----------------------------|----------|-------------|----------|
 * | device   | Pointer to a device object | Device * |             | True     |
 */
bool CloseDevice(Device *device);

void StartDebugPrintServer(Device *device);

// ==================================================
//                  HOST API: program & kernels
// ==================================================
// Kernel args are only initialized with compile time arguments

/**
 * Creates kernel arguments for a data movement kernel
 *
 * Return value: DataMovementKernelArgs *
 *
 * |      Argument     |                                                      Description                                                      |        Data type        |    Valid range    | required |
 * |:-----------------:|:---------------------------------------------------------------------------------------------------------------------:|:-----------------------:|:-----------------:|----------|
 * | logical_core      | The location of the Tensix core with a kernel that receives these arguments (Logical co-ordinates)                    | const tt_xy_pair &      | {0, 0} –> {9, 11} | Yes      |
 * | runtime_args      | Collection of runtime args for the kernel. Required kernel arguments are located in the *.cpp file of the kernel      | std::vector<uint32_t> & |                   | Yes      |
 * | compile_time_args | Collection of compile time args for the kernel. Required kernel arguments are located in the *.cpp file of the kernel | std::vector<uint32_t> & | Default empty     | Yes      |
 */
DataMovementKernelArgs *InitializeCompileTimeDataMovementKernelArgs(const tt_xy_pair &logical_core, const std::vector<uint32_t> &compile_time_args);

/**
 * Creates the same kernel arguments for a range of cores
 *
 * Return value: DataMovementKernelArgs *
 *
 * | Argument          | Description                                                                                                           | Data type                                             | Valid range                                            | required |
 * |-------------------|-----------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------|--------------------------------------------------------|----------|
 * | core_range        | The range of the Tensix co-ordinates with a kernel that receives these arguments (Logical co-ordinates)               | const CoreRange & (std::pair<tt_xy_pair, tt_xy_pair>) | Any range encompassing cores within {0 , 0} –> {9, 11} | Yes      |
 * | runtime_args      | Collection of runtime args for the kernel. Required kernel arguments are located in the *.cpp file of the kernel      | std::vector<uint32_t> &                               |                                                        | Yes      |
 * | compile_time_args | Collection of compile time args for the kernel. Required kernel arguments are located in the *.cpp file of the kernel | std::vector<uint32_t> &                               | Default empty                                          | Yes      |
 */
DataMovementKernelArgs *InitializeCompileTimeDataMovementKernelArgs(const CoreRange &core_range, const std::vector<uint32_t> &compile_time_args);

/**
 * Creates kernel arguments specified by a combination of single core co-ordinates or a range of core co-ordinates
 *
 * Return value: DataMovementKernelArgs *
 *
 * |      Argument     |                                                                 Description                                                                |                               Data type                               |                                                      Valid range                                                      | required |
 * |:-----------------:|:------------------------------------------------------------------------------------------------------------------------------------------:|:---------------------------------------------------------------------:|:---------------------------------------------------------------------------------------------------------------------:|----------|
 * | core_blocks       | A collection containing a single Tensix co-ordinate or a range of Tensix co-ordinates that receives these arguments (Logical co-ordinates) | const CoreBlocks & (std::vector<std::variant<tt_xy_pair, CoreRange>>) | A single core or range encompassing cores within {0 , 0} –> {9, 11}                                                   | Yes      |
 * | runtime_args      | A collection of runtime args. Required kernel arguments are located in the *.cpp file of the kernel                                        | std::vector<std::vector<uint32_t>> &                                  | Same size as core_blocks. Args are assigned to core or range of cores from core_blocks in order of runtime_args.      | Yes      |
 * | compile_time_args | A collection of compile time args. Required kernel arguments are located in the *.cpp file of the kernel                                   | std::vector<std::vector<uint32_t>> &                                  | Same size as core_blocks. Args are assigned to core or range of cores from core_blocks in order of compile_time_args. | Yes      |
 */
DataMovementKernelArgs *InitializeCompileTimeDataMovementKernelArgs(const CoreBlocks &core_blocks, const std::vector<std::vector<uint32_t>> &compile_time_args_spec);

/**
 * Creates kernel arguments for compute kernel
 *
 * Return value: ComputeKernelArgs *
 *
 * |        Argument        |                                                Description                                                |      Data type     |    Valid range    | required |
 * |:----------------------:|:---------------------------------------------------------------------------------------------------------:|:------------------:|:-----------------:|----------|
 * | logical_core           | The location of the Tensix core with a kernel that receives these arguments (Logical co-ordinates)        | const tt_xy_pair & | {0, 0} –> {9, 11} | Yes      |
 * | compile_time_args      | A pointer to the struct containing the args. Struct definition is located in the *.cpp file of the kernel | void *             |                   | Yes      |
 * | compile_time_args_size | Size of struct containing the kernel arguments                                                            | size_t             | 0 to 512 Bytes    | Yes      |
 */
ComputeKernelArgs *InitializeCompileTimeComputeKernelArgs(const tt_xy_pair &logical_core, const vector<uint32_t> &compile_time_args);

/**
 * Creates the same kernel arguments for a range of cores
 *
 * Return value: ComputeKernelArgs *
 *
 * |        Argument        |                                                Description                                                |                       Data type                       |                       Valid range                      | required |
 * |:----------------------:|:---------------------------------------------------------------------------------------------------------:|:-----------------------------------------------------:|:------------------------------------------------------:|----------|
 * | core_range             | The range of the Tensix co-ordinates with a kernel that receives these arguments (Logical co-ordinates)   | const CoreRange & (std::pair<tt_xy_pair, tt_xy_pair>) | Any range encompassing cores within {0 , 0} –> {9, 11} | Yes      |
 * | compile_time_args      | A pointer to the struct containing the args. Struct definition is located in the *.cpp file of the kernel | void *                                                |                                                        | Yes      |
 * | compile_time_args_size | Size of struct containing the kernel arguments                                                            | size_t                                                | 0 to 512 Bytes                                         | Yes      |
 */
ComputeKernelArgs *InitializeCompileTimeComputeKernelArgs(const CoreRange &core_range, const vector<uint32_t> &compile_time_args);

/**
 * Creates kernel arguments specified by a combination of single core co-ordinates or a range of core co-ordinates
 *
 * Return value: ComputeKernelArgs *
 *
 * | Argument               | Description                                                                                                                                | Data type                                                             | Valid range                                                                                                           | required |
 * |------------------------|--------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------|----------|
 * | core_blocks            | A collection containing a single Tensix co-ordinate or a range of Tensix co-ordinates that receives these arguments (Logical co-ordinates) | const CoreBlocks & (std::vector<std::variant<tt_xy_pair, CoreRange>>) | A single core or range encompassing cores within {0 , 0} –> {9, 11}                                                   | Yes      |
 * | compile_time_args      | A collection of pointers to structs containing the args. Struct definition is located in the *.cpp file of the kernel.                     | const std::vector<void *> &                                           | Same size as core_blocks. Args are assigned to core or range of cores from core_blocks in order of compile_time_args. | Yes      |
 * | compile_time_args_size | Size of struct containing the kernel arguments                                                                                             | size_t                                                                | 0 to 512 Bytes                                                                                                        | Yes      |
 */
ComputeKernelArgs *InitializeCompileTimeComputeKernelArgs(const CoreBlocks &core_blocks, const std::vector<std::vector<uint32_t>> &compile_time_args_spec);
/**
 * Creates a single core data movement kernel and adds it to the program.
 *
 * Return value: DataMovementKernel *
 *
 * | Argument       | Description                                                                                                  | Data type                | Valid range                                                    | required |
 * |----------------|--------------------------------------------------------------------------------------------------------------|--------------------------|----------------------------------------------------------------|----------|
 * | program        | The program to which this kernel will be added to                                                            | Program *                |                                                                | Yes      |
 * | file_name      | Name of file containing the kernel                                                                           | const std::string        |                                                                | Yes      |
 * | core           | The location of the Tensix core on which the kernel will execute (Logical co-ordinates)                      | const tt_xy_pair &       | {0, 0} –> {9, 11}                                              | Yes      |
 * | kernel_args    | Compile and runtime kernel arguments passed at compile time and runtime respectively                         | DataMovementKernelArgs * |                                                                | Yes      |
 * | processor_type | The target RISC-V processor on which the kernel will execute, on the given Tensix core (1 kernel per RISC-V) | enum                     | DataMovementProcessor::RISCV_0, DataMovementProcessor::RISCV_1 | Yes      |
 * | noc            | The NoC ID on which the kernel will perform data transfers                                                   | enum                     | RISCV_0_default, RISCV_1_default, NOC_0, NOC_1,                | Yes      |
 */
DataMovementKernel *CreateDataMovementKernel(
    Program *program,
    const std::string &file_name,
    const tt_xy_pair &core,
    DataMovementKernelArgs *kernel_args,
    DataMovementProcessor processor_type,
    NOC noc);

/**
 * Creates a single core data movement kernel with no default arguments and
 * adds it to the program.
 *
 * Return value: DataMovementKernel *
 *
 * | Argument       | Description                                                                                                  | Data type                | Valid range                                                    | required |
 * |----------------|--------------------------------------------------------------------------------------------------------------|--------------------------|----------------------------------------------------------------|----------|
 * | program        | The program to which this kernel will be added to                                                            | Program *                |                                                                | Yes      |
 * | file_name      | Name of file containing the kernel                                                                           | const std::string        |                                                                | Yes      |
 * | core           | The location of the Tensix core on which the kernel will execute (Logical co-ordinates)                      | const tt_xy_pair &       | {0, 0} –> {9, 11}                                              | Yes      |
 * | processor_type | The target RISC-V processor on which the kernel will execute, on the given Tensix core (1 kernel per RISC-V) | enum                     | DataMovementProcessor::RISCV_0, DataMovementProcessor::RISCV_1 | Yes      |
 * | noc            | The NoC ID on which the kernel will perform data transfers                                                   | enum                     | RISCV_0_default, RISCV_1_default, NOC_0, NOC_1,                | Yes      |
 */
DataMovementKernel *CreateDataMovementKernel(
    Program *program,
    const std::string &file_name,
    const tt_xy_pair &core,
    DataMovementProcessor processor_type,
    NOC noc);

/**
 * Creates a multi-core data movement kernel and adds it to the program.
 *
 * Return value: DataMovementKernel *
 *
 * | Argument       | Description                                                                                                  | Data type                | Valid range                                                    | required |
 * |----------------|--------------------------------------------------------------------------------------------------------------|--------------------------|----------------------------------------------------------------|----------|
 * | program        | The program to which this kernel will be added to                                                            | Program *                |                                                                | Yes      |
 * | file_name      | Name of file containing the kernel                                                                           | const std::string        |                                                                | Yes      |
 * | core_range     | The range of the Tensix co-ordinates on which the kernel will execute (Logical co-ordinates)                 | const CoreRange &        | Any range encompassing cores within {0 , 0} –> {9, 11}         | Yes      |
 * | kernel_args    | Compile and runtime kernel arguments passed at compile time and runtime respectively                         | DataMovementKernelArgs * |                                                                | Yes      |
 * | processor_type | The target RISC-V processor on which the kernel will execute, on the given Tensix core (1 kernel per RISC-V) | enum                     | DataMovementProcessor::RISCV_0, DataMovementProcessor::RISCV_1 | Yes      |
 * | noc            | The NoC ID on which the kernel will perform data transfers                                                   | enum                     | RISCV_0_default, RISCV_1_default, NOC_0, NOC_1,                | Yes      |
 */
DataMovementKernel *CreateDataMovementKernel(
    Program *program,
    const std::string &file_name,
    const CoreRange &core_range,
    DataMovementKernelArgs *kernel_args,
    DataMovementProcessor processor_type,
    NOC noc);

/**
 * Creates a multi-core data movement kernel with no default arguments and adds
 * it to the program.
 *
 * Return value: DataMovementKernel *
 *
 * | Argument       | Description                                                                                                  | Data type                | Valid range                                                    | required |
 * |----------------|--------------------------------------------------------------------------------------------------------------|--------------------------|----------------------------------------------------------------|----------|
 * | program        | The program to which this kernel will be added to                                                            | Program *                |                                                                | Yes      |
 * | file_name      | Name of file containing the kernel                                                                           | const std::string        |                                                                | Yes      |
 * | core_range     | The range of the Tensix co-ordinates on which the kernel will execute (Logical co-ordinates)                 | const CoreRange &        | Any range encompassing cores within {0 , 0} –> {9, 11}         | Yes      |
 * | processor_type | The target RISC-V processor on which the kernel will execute, on the given Tensix core (1 kernel per RISC-V) | enum                     | DataMovementProcessor::RISCV_0, DataMovementProcessor::RISCV_1 | Yes      |
 * | noc            | The NoC ID on which the kernel will perform data transfers                                                   | enum                     | RISCV_0_default, RISCV_1_default, NOC_0, NOC_1,                | Yes      |
 */
DataMovementKernel *CreateDataMovementKernel(
    Program *program,
    const std::string &file_name,
    const CoreRange &core_range,
    DataMovementProcessor processor_type,
    NOC noc);

/**
 * Creates a single core compute kernel object, and adds it to the program.
 *
 * Return value: ComputeKernel *
 *
 * |     Argument     |                                       Description                                       |      Data type      |      Valid range      | required |
 * |:----------------:|:---------------------------------------------------------------------------------------:|:-------------------:|:---------------------:|----------|
 * | program          | The program to which this kernel will be added to                                       | Program *           |                       | Yes      |
 * | file_name        | Name of file containing the kernel                                                      | const std::string   |                       | Yes      |
 * | core             | The location of the Tensix core on which the kernel will execute (Logical co-ordinates) | const tt_xy_pair &  | {0, 0} –> {9, 11}     | Yes      |
 * | kernel_args      | Kernel arguments, passed at compile time                                                | ComputeKernelArgs * |                       | Yes      |
 * | math_fidelity    | The percision of the matrix compute engine                                              | enum                | MathFidelity::HiFi4   | Yes      |
 * | fp32_dest_acc_en | Specifies the type of accumulation performed in the matrix compute engine.              | bool                | false (for Grayskull) | Yes      |
 * | math_approx_mode | Used by the vector compute engine. (will be depricated)                                 | bool                | true, false           | Yes      |
 */
ComputeKernel *CreateComputeKernel(
    Program *program,
    const std::string &file_name,
    const tt_xy_pair &core,
    ComputeKernelArgs *kernel_args,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    bool math_approx_mode);

/**
 * Creates a multi-core compute kernel object, and adds it to the program.
 *
 * Return value: ComputeKernel *
 *
 * | Argument         | Description                                                                                  | Data type           | Valid range                                            | required |
 * |------------------|----------------------------------------------------------------------------------------------|---------------------|--------------------------------------------------------|----------|
 * | program          | The program to which this kernel will be added to                                            | Program *           |                                                        | Yes      |
 * | file_name        | Name of file containing the kernel                                                           | const std::string   |                                                        | Yes      |
 * | core_range       | The range of the Tensix co-ordinates on which the kernel will execute (Logical co-ordinates) | const CoreRange &   | Any range encompassing cores within {0 , 0} –> {9, 11} | Yes      |
 * | kernel_args      | Kernel arguments, passed at compile time                                                     | ComputeKernelArgs * |                                                        | Yes      |
 * | math_fidelity    | The percision of the matrix compute engine                                                   | enum                | MathFidelity::HiFi4                                    | Yes      |
 * | fp32_dest_acc_en | Specifies the type of accumulation performed in the matrix compute engine.                   | bool                | false (for Grayskull)                                  | Yes      |
 * | math_approx_mode | Used by the vector compute engine. (will be depricated)                                      | bool                | true, false                                            | Yes      |
 */
ComputeKernel *CreateComputeKernel(
    Program *program,
    const std::string &file_name,
    const CoreRange &core_range,
    ComputeKernelArgs *kernel_args,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    bool math_approx_mode);

// ==================================================
//                  HOST API: data format
// ==================================================
/**
 * Returns datum size of given data format in bytes
 *
 * Return value: uint32_t
 *
 * | Argument    | Description    | Type                | Valid Range | Required |
 * |-------------|----------------|---------------------|-------------|----------|
 * | data_format | Format of data | tt::DataFormat enum |             | Yes      |
 */
uint32_t DatumSize(const DataFormat &data_format);

/**
 * Returns tile size of given data format in bytes
 *
 * Return value: uint32_t
 *
 * | Argument    | Description    | Type                | Valid Range | Required |
 * |-------------|----------------|---------------------|-------------|----------|
 * | data_format | Format of data | tt::DataFormat enum |             | Yes      |
 */
uint32_t TileSize(const DataFormat &data_format);

// ==================================================
//                  HOST API: buffers
// ==================================================
/**
 * Creates a Circular Buffer (CBs) in L1 memory at specified address and core and adds it to the program. L1 allocator does not track CBs with manually specified addresses.
 * Warning: Do not mix use of this API with CB creation APIs that are tracked by allocator since there is no gurantee that buffer space will not be overwritten.
 *
 * Return value: CircularBuffer *
 *
 * | Argument      | Description                                                                    | Type               | Valid Range                             | Required |
 * |---------------|--------------------------------------------------------------------------------|--------------------|-----------------------------------------|----------|
 * | program       | The program to which buffer will be added to.                                  | Program *          |                                         | True     |
 * | device        | The device where the L1 buffer resides.                                        | Device *           |                                         | True     |
 * | buffer_index  | The index/ID of the CB.                                                        | uint32_t           | 0 to 32 DOX-TODO: specify more detail here. | True     |
 * | core          | The location of the Tensix core on which the CB will reside (logical co-ordinates) | const tt_xy_pair & | DOX-TODO: { , } –> { , }                    | True     |
 * | num_tiles     | Total number of tiles to be stored in the CB                                   | uint32_t           | DOX-TODO: range?                            | True     |
 * | size_in_bytes | Size of CB buffer in Bytes                                                     | uint32_t           | 0 to 1 MB (DOX-TODO: in Bytes)              | True     |
 * | l1_address    | Address at which the CB buffer will reside                                     | uint32_t           | 200 kB to 1MB (DOX-TODO: in bytes)          | True     |
 * | data_format   | The format of the data to be stored in the CB                                  | DataFormat enum    | DataFormat::Float16_b                   | True     |
 */
CircularBuffer *CreateCircularBuffer(
    Program *program,
    Device *device,
    uint32_t buffer_index,
    const tt_xy_pair &core,
    uint32_t num_tiles,
    uint32_t size_in_bytes,
    uint32_t l1_address,
    DataFormat data_format);

/**
 * Allocates and creates Circular Buffers (CBs) in L1 memory of specified core and adds it to the program. L1 allocator is responsible for generating address.
 * Warning: Do not mix use of this API with CB creation APIs that are not tracked by allocator since there is no gurantee that buffer space will not be overwritten.
 *
 * Return value: CircularBuffer *
 *
 * | Argument      | Description                                                                    | Type               | Valid Range                             | Required |
 * |---------------|--------------------------------------------------------------------------------|--------------------|-----------------------------------------|----------|
 * | program       | The program to which buffer will be added to.                                  | Program *          |                                         | True     |
 * | device        | The device where the L1 buffer resides.                                        | Device *           |                                         | True     |
 * | buffer_index  | The index/ID of the CB.                                                        | uint32_t           | 0 to 32 DOX-TODO: specify more detail here. | True     |
 * | core          | The location of the Tensix core on which the CB will reside (logical co-ordinates) | const tt_xy_pair & | DOX-TODO: { , } –> { , }                    | True     |
 * | num_tiles     | Total number of tiles to be stored in the CB                                   | uint32_t           | DOX-TODO: range?                            | True     |
 * | size_in_bytes | Size of CB buffer in Bytes                                                     | uint32_t           | 0 to 1 MB (DOX-TODO: in Bytes)              | True     |
 * | data_format   | The format of the data to be stored in the CB                                  | DataFormat enum    | DataFormat::Float16_b                   | True     |
 */
CircularBuffer *CreateCircularBuffer(
    Program *program,
    Device *device,
    uint32_t buffer_index,
    const tt_xy_pair &core,
    uint32_t num_tiles,
    uint32_t size_in_bytes,
    DataFormat data_format);

/**
 * Creates Circular Buffers (CBs) in L1 memory of all cores within core range (inclusive) at specified address and adds it to the program. L1 allocator does not track CBs with manually specified addresses.
 * Warning: Do not mix use of this API with CB creation APIs that are tracked by allocator since there is no gurantee that buffer space will not be overwritten.
 *
 * Return value: std::vector<CircularBuffer *>
 *
 * | Argument      | Description                                                                    | Type               | Valid Range                             | Required |
 * |---------------|--------------------------------------------------------------------------------|--------------------|-----------------------------------------|----------|
 * | program       | The program to which buffer will be added to.                                  | Program *          |                                         | True     |
 * | device        | The device where the L1 buffer resides.                                        | Device *           |                                         | True     |
 * | buffer_index  | The index/ID of the CB.                                                        | uint32_t           | 0 to 32 DOX-TODO: specify more detail here. | True     |
 * | core_range    | Range of the Tensix co-ordinates where buffer will reside (Logical co-ordinates)  | const CoreRange & (std::pair<tt_xy_pair, tt_xy_pair>) | DOX-TODO: { , } –> { , }                    | True     |
 * | num_tiles     | Total number of tiles to be stored in the CB                                   | uint32_t           | DOX-TODO: range?                            | True     |
 * | size_in_bytes | Size of CB buffer in Bytes                                                     | uint32_t           | 0 to 1 MB (DOX-TODO: in Bytes)              | True     |
 * | l1_address    | Address at which the CB buffer will reside                                     | uint32_t           | 200 kB to 1MB (DOX-TODO: in bytes)          | True     |
 * | data_format   | The format of the data to be stored in the CB                                  | DataFormat enum    | DataFormat::Float16_b                   | True     |
 */
std::vector<CircularBuffer *> CreateCircularBuffers(
    Program *program,
    Device *device,
    uint32_t buffer_index,
    const CoreRange &core_range,
    uint32_t num_tiles,
    uint32_t size_in_bytes,
    uint32_t l1_address,
    DataFormat data_format);

/**
 * Creates Circular Buffer (CB) per core within core range (inclusive). CBs will be allocated to the same address on their respective cores, an error is returned is this is not possible.
 * Warning: Do not mix use of this API with CB creation APIs that are not tracked by allocator since there is no gurantee that buffer space will not be overwritten.
 *
 * Return value: CircularBuffer *
 *
 * | Argument      | Description                                                                    | Type               | Valid Range                             | Required |
 * |---------------|--------------------------------------------------------------------------------|--------------------|-----------------------------------------|----------|
 * | program       | The program to which buffer will be added to.                                  | Program *          |                                         | True     |
 * | device        | The device where the L1 buffer resides.                                        | Device *           |                                         | True     |
 * | buffer_index  | The index/ID of the CB.                                                        | uint32_t           | 0 to 32 DOX-TODO: specify more detail here. | True     |
 * | core_range    | Range of the Tensix co-ordinates where buffer will reside (Logical co-ordinates)  | const CoreRange & (std::pair<tt_xy_pair, tt_xy_pair>) | DOX-TODO: { , } –> { , }                    | True     |
 * | num_tiles     | Total number of tiles to be stored in the CB                                   | uint32_t           | DOX-TODO: range?                            | True     |
 * | size_in_bytes | Size of CB buffer in Bytes                                                     | uint32_t           | 0 to 1 MB (DOX-TODO: in Bytes)              | True     |
 * | data_format   | The format of the data to be stored in the CB                                  | DataFormat enum    | DataFormat::Float16_b                   | True     |
 */
std::vector<CircularBuffer *> CreateCircularBuffers(
    Program *program,
    Device *device,
    uint32_t buffer_index,
    const CoreRange &core_range,
    uint32_t num_tiles,
    uint32_t size_in_bytes,
    DataFormat data_format);

/**
 * Initializes semaphore on all cores within core range (inclusive). Each core can have up to four 32B semaphores.
 *
 * Return value: std::vector<Semaphore *>
 *
 * | Argument      | Description                                          | Type                                                  | Valid Range                                              | Required |
 * |---------------|------------------------------------------------------|-------------------------------------------------------|----------------------------------------------------------|----------|
 * | program       | The program to which semaphore will be added to      | Program *                                             |                                                          | Yes      |
 * | device        | The device where the semaphore resides               | Device *                                              |                                                          | Yes      |
 * | core_range    | Range of the Tensix co-ordinates using the semaphore | const CoreRange & (std::pair<tt_xy_pair, tt_xy_pair>) | Pair of logical coords where first coord <= second coord | Yes      |
 * | initial_value | Initial value of the semaphore                       | uint32_t                                              |                                                          | Yes      |
 */
std::vector<Semaphore *> CreateSemaphores(Program *program, Device *device, const CoreRange &core_range, uint32_t initial_value);

/**
* Copies data from a host buffer into the specified buffer
*
* Return value: void
*
* | Argument    | Description                                     | Data type               | Valid range                                      | Required |
* |-------------|-------------------------------------------------|-------------------------|--------------------------------------------------|----------|
* | buffer      | Buffer to send data to                          | const Buffer &          |                                                  | Yes      |
* | host_buffer | Buffer on host to copy data from                | std::vector<uint32_t> & | Host buffer size must match buffer               | Yes      |
*/
void WriteToBuffer(const Buffer &buffer, std::vector<uint32_t> &host_buffer);

/**
* Copies data from a buffer into a host buffer
*
* Return value: void
*
* | Argument    | Description                                     | Data type               | Valid range                                      | Required |
* |-------------|-------------------------------------------------|-------------------------|--------------------------------------------------|----------|
* | buffer      | Buffer to read data from                        | const Buffer &          |                                                  | Yes      |
* | host_buffer | Buffer on host to copy data into                | std::vector<uint32_t> & |                                                  | Yes      |
*/
void ReadFromBuffer(const Buffer &buffer, std::vector<uint32_t> &host_buffer);

/**
*  Deallocates buffer from device by marking its memory as free.
*
*  Return value: void
*
*  | Argument | Description                          | Type     | Valid Range | Required |
*  |----------|--------------------------------------|----------|-------------|----------|
*  | buffer   | The buffer to deallocate from device | Buffer & |             | Yes      |
*/
void DeallocateBuffer(Buffer &buffer);

/**
 * Copies data from a host buffer into a buffer within the device DRAM channel
 *
 * Return value: bool
 *
 * | Argument     | Description                                            | Data type             | Valid range                               | required |
 * |--------------|--------------------------------------------------------|-----------------------|-------------------------------------------|----------|
 * | device       | The device whose DRAM to write data into               | Device *              |                                           | Yes      |
 * | dram_channel | Channel index of DRAM to write into                    | int                   | On Grayskull, [0, 7] inclusive            | Yes      |
 * | address      | Starting address on DRAM channel to begin writing data | uint32_t              |                                           | Yes      |
 * | host_buffer  | Buffer on host to copy data from                       | std::vector<uint32_t> | Host buffer must be fully fit DRAM buffer | Yes      |
 */
bool WriteToDeviceDRAMChannel(Device *device, int dram_channel, uint32_t address, std::vector<uint32_t> &host_buffer);

/**
 * Copy data from a device DRAM channel to a host buffer
 *
 * Return value: bool
 *
 * | Argument     | Description                                                  | Data type             | Valid range                    | required |
 * |--------------|--------------------------------------------------------------|-----------------------|--------------------------------|----------|
 * | device       | The device whose DRAM to read data from                      | Device *              |                                | Yes      |
 * | dram_channel | Channel index of DRAM to read from                           | int                   | On Grayskull, [0, 7] inclusive | Yes      |
 * | address      | Starting address on DRAM channel from which to begin reading | uint32_t              |                                | Yes      |
 * | size         | Size of buffer to read from device in bytes                  | uint32_t              |                                | Yes      |
 * | host_buffer  | Buffer on host to copy data into                             | std::vector<uint32_t> |                                | Yes      |
 */
bool ReadFromDeviceDRAMChannel(Device *device, int dram_channel, uint32_t address, uint32_t size, std::vector<uint32_t> &host_buffer);

/**
 * Copy data from a host buffer into an L1 buffer. (Note: Current Can not be a CircularBuffer.)
 *
 * Return value: bool
 *
 * | Argument      | Description                                     | Data type             | Valid range                                         | required |
 * |---------------|-------------------------------------------------|-----------------------|-----------------------------------------------------|----------|
 * | device        | The device whose DRAM to write data into        | Device *              |                                                     | Yes      |
 * | logical_core  | Logical coordinate of core whose L1 to write to | tt_xy_pair            | On Grayskull, any valid logical worker coordinate   | Yes      |
 * | address       | Starting address in L1 to write into            | uint32_t              | Any non-reserved address in L1 that fits for buffer | Yes      |
 * | host_buffer   | Buffer on host whose data to copy from          | std::vector<uint32_t> | Buffer must fit into L1                             | Yes      |
 */
bool WriteToDeviceL1(Device *device, const tt_xy_pair &logical_core, uint32_t address, std::vector<uint32_t> &host_buffer);

/**
 * Copy data from an L1 buffer into a host buffer. Must be a buffer, and not a CB.
 *
 * Return value: bool
 *
 * | Argument             | Description                                 | Data type             | Valid range                                       | required |
 * |----------------------|---------------------------------------------|-----------------------|---------------------------------------------------|----------|
 * | device               | The device whose DRAM to read data from     | Device *              |                                                   | Yes      |
 * | logical_core         | Logical coordinate of core whose L1 to read | tt_xy_pair            | On Grayskull, any valid logical worker coordinate | Yes      |
 * | address              | Starting address in L1 to read from         | uint32_t              |                                                   | Yes      |
 * | size                 | Size of L1 buffer in bytes                  | uint32_t              |                                                   | Yes      |
 * | host_buffer          | Buffer on host to copy data into            | std::vector<uint32_t> | Buffer must fit L1 buffer                         | Yes      |
 */
bool ReadFromDeviceL1(Device *device, const tt_xy_pair &logical_core, uint32_t address, uint32_t size, std::vector<uint32_t> &host_buffer);

// ==================================================
//           COMPILE & EXECUTE KENRNELS
//
// ==================================================

// Compiles all kernels within the program, and generates their binaries
bool CompileProgram(
    Device *device,                 // Device - device doesn't have to be initialized to compile the program.
    Program *program,               // Program
    bool profile_kernel = false);   // Set the compile flag for kernels to report profiling timer marks

// Configures a given device with a given program.
// - Loads all kernel binaries into L1s of assigned Tensix cores
// - Configures circular buffers (inits regs with buffer data)
// - Takes the device out of reset
bool ConfigureDeviceWithProgram(Device *device, Program *program);

// Loads all kernel args into L1s of assigned Tensix cores
bool WriteRuntimeArgsToDevice(Device *device, DataMovementKernel *kernel, const tt_xy_pair &logical_core, const std::vector<uint32_t> &runtime_args);

bool WriteRuntimeArgsToDevice(Device *device, DataMovementKernel *kernel, const CoreRange &core_range, const std::vector<uint32_t> &runtime_args);

bool WriteRuntimeArgsToDevice(Device *device, DataMovementKernel *kernel, const CoreBlocks &core_blocks, const std::vector<std::vector<uint32_t>> &runtime_args_spec);

// Launches all kernels on cores specified with kernels in the program.
// All kernels on a given Tensix core must be launched.
bool LaunchKernels(Device *device, Program *program, bool stagger_start = false);

bool WriteToDeviceL1(Device *device, const tt_xy_pair &core, op_info_t op_info, int op_idx);

}  // namespace tt_metal

}  // namespace tt
