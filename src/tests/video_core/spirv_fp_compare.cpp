// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>

#include <catch2/catch_test_macros.hpp>
#define SPV_ENABLE_UTILITY_CODE
#include <spirv/unified1/spirv.hpp11>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"
#include "shader_recompiler/object_pool.h"

namespace {
namespace IR = Shader::IR;

std::vector<u32> EmitOrdNotEqual(bool ignore_nan_fp_comparisons) {
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    Shader::IR::Program program;
    program.stage = Shader::Stage::Compute;
    program.workgroup_size = {1, 1, 1};
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                           {{}, IR::AbstractSyntaxNode::Type::Return}};

    IR::IREmitter ir{block};
    const IR::F32 lhs = ir.Imm32(1.0f);
    const IR::F32 rhs = ir.Imm32(2.0f);
    const IR::U1 cmp = ir.FPNotEqual(lhs, rhs, {}, true);
    (void)ir.Select(cmp, ir.Imm32(1), ir.Imm32(0));

    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    profile.ignore_nan_fp_comparisons = ignore_nan_fp_comparisons;
    return Shader::Backend::SPIRV::EmitSPIRV(profile, program);
}
} // Anonymous namespace

TEST_CASE("SPIR-V FPOrdNotEqual emits OpFOrdNotEqual under ignore_nan_fp_comparisons",
          "[video_core][spirv][fp_compare]") {
    for (const bool ignore_nan : {false, true}) {
        const auto code = EmitOrdNotEqual(ignore_nan);
        size_t ord_equal_count = 0;
        size_t ord_not_equal_count = 0;
        size_t is_nan_count = 0;
        for (size_t offset = 5; offset < code.size();) {
            const u32 word_count = code[offset] >> 16;
            REQUIRE(word_count > 0);
            REQUIRE(offset + word_count <= code.size());
            const auto op = static_cast<spv::Op>(code[offset] & 0xffffU);
            ord_equal_count += op == spv::Op::OpFOrdEqual;
            ord_not_equal_count += op == spv::Op::OpFOrdNotEqual;
            is_nan_count += op == spv::Op::OpIsNan;
            offset += word_count;
        }
        CAPTURE(ignore_nan);
        CHECK(ord_equal_count == 0);
        CHECK(ord_not_equal_count == 1);
        CHECK(is_nan_count == (ignore_nan ? 2U : 0U));
    }
}
