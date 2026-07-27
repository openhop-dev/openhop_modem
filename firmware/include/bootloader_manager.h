#pragma once

#include <cstdint>

namespace BootloaderManager {

enum class Mode : uint8_t { NONE = 0, UF2_USB, BLE_OTA, REBOOT };
constexpr uint32_t TRANSITION_DELAY_MS = 150;

using TransitionCallback = void (*)(Mode mode, void* context);

class DeferredTransition {
public:
    void arm(Mode mode);
    void responseClosed(uint32_t nowMs);
    void responseAborted();
    void poll(uint32_t nowMs, TransitionCallback callback, void* context);
    Mode pending() const { return mode_; }
    bool committed() const { return mode_ != Mode::NONE && responseClosed_; }

private:
    Mode mode_ = Mode::NONE;
    bool responseClosed_ = false;
    uint32_t responseClosedMs_ = 0;
};

// These wrappers deliberately use the Adafruit nRF52 BSP transitions. The
// protocol command uses the BSP helper historically named UF2/USB (GPREGRET
// 0x57); the tested RAK bootloader exposes serial DFU rather than a UF2 disk.
// BLE OTA uses the BSP's distinct transition and is only invoked after an HTTP
// response closes.
[[noreturn]] void enterUf2Dfu();
[[noreturn]] void enterBleOtaDfu();
void execute(Mode mode, void* context = nullptr);

}  // namespace BootloaderManager
