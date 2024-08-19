// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "binary_device_operation.hpp"
#include "ttnn/tensor/tensor.hpp"
#include "ttnn/deprecated/tt_dnn/op_library/bcast/bcast_op.hpp"
//#include "ttnn/deprecated/tt_dnn/op_library/work_split.hpp"
#include "tt_metal/common/constants.hpp"
#include "tt_metal/detail/util.hpp"
#include "tt_metal/host_api.hpp"
//#include "ttnn/device_operation.hpp"

namespace ttnn::operations::binary {

static const tt::tt_metal::BcastOpMath binary_op_type_to_bcast_op_math(const BinaryOpType binary_op_type) {
    switch (binary_op_type) {
        case BinaryOpType::ADD: return tt::tt_metal::BcastOpMath::ADD;
        case BinaryOpType::SUB: return tt::tt_metal::BcastOpMath::SUB;
        case BinaryOpType::MUL: return tt::tt_metal::BcastOpMath::MUL;
        default: TT_THROW("BinaryOpType cannot be mapped to BcastOpMath");
    }
}

BinaryDeviceOperation::BroadcastHeightMultiCoreShardedOptimized::cached_program_t
BinaryDeviceOperation::BroadcastHeightMultiCoreShardedOptimized::create(
    const operation_attributes_t& operation_attributes,
    const tensor_args_t& tensor_args,
    tensor_return_value_t& tensor_return_value) {
    using namespace tt;
    using namespace tt::tt_metal;

    const auto& a = tensor_args.input_tensor_a;
    const auto& b = tensor_args.input_tensor_b;
    auto& output = tensor_return_value;
    auto bcast_math = binary_op_type_to_bcast_op_math(operation_attributes.binary_op_type);

    const auto ashape = a.get_legacy_shape();
    const auto bshape = b.get_legacy_shape();
    uint32_t N = ashape.rank() >= 4 ? ashape[-4] : 1;
    uint32_t C = ashape.rank() >= 3 ? ashape[-3] : 1;
    uint32_t H = ashape[-2];
    uint32_t W = ashape[-1];
    uint32_t bN = bshape.rank() >= 4 ? bshape[-4] : 1;
    uint32_t bC = bshape.rank() >= 3 ? bshape[-3] : 1;
    uint32_t bH = bshape[-2];
    uint32_t bW = bshape[-1];
    uint32_t NC = N * C;
    uint32_t HW = H * W;

    // uint32_t Wt = W / TILE_WIDTH;
    // uint32_t Ht = H / TILE_HEIGHT;

    // uint32_t num_tensor_tiles = NC * Ht * Wt;
    uint32_t num_btensor_tiles = NC * bH * bW / TILE_HW;

    uint32_t bnc1 = (bN * bC == 1) ? 1 : 0;

    tt_metal::Program program = tt_metal::CreateProgram();

    tt_metal::Device* device = a.device();

    auto shard_spec = a.shard_spec().value();
    auto all_cores = shard_spec.grid;
    uint32_t ncores = shard_spec.num_cores();

    auto compute_with_storage_grid_size = device->compute_with_storage_grid_size();
    uint32_t ncores_x = compute_with_storage_grid_size.x;

    auto out_shard_spec = output.shard_spec().value();
    TT_FATAL(
        out_shard_spec.num_cores() == ncores,
        "Output tensor should have same number of cores {} as input tensor {}",
        out_shard_spec.num_cores(),
        ncores);

    tt::DataFormat act_df = tt_metal::datatype_to_dataformat_converter(a.get_dtype());
    tt::DataFormat b_df = tt_metal::datatype_to_dataformat_converter(b.get_dtype());
    tt::DataFormat out_df = tt_metal::datatype_to_dataformat_converter(output.get_dtype());

    uint32_t input_tile_size = tt::tt_metal::detail::TileSize(act_df);
    uint32_t input1_tile_size = tt::tt_metal::detail::TileSize(b_df);
    uint32_t output_tile_size = tt::tt_metal::detail::TileSize(out_df);

    TT_FATAL(input_tile_size == output_tile_size, "Input and output tile size should be same");
    uint32_t shard_size_in_bytes = shard_spec.numel() * a.element_size();

    uint32_t num_tile_per_core = (shard_size_in_bytes + input_tile_size - 1) / TILE_HW;  // ceil value
    TT_FATAL(input_tile_size <= shard_size_in_bytes, "Input tile size should be less than shard size");

    uint32_t Wt, Ht;
    if (a.memory_config().memory_layout == TensorMemoryLayout::BLOCK_SHARDED) {
        ncores_x = all_cores.ranges().begin()->end_coord.y + 1;
        Wt = shard_spec.shape[1] / TILE_WIDTH;
        Ht = shard_spec.shape[0] / TILE_HEIGHT;
    } else if (a.memory_config().memory_layout == TensorMemoryLayout::WIDTH_SHARDED) {
        Wt = shard_spec.shape[1] / TILE_WIDTH;
        Ht = shard_spec.shape[0] / TILE_HEIGHT;
        TT_ASSERT(
            (shard_spec.shape[0] % (bN * TILE_HEIGHT) == 0),
            "Shard height per batch must be divisible by TILE_HEIGHT {} {} {} ",
            shard_spec.shape[0],
            bN,
            TILE_HEIGHT);
    } else {
        TT_FATAL(false, "Unsupported memory layout");
    }

    TT_ASSERT(
        (shard_spec.shape[0] % TILE_HEIGHT == 0) && (shard_spec.shape[0] % TILE_WIDTH == 0),
        "Shard shapes must be multiple of TILE_HEIGHT ");

    uint32_t src0_cb_index = CB::c_in0;
    uint32_t aligned_input_tile_nbytes =
        round_up_to_mul32(input_tile_size);  // will have issue if the page is not multiple of 32
    uint32_t in_cb_pagesize = aligned_input_tile_nbytes;
    tt_metal::CircularBufferConfig src0_cb_config =
        tt_metal::CircularBufferConfig(aligned_input_tile_nbytes * num_tile_per_core, {{src0_cb_index, act_df}})
            .set_page_size(src0_cb_index, in_cb_pagesize)
            .set_globally_allocated_address(*a.buffer());
    auto cb_src0 = tt_metal::CreateCircularBuffer(program, all_cores, src0_cb_config);

    uint32_t output_cb_index = CB::c_out0;  // output operands start at index 16
    tt_metal::CircularBufferConfig output_cb_config =
        tt_metal::CircularBufferConfig(aligned_input_tile_nbytes * num_tile_per_core, {{output_cb_index, out_df}})
            .set_page_size(output_cb_index, in_cb_pagesize)
            .set_globally_allocated_address(*output.buffer());
    auto out_cb = tt_metal::CreateCircularBuffer(program, all_cores, output_cb_config);

    uint32_t h_blk = std::min(Ht, 8u);
    uint32_t w_blk = std::min(Wt, 8u);

    uint32_t num_input_tiles = w_blk;
    uint32_t src1_cb_index = CB::c_in1;
    tt_metal::CircularBufferConfig src1_cb_config =
        tt_metal::CircularBufferConfig(num_input_tiles * input1_tile_size, {{src1_cb_index, b_df}})
            .set_page_size(src1_cb_index, input1_tile_size);
    auto cb_src1 = tt_metal::CreateCircularBuffer(program, all_cores, src1_cb_config);

    auto src0_buffer = a.buffer();
    auto src1_buffer = b.buffer();
    auto dst_buffer = output.buffer();
    bool src1_is_dram = src1_buffer->buffer_type() == tt_metal::BufferType::DRAM ? 1 : 0;
    std::vector<uint32_t> reader_compile_time_args = {(uint32_t)src0_cb_index, (uint32_t)src1_is_dram};

    bool dst_is_dram = dst_buffer->buffer_type() == tt_metal::BufferType::DRAM ? 1 : 0;
    std::vector<uint32_t> writer_compile_time_args = {(uint32_t)dst_is_dram};

    KernelHandle binary_reader_kernel_id = tt_metal::CreateKernel(
        program,
        "ttnn/cpp/ttnn/deprecated/tt_dnn/op_library/bcast/kernels/dataflow/reader_bcast_h_sharded_optimised.cpp",
        all_cores,
        tt_metal::ReaderDataMovementConfig(reader_compile_time_args));

    std::map<std::string, std::string> bcast_defines = bcast_op_utils::get_defines(BcastOpDim::H, bcast_math);
    // const char* compute_name = bcast_op_utils::get_compute_name(BcastOpDim::H));
    auto bcast_kernel_id = tt_metal::CreateKernel(
        program,
        "ttnn/cpp/ttnn/deprecated/tt_dnn/op_library/bcast/kernels/compute/bcast_h_sharded_optimised.cpp",
        all_cores,
        tt_metal::ComputeConfig{.compile_args = {}, .defines = bcast_defines});

    uint32_t ncores_y = ncores / ncores_x;
    TT_FATAL((NC * H / TILE_HEIGHT) % bN == 0, "N*C*H of input0 must be devisible by batch size of input1");
    uint32_t Ht_per_batch_b = std::min((NC * H / TILE_HEIGHT) / bN, Ht);
    uint32_t batch_b = Ht / Ht_per_batch_b;

    log_debug(
        "ncores {}, ncores_x {}, Wt {}, Ht {}, h_blk {}, w_blk {}, src0_cb_index {}, src1_cb_index {}, output_cb_index "
        "{}, src1_is_dram {}, dst_is_dram {}, Ht_per_batch_b {}, batch_b {}",
        ncores,
        ncores_x,
        Wt,
        Ht,
        h_blk,
        w_blk,
        src0_cb_index,
        src1_cb_index,
        output_cb_index,
        src1_is_dram,
        dst_is_dram,
        Ht_per_batch_b,
        batch_b);

    for (uint32_t i = 0; i < ncores; i++) {
        CoreCoord core;
        uint32_t offset = 0;
        if (a.memory_config().memory_layout == TensorMemoryLayout::BLOCK_SHARDED) {
            core = {i / ncores_x, i % ncores_x};
            if (shard_spec.orientation == ShardOrientation::ROW_MAJOR) {
                offset = Wt * (i / ncores_x) + Wt * ncores_y * ((i % ncores_x) / (ncores_x / bN));
            } else {
                offset = Wt * (i % ncores_x) + Wt * ncores_x * ((i / ncores_x) / (ncores_y / bN));
            }
        } else if (a.memory_config().memory_layout == TensorMemoryLayout::WIDTH_SHARDED) {
            core = {i % ncores_x, i / ncores_x};
            if (shard_spec.orientation == ShardOrientation::ROW_MAJOR) {
                offset = Wt * (core.x + core.y * ncores_x);
            } else {
                offset = Wt * (ncores_y * core.x + core.y);
                if (core.y == ncores_y) {
                    offset = Wt * (ncores_y * ncores_x + core.x);
                }
            }
        }
        uint32_t tile_offset = Wt * ncores;  // used in multi batch weight for block sharded
        tt_metal::SetRuntimeArgs(
            program,
            binary_reader_kernel_id,
            core,
            {
                b.buffer()->address(),  // (0) src1_addr
                Ht,                     // (1) Ht
                Wt,                     // (2) Wt
                offset,                 // (3) read offset in1
                tile_offset,            // (4) in1 offset between batches
                w_blk,                  // (5) block size in w
                batch_b,                // (6) in1 batch size
            });

        tt_metal::SetRuntimeArgs(
            program,
            bcast_kernel_id,
            core,
            {
                NC,              // (0) B
                Ht,              // (1) Hbatch  for block shardeshardedt
                Wt,              // (2) Wt
                h_blk,           // (3) h block size
                batch_b,         // (4) in1 batch size
                Ht_per_batch_b,  // (5) Ht per in1 batch size (bN)
            });
    }

    return {
        std::move(program),
        {
            binary_reader_kernel_id,
            bcast_kernel_id,
            cb_src0,
            out_cb,
            ncores_x
        }};
    }

void BinaryDeviceOperation ::BroadcastHeightMultiCoreShardedOptimized::override_runtime_arguments(
    cached_program_t& cached_program,
    const operation_attributes_t& operation_attributes,
    const tensor_args_t& tensor_args,
    tensor_return_value_t& tensor_return_value) {
    using namespace tt;
    using namespace tt::tt_metal;

    const auto& input_tensor_a = tensor_args.input_tensor_a;
    const auto& input_tensor_b = tensor_args.input_tensor_b;
    auto& output_tensor = tensor_return_value;

    auto&& [binary_reader_kernel_id, bcast_kernel_id, cb_src0,out_cb, ncores_x ] =
        cached_program.shared_variables;

    auto& program = cached_program.program;
        auto src_buffer = input_tensor_a.buffer();
        auto dst_buffer = output_tensor.buffer();
        UpdateDynamicCircularBufferAddress(program, cb_src0, *src_buffer);
        UpdateDynamicCircularBufferAddress(program, out_cb, *dst_buffer);
        auto a = input_tensor_a;
        auto b = input_tensor_b;
        auto shard_spec = a.shard_spec().value();
        auto all_cores = shard_spec.grid;
        uint32_t ncores = shard_spec.num_cores();
        uint32_t Wt = 0, Ht =0;
        const auto ashape = input_tensor_a.get_legacy_shape();
        uint32_t N  = ashape[0], C  = ashape[1], H  = ashape[2], W  = ashape[3];
        uint32_t bN = input_tensor_b.get_legacy_shape()[0];
        uint32_t NC = N*C;
        if(a.memory_config().memory_layout == TensorMemoryLayout::BLOCK_SHARDED){
            Wt = shard_spec.shape[1] / TILE_WIDTH;
            Ht = shard_spec.shape[0] / TILE_HEIGHT;
        } else if(a.memory_config().memory_layout == TensorMemoryLayout::WIDTH_SHARDED){
            Wt = shard_spec.shape[1] / TILE_WIDTH;
            Ht = shard_spec.shape[0] / TILE_HEIGHT;
        } else{
            TT_FATAL(false, "Unsupported memory layout");
        }
        uint32_t ncores_y = ncores / ncores_x;
        uint32_t Ht_per_b1 = 0; // Ht per batch
        for (uint32_t i = 0; i < ncores; i++){
            CoreCoord core;
            uint32_t offset = 0;
            if(a.memory_config().memory_layout == TensorMemoryLayout::BLOCK_SHARDED){
                core = {i / ncores_x, i % ncores_x};
                Ht_per_b1 = Ht;
                if(shard_spec.orientation == ShardOrientation::ROW_MAJOR){
                    offset = Wt * (i / ncores_x) + Wt *  ncores_y * ((i % ncores_x) / (ncores_x/bN));
                }else{
                    offset = Wt * (i % ncores_x) + Wt *  ncores_x * ((i / ncores_x) / (ncores_y/bN));
                }
            }  else if(a.memory_config().memory_layout == TensorMemoryLayout::WIDTH_SHARDED){
                core = {i % ncores_x, i / ncores_x};
                if(shard_spec.orientation == ShardOrientation::ROW_MAJOR){
                    offset = Wt * (core.x + core.y * ncores_x);
                }else{
                    offset = Wt * (ncores_y * core.x + core.y);
                    if(core.y == ncores_y){
                        offset = Wt * (ncores_y * ncores_x + core.x);
                    }
                }
                Ht_per_b1 = Ht / bN;
            }
            uint32_t tile_offset = Wt * ncores;

            uint32_t h_blk = std::min(Ht, 8u);
            uint32_t w_blk = std::min(Wt, 8u);

            tt_metal::SetRuntimeArgs(
                program,
                binary_reader_kernel_id,
                core,
                {
                    b.buffer()->address(), // (0) src1_addr
                    Ht, // (1) Ht
                    Wt, // (2) Wt
                    offset, // (3) read offset in1
                    tile_offset, // (4) in1 offset between batches
                    w_blk, // (5) block size in w
                    bN, // (6) in1 batch size
                }
            );

            tt_metal::SetRuntimeArgs(
                program,
                bcast_kernel_id,
                core,
                {
                    NC, // (0) B
                    Ht, // (1) Hbatch  for block shardeshardedt
                    Wt,  // (2) Wt
                    h_blk, // (3) h block size
                    bN, // (4) in1 batch size
                    Ht_per_b1, // (5) Ht per in1 batch size (bN)
                }
            );
        }
}

}  // namespace ttnn::operations::binary