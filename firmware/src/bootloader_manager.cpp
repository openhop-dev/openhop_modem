#include "bootloader_manager.h"

namespace BootloaderManager {

void DeferredTransition::arm(Mode mode) {
    if (mode_ != Mode::NONE || mode == Mode::NONE) return;
    mode_ = mode;
    responseClosed_ = false;
    responseClosedMs_ = 0;
}

void DeferredTransition::responseClosed(uint32_t nowMs) {
    if (mode_ == Mode::NONE || responseClosed_) return;
    responseClosed_ = true;
    responseClosedMs_ = nowMs;
}

void DeferredTransition::responseAborted() {
    if (responseClosed_) return;
    mode_ = Mode::NONE;
    responseClosed_ = false;
    responseClosedMs_ = 0;
}

void DeferredTransition::poll(uint32_t nowMs, TransitionCallback callback, void* context) {
    if (mode_ == Mode::NONE || !responseClosed_ ||
        static_cast<uint32_t>(nowMs - responseClosedMs_) < TRANSITION_DELAY_MS) {
        return;
    }
    const Mode mode = mode_;
    mode_ = Mode::NONE;
    responseClosed_ = false;
    responseClosedMs_ = 0;
    if (callback) callback(mode, context);
}

}  // namespace BootloaderManager

#if defined(NRF52_SERIES)
#include <Arduino.h>

namespace BootloaderManager {

[[noreturn]] void enterUf2Dfu() {
    ::enterUf2Dfu();
    while (true) {}
}

[[noreturn]] void enterBleOtaDfu() {
    ::enterOTADfu();
    while (true) {}
}

void execute(Mode mode, void*) {
    if (mode == Mode::UF2_USB) enterUf2Dfu();
    if (mode == Mode::BLE_OTA) enterBleOtaDfu();
    if (mode == Mode::REBOOT) NVIC_SystemReset();
}

}  // namespace BootloaderManager
#else
namespace BootloaderManager {
[[noreturn]] void enterUf2Dfu() { while (true) {} }
[[noreturn]] void enterBleOtaDfu() { while (true) {} }
void execute(Mode, void*) {}
}  // namespace BootloaderManager
#endif
