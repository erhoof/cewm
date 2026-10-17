// Unit test for engine/arm_alu.h (shifter, carry/overflow, conditions). Build: see test/CMakeLists.txt (target alu_test).
#include "../engine/arm_alu.h"
#include <cassert>
#include <cstdio>
using namespace cewm::alu;
int main() {
    // LSL: amt 0 keeps carry; 32 -> carry is bit0; >32 -> 0/0.
    assert(ShiftBy(kLsl, 0x80000001, 0, 1).c == 1 && ShiftBy(kLsl, 0x80000001, 0, 1).v == 0x80000001);
    assert(ShiftBy(kLsl, 0x80000001, 1, 0).v == 2 && ShiftBy(kLsl, 0x80000001, 1, 0).c == 1);
    assert(ShiftBy(kLsl, 3, 32, 0).v == 0 && ShiftBy(kLsl, 3, 32, 0).c == 1);
    assert(ShiftBy(kLsl, 3, 33, 1).c == 0);
    // LSR #32 (imm encoding 0): result 0, carry bit31.
    assert(ShiftBy(kLsr, 0x80000000, 32, 0).v == 0 && ShiftBy(kLsr, 0x80000000, 32, 0).c == 1);
    assert(ShiftBy(kLsr, 6, 1, 0).v == 3 && ShiftBy(kLsr, 6, 2, 0).c == 1);
    // ASR >= 32 fills with sign.
    assert(ShiftBy(kAsr, 0x80000000, 40, 0).v == 0xFFFFFFFF && ShiftBy(kAsr, 0x80000000, 40, 0).c == 1);
    assert(ShiftBy(kAsr, 0xFFFFFFF0, 4, 0).v == 0xFFFFFFFF);
    // ROR by register multiple of 32: value unchanged, carry = bit31.
    assert(ShiftBy(kRor, 0x80000000, 32, 0).v == 0x80000000 && ShiftBy(kRor, 0x80000000, 32, 0).c == 1);
    assert(ShiftBy(kRor, 1, 1, 0).v == 0x80000000 && ShiftBy(kRor, 1, 1, 0).c == 1);
    // RRX.
    assert(ShiftBy(kRrx, 3, 1, 1).v == 0x80000001 && ShiftBy(kRrx, 3, 1, 1).c == 1);
    // Arithmetic flags.
    auto s = AddWC(0xFFFFFFFF, 1, 0); assert(s.v == 0 && s.c == 1 && s.o == 0);
    s = AddWC(0x7FFFFFFF, 1, 0);      assert(s.v == 0x80000000 && s.c == 0 && s.o == 1);
    auto d = DataProc(0xA, 5, 5, 0, 0); assert(d.v == 0 && d.c == 1 && !d.writes);  // CMP equal: C set (no borrow)
    d = DataProc(0x2, 3, 5, 0, 0);      assert(d.v == 0xFFFFFFFE && d.c == 0);       // SUB borrow: C clear
    d = DataProc(0x6, 5, 3, 0, 0);      assert(d.v == 1);                            // SBC with C=0 subtracts 1 more: 5-3-1... =1
    d = DataProc(0xD, 0, 0x55, 1, 0);   assert(d.v == 0x55 && d.c == 1 && !d.arith); // MOV keeps shifter carry
    d = DataProc(0xF, 0, 0, 0, 0);      assert(d.v == 0xFFFFFFFF);                   // MVN
    // Conditions: GE (A) is N==V, GT (C) also !Z, LS (9) !C||Z.
    assert(CondPasses(0xA, 1, 0, 0, 1) && !CondPasses(0xC, 1, 1, 0, 1) && CondPasses(0x9, 0, 0, 0, 0) && CondPasses(0xE, 0, 0, 0, 0));
    puts("alu_test ok");
}
