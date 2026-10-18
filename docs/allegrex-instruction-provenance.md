# Instruction support added during FF3 bring-up

The implementations are independently written from instruction semantics and
published hardware observations. No emulator implementation is included. Game
code and generated FF3 translation units remain local ignored inputs/outputs.

## Integer instructions

`madd` and `msub` multiply signed 32-bit operands, then add/subtract the product
from the 64-bit HI:LO accumulator modulo 2^64. The implementation widens the
signed product before converting to unsigned arithmetic, avoiding signed overflow.

[Yakumo](https://github.com/TeamGDB/Yakumo/tree/87bc4d9f5ad61829652ffb07bc8e477d39f706d5)
was a useful MIT-licensed comparison for this arithmetic. Its code generator was
reviewed, but this implementation uses a separately written context helper.
[sp00nznet/psprecomp](https://github.com/sp00nznet/psprecomp/tree/caca7595251410ae7887aa209ba56397a835d0e1)
was also inspected as an MIT-licensed VFPU reference; no source was imported.

`break` records ExcCode 9, EPC, the branch-delay bit, and the original instruction.
The host stops with these diagnostics. Dispatching a guest exception handler is
not implemented. Branch-likely annulment is handled by the generated branch;
an annulled `break` never executes.

Instruction encodings were checked against
[PSPDEV Allegrex opcodes](https://github.com/pspdev/binutils-gdb/blob/allegrex-v2.37.0/opcodes/mips-opc.c).
Only the implemented widths/encodings are accepted.

## VFPU

[VFPU documentation](https://pspdev.github.io/vfpu-docs/) describes `vcrs.t`
(partial cross-product, distinct from `vcrsp.t`) and `vi2uc.q`. VCRS ignores source
prefixes and honors destination masking/saturation. VI2UC uses source swizzle,
clamps negative signed lanes to zero, packs bits 23–30 into bytes, honors the
scalar destination mask, and ignores saturation. Inputs are read before outputs.

The scalar `vrndf1.s` implementation follows the mathematical state layout and
recurrence identified by fp64's [hardware investigation](https://github.com/hrydgard/ppsspp/issues/16946):

- Four 32-bit generator words are split across the low halfwords of RCX0–7.
  The next nibble of each RCX register forms a fifth, carry word.
- The components advance with a linear congruential recurrence, a xorshift
  recurrence, and a Pell recurrence with carry. Their sum provides the output.
- The carry rule uses the high word of the old third word plus half the old
  fourth word plus old carry. The [corrected carry research](https://github.com/hrydgard/ppsspp/issues/16946#issuecomment-1567078071)
  supersedes earlier conjectures in that discussion.
- Float output uses the low 23 result bits as the mantissa with exponent 127.
  Destination masking still advances the generator; saturation is applied after
  generation. All three prefixes are consumed.

The regression test embeds the two scalar outputs and intervening RCX state
[observed for seed 1.4f](https://github.com/hrydgard/ppsspp/issues/16946#issuecomment-1471889038).
It does not claim exhaustive hardware equivalence. Vector random instructions
and seed instructions remain unsupported by the decoder.

FF3 also uses `mfvc $zero,255`. The read result is discarded and VFPU operations
already complete synchronously in this runtime, so the emitter needs no register
access for it. This interpretation is consistent with the interlock observation
in [PPSSPP's MFVC handler](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Core/MIPS/MIPSIntVFPU.cpp).
No handler source was imported. The generated-code fixtures include this form.

## Validation

`ff3_instruction_tests` and `ff3_instruction_dense_tests` generate a
synthetic ELF, recompile it with the actual code generator, build the emitted C++,
and execute the results. They cover
signed products, HI/LO carry/wrap, multiply in a return delay slot, taken/untaken
and annulled trap slots, VFPU prefixes/overlap, and hardware random vectors.
The fixture and tests contain no game instructions or assets.
