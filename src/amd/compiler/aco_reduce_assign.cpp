/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "aco_ir.h"
#include "aco_builder.h"

/*
 * Insert p_linear_start instructions right before RA to correctly allocate
 * temporaries for reductions that have to disrespect EXEC by executing in
 * WWM.
 */

namespace aco {

void setup_reduce_temp(Program* program)
{
   unsigned last_top_level_block_idx = 0;
   unsigned maxSize = 0;

   std::vector<bool> hasReductions(program->blocks.size());
   for (Block& block : program->blocks) {
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->format != Format::PSEUDO_REDUCTION)
            continue;

         maxSize = MAX2(maxSize, instr->operands[0].size());
         hasReductions[block.index] = true;
      }
   }

   if (maxSize == 0)
      return;

   assert(maxSize == 1 || maxSize == 2);
   Temp reduceTmp(0, RegClass(RegType::vgpr, maxSize).as_linear());
   Temp vtmp(0, RegClass(RegType::vgpr, maxSize).as_linear());
   int inserted_at = -1;
   int vtmp_inserted_at = -1;
   bool reduceTmp_in_loop = false;
   bool vtmp_in_loop = false;

   for (Block& block : program->blocks) {

      /* insert p_end_linear_vgpr after the outermost loop */
      if (reduceTmp_in_loop && block.loop_nest_depth == 0) {
         assert(inserted_at == (int)last_top_level_block_idx);

         aco_ptr<Instruction> end{create_instruction<Instruction>(aco_opcode::p_end_linear_vgpr, Format::PSEUDO, vtmp_in_loop ? 2 : 1, 0)};
         end->operands[0] = Operand(reduceTmp);
         if (vtmp_in_loop)
            end->operands[1] = Operand(vtmp);
         /* insert after the phis of the loop exit block */
         std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.begin();
         while ((*it)->opcode == aco_opcode::p_linear_phi || (*it)->opcode == aco_opcode::p_phi)
            ++it;
         block.instructions.insert(it, std::move(end));
         reduceTmp_in_loop = false;
      }

      if (block.kind & block_kind_top_level)
         last_top_level_block_idx = block.index;

      if (!hasReductions[block.index])
         continue;

      std::vector<aco_ptr<Instruction>>::iterator it;
      for (it = block.instructions.begin(); it != block.instructions.end(); ++it) {
         Instruction *instr = (*it).get();
         if (instr->format != Format::PSEUDO_REDUCTION)
            continue;

         ReduceOp op = static_cast<Pseudo_reduction_instruction *>(instr)->reduce_op;
         reduceTmp_in_loop |= block.loop_nest_depth > 0;

         if ((int)last_top_level_block_idx != inserted_at) {
            reduceTmp = {program->allocateId(), reduceTmp.regClass()};
            aco_ptr<Pseudo_instruction> create{create_instruction<Pseudo_instruction>(aco_opcode::p_start_linear_vgpr, Format::PSEUDO, 0, 1)};
            create->definitions[0] = Definition(reduceTmp);
            /* find the right place to insert this definition */
            if (last_top_level_block_idx == block.index) {
               /* insert right before the current instruction */
               it = block.instructions.insert(it, std::move(create));
               it++;
               /* inserted_at is intentionally not updated here, so later blocks
                * would insert at the end instead of using this one. */
            } else {
               assert(last_top_level_block_idx < block.index);
               /* insert before the branch at last top level block */
               std::vector<aco_ptr<Instruction>>& instructions = program->blocks[last_top_level_block_idx].instructions;
               instructions.insert(std::next(instructions.begin(), instructions.size() - 1), std::move(create));
               inserted_at = last_top_level_block_idx;
            }
         }

         /* same as before, except for the vector temporary instead of the reduce temporary */
         unsigned cluster_size = static_cast<Pseudo_reduction_instruction *>(instr)->cluster_size;
         bool need_vtmp = op == imul32 || op == fadd64 || op == fmul64 ||
                          op == fmin64 || op == fmax64;

         if (program->chip_class >= GFX10 && cluster_size == 64 && op != gfx10_wave64_bpermute)
            need_vtmp = true;

         need_vtmp |= cluster_size == 32;

         vtmp_in_loop |= need_vtmp && block.loop_nest_depth > 0;
         if (need_vtmp && (int)last_top_level_block_idx != vtmp_inserted_at) {
            vtmp = {program->allocateId(), vtmp.regClass()};
            aco_ptr<Pseudo_instruction> create{create_instruction<Pseudo_instruction>(aco_opcode::p_start_linear_vgpr, Format::PSEUDO, 0, 1)};
            create->definitions[0] = Definition(vtmp);
            if (last_top_level_block_idx == block.index) {
               it = block.instructions.insert(it, std::move(create));
               it++;
            } else {
               assert(last_top_level_block_idx < block.index);
               std::vector<aco_ptr<Instruction>>& instructions = program->blocks[last_top_level_block_idx].instructions;
               instructions.insert(std::next(instructions.begin(), instructions.size() - 1), std::move(create));
               vtmp_inserted_at = last_top_level_block_idx;
            }
         }

         instr->operands[1] = Operand(reduceTmp);
         if (need_vtmp)
            instr->operands[2] = Operand(vtmp);

         /* scalar temporary */
         Builder bld(program);
         instr->definitions[1] = bld.def(s2);

         /* scalar identity temporary */
         bool need_sitmp = program->chip_class >= GFX10 && cluster_size == 64;
         if (instr->opcode == aco_opcode::p_exclusive_scan) {
            need_sitmp |=
               (op == imin32 || op == imin64 || op == imax32 || op == imax64 ||
                op == fmin32 || op == fmin64 || op == fmax32 || op == fmax64 ||
                op == fmul64);
         }
         if (need_sitmp) {
            instr->definitions[2] = bld.def(RegClass(RegType::sgpr, instr->operands[0].size()));
         }

         /* vcc clobber */
         if (op == iadd32 && program->chip_class < GFX9)
            instr->definitions[4] = Definition(vcc, s2);
      }
   }
}

};
