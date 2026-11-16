#include "rf_fem_state.h"

#include <cassert>
#include <iostream>

namespace {

constexpr uint8_t kHeltecV43Caps = FEM_STATE_RX_LNA;
constexpr uint8_t kStationG3Caps = FEM_STATE_RX_LNA | FEM_STATE_TX_PA;

void expectApplied(uint8_t capability, uint8_t current, uint8_t apply,
                   uint8_t requested, uint8_t expected) {
    uint8_t next = 0xFF;
    const auto status = RFFrontEnd::applyFemMask(capability, current, apply, requested, next);
    assert(status == RFFrontEnd::FemApplyStatus::Applied);
    assert(next == expected);
}

void expectUnsupported(uint8_t capability, uint8_t current, uint8_t apply, uint8_t requested) {
    uint8_t next = 0xFF;
    const auto status = RFFrontEnd::applyFemMask(capability, current, apply, requested, next);
    assert(status == RFFrontEnd::FemApplyStatus::Unsupported);
    assert(next == current);
}

void testHeltecV43LnaOnly() {
    expectApplied(kHeltecV43Caps, 0x00, FEM_STATE_RX_LNA, FEM_STATE_RX_LNA, FEM_STATE_RX_LNA);
    expectApplied(kHeltecV43Caps, FEM_STATE_RX_LNA, FEM_STATE_RX_LNA, 0x00, 0x00);
    expectApplied(kHeltecV43Caps, FEM_STATE_RX_LNA, 0x00, FEM_STATE_TX_PA, FEM_STATE_RX_LNA);
    expectUnsupported(kHeltecV43Caps, 0x00, FEM_STATE_TX_PA, FEM_STATE_TX_PA);
    expectUnsupported(kHeltecV43Caps, FEM_STATE_RX_LNA,
                      FEM_STATE_RX_LNA | FEM_STATE_TX_PA, FEM_STATE_RX_LNA);
}

void testStationG3IndependentBits() {
    expectApplied(kStationG3Caps, FEM_STATE_RX_LNA, FEM_STATE_TX_PA, FEM_STATE_TX_PA,
                  FEM_STATE_RX_LNA | FEM_STATE_TX_PA);
    expectApplied(kStationG3Caps, FEM_STATE_RX_LNA | FEM_STATE_TX_PA, FEM_STATE_RX_LNA, 0x00,
                  FEM_STATE_TX_PA);
    expectApplied(kStationG3Caps, 0x00, FEM_STATE_RX_LNA | FEM_STATE_TX_PA,
                  FEM_STATE_RX_LNA | FEM_STATE_TX_PA,
                  FEM_STATE_RX_LNA | FEM_STATE_TX_PA);
    expectUnsupported(kStationG3Caps, FEM_STATE_RX_LNA, 0x04, 0x04);
    expectUnsupported(kStationG3Caps, 0x00, 0xFF, FEM_STATE_RX_LNA);
}

}  // namespace

int main() {
    testHeltecV43LnaOnly();
    testStationG3IndependentBits();
    std::cout << "rf fem state tests passed\n";
    return 0;
}
