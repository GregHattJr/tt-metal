#include <iostream>

#include "build_kernels_for_riscv/build_kernels_for_riscv.hpp"
#include "tests/tt_metal/test_utils/env_vars.hpp"


namespace matmul {
// FIXME:copy pasted the args here from the kernel file,  we could refactor the HLK file
struct hlk_args_t {
    int block_tile_dim;
    int dst_tile_rows;
    int dst_tile_cols;
    int block_cnt;
    int in0_block_tile_cnt;
    int in1_block_tile_cnt;
    int out_block_tile_cnt;
};
}



int main(int argc, char* argv[]) {

    std::string arch_name = tt::test_utils::get_env_arch_name();

    // Create and config an OP
    tt::build_kernel_for_riscv_options_t build_kernel_for_riscv_options("matmul","matmul_small_block");

    log_info(tt::LogBuildKernels, "Compiling OP: {}", build_kernel_for_riscv_options.name);

    std::vector<uint32_t> compute_kernel_args = {
        1,
        1,
        1,
        1,
        1,
        1,
        1
    };

    // HLK config
    build_kernel_for_riscv_options.set_hlk_file_name_all_cores("tt_metal/kernels/compute/matmul.cpp");
    build_kernel_for_riscv_options.set_hlk_math_fidelity_all_cores(MathFidelity::HiFi4);

    // matmul: 2 input operands, and 1 output operand (operand == buffer)
    build_kernel_for_riscv_options.set_hlk_operand_dataformat_all_cores(tt::HlkOperand::in0, tt::DataFormat::Float16_b);
    build_kernel_for_riscv_options.set_hlk_operand_dataformat_all_cores(tt::HlkOperand::in1, tt::DataFormat::Float16_b);
    build_kernel_for_riscv_options.set_hlk_operand_dataformat_all_cores(tt::HlkOperand::out0, tt::DataFormat::Float16_b);

    // make sure to set this to false on GS (because no FP32 in dst), otherwise pack_src_format will be incorrect
    build_kernel_for_riscv_options.fp32_dest_acc_en = false;

    // NCRISC / BRISC config
    build_kernel_for_riscv_options.ncrisc_kernel_file_name = "tt_metal/kernels/dataflow/reader_matmul_small_block.cpp";
    build_kernel_for_riscv_options.brisc_kernel_file_name = "tt_metal/kernels/dataflow/writer_unary.cpp"; // writer unary is generic to support this case

    // generate binaries
    __internal::generate_default_bank_to_noc_coord_descriptor (
        &build_kernel_for_riscv_options,
        build_kernel_for_riscv_options.name,
        tt::get_arch_from_string(arch_name)
    );
    generate_binaries_params_t params = {.compute_kernel_compile_time_args = compute_kernel_args};
    generate_binaries_all_riscs(&build_kernel_for_riscv_options, build_kernel_for_riscv_options.name, arch_name, params);

    return 0;
}
