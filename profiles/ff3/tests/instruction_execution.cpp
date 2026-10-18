#include "psprecomp/runtime.hpp"
#include "psprecomp/decoder.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace psprecomp { void register_generated_functions(Runtime &); }
using namespace psprecomp;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
 try {
    Runtime rt; register_generated_functions(rt); auto &c = rt.cpu();
    check(c.vfpu_ctrl[8] == 0x3F800001u && c.vfpu_ctrl[11] == 0x3F800008u, "Default random state");
    c.hi = 0xFFFFFFFFu; c.lo = 0xFFFFFFFFu; c.gpr[4] = 1; c.gpr[5] = 1;
    rt.run(0x08804200u, 2); check(c.hi == 0 && c.lo == 0, "MADD carry/wrap");
    c.hi = 0; c.lo = 1; c.gpr[4] = 0x80000000u; c.gpr[5] = 2;
    rt.run(0x08804200u, 2); check(c.hi == 0xFFFFFFFFu && c.lo == 1, "MADD signed product");
    c.hi = 0x80000000u; c.lo = 0; c.gpr[4] = 0x80000000u; c.gpr[5] = 0xFFFFFFFFu;
    rt.run(0x08804040u, 2); check(c.hi == 0x80000000u && c.lo == 0, "MSUB inverse");
    check(c.trap.valid && c.trap.epc == 0x08804048u && c.trap.cause == 36u && c.trap.instruction == 0x1CDu, "BREAK context");
    for (auto rhs : {0u, 1u}) {
        c.gpr[4] = 0; c.gpr[5] = rhs; rt.run(0x08804080u, 2);
        check(c.trap.epc == 0x08804080u && c.trap.cause == 0x80000024u, "BREAK branch delay context");
        rt.run(0x088040C0u, 2);
        check(c.trap.epc == (rhs ? 0x088040C8u : 0x088040C0u), "Likely branch annul/EPC");
        check(c.trap.cause == (rhs ? 36u : 0x80000024u), "Likely branch BD");
    }
    c.hi = 0; c.lo = 0; c.gpr[4] = 3; c.gpr[5] = 7; c.gpr[31] = 0x08804048u;
    rt.run(0x088041C0u, 2); check(c.lo == 21, "MADD return delay slot");
    const float a[4]{2, 3, 5, 0}, b[4]{7, 11, 13, 0};
    c.write_vfpu_vector(a, 0, 3); c.write_vfpu_vector(b, 1, 3);
    c.vfpu_ctrl[0] = c.vfpu_ctrl[1] = 0xFFFFFu; // ignored by VCRS
    c.vfpu_ctrl[2] = 0; rt.run(0x08804100u, 2);
    float result[4]{}; c.read_vfpu_vector(result, 2, 3);
    check(result[0] == 39 && result[1] == 35 && result[2] == 22, "VCRS components/prefixes");
    c.vfpu_ctrl[2] = 0x201u; c.execute_vfpu_vcrs(0, 0, 1); c.read_vfpu_vector(result, 0, 3);
    check(result[0] == 1 && result[1] == 3 && result[2] == 22, "VCRS overlap/mask/saturation");
    const float packed[4]{std::bit_cast<float>(0x7FFFFFFFu), std::bit_cast<float>(0x80000000u),
                          std::bit_cast<float>(0x40000000u), std::bit_cast<float>(0x00800000u)};
    c.write_vfpu_vector(packed, 0, 4); c.vfpu_ctrl[0] = 0xFFF1Bu; c.vfpu_ctrl[2] = 3;
    rt.run(0x08804140u, 2); check(c.vfpu_scalar_bits(0) == 0xFF008001u, "VI2UC swizzle/overlap/ignored modifiers");
    c.vfpu_ctrl[2] = 0x100; rt.run(0x08804140u, 2);
    check(c.vfpu_scalar_bits(0) == 0xFF008001u && c.vfpu_ctrl[0] == 0xE4, "VI2UC mask/prefix reset");
    // Published hardware scalar trace, seed 1.4f; these are observed values.
    const std::uint32_t initial[]{0x3F833333,0x3F833333,0x3F833333,0x3F833333,0x3F833FB3,0x3F8B3FB3,0x3F8F3FB3,0x3F833FB3};
    std::copy(std::begin(initial), std::end(initial), c.vfpu_ctrl.begin()+8);
    rt.run(0x08804180u, 2); check(c.vfpu_scalar_bits(0) == 0x3F93E89Du, "VRNDF1 first hardware output");
    const std::uint32_t state[]{0x3F8096D8,0x3F8084F9,0x3F803333,0x3F80CCCC,0x3F804F4C,0x3F80637A,0x3F803FB3,0x3F80FECC};
    check(std::equal(std::begin(state), std::end(state), c.vfpu_ctrl.begin()+8), "VRNDF1 hardware state");
    rt.run(0x08804180u, 2); check(c.vfpu_scalar_bits(0) == 0x3FF4AC2Fu, "VRNDF1 second hardware output");
    c.vfpu_ctrl[2] = 0x100; auto before = c.vfpu_ctrl; rt.run(0x08804180u, 2);
    check(c.vfpu_scalar_bits(0) == 0x3FF4AC2Fu && before[8] != c.vfpu_ctrl[8], "Masked VRNDF1 advances state");
    c.vfpu_ctrl[2] = 1; rt.run(0x08804180u, 2); check(c.vfpu_scalar_bits(0) == 0x3F800000u, "VRNDF1 saturation");
    check(decode_allegrex(0xD0220080).kind == OpcodeKind::Vfpu, "Unimplemented random vector accepted");
    check(decode_allegrex(0x0085081C).kind == OpcodeKind::Unsupported, "Invalid MADD accepted");
    c.hi = 0; c.lo = 0; c.gpr[4] = 3; c.gpr[5] = 7; c.gpr[31] = 0x088040C8u;
    rt.run(0x0880D000u, 2); check(c.lo == 21 && c.trap.epc == 0x088040C8u, "Sparse dispatcher return");
    rt.run(0x08804240u, 4);
    check(rt.stop_reason().find("Missing HLE import fixture::") != std::string::npos, "Direct import boundary");
    check(!c.trap.valid, "Previous run trap must not leak into import probe");
    check(c.pc == 0x08804280u && c.gpr[31] == 0x08804248u, "Import call PC/RA");
    c.gpr[31] = 0x08804280u; rt.run(0x088041C0u, 4);
    check(rt.stop_reason().find("Missing HLE import fixture::") != std::string::npos, "Indirect local import boundary");
    std::cout << "Generated Allegrex extension execution passed\n";
 } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
