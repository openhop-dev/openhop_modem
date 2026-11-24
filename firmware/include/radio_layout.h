#pragma once

#include "board_config.h"
#include <stddef.h>
#include <string.h>

namespace RadioLayout {

constexpr size_t CAPACITY = 4;

// Immutable board-declared ownership. No radio/SPI construction or GPIO writes
// occur here; callers validate before constructing hardware or opening ports.
class Plan {
public:
    explicit Plan(const BoardConfig& board)
        : board_(board), primary_{"RF1", &board, 0, -1} {}

    size_t count() const {
        return board_.radios ? board_.radio_count : 1;
    }

    const RadioDescriptor& at(size_t index) const {
        if (index >= count() || index >= CAPACITY) return invalid_;
        return board_.radios ? board_.radios[index] : primary_;
    }

    uint16_t port(size_t index, uint16_t configuredPrimaryPort = 5055) const {
        const auto& descriptor = at(index);
        if (!descriptor.hardware) return 0;
        if (descriptor.tcp_port) return descriptor.tcp_port;
        return index == 0 ? (configuredPrimaryPort ? configuredPrimaryPort : 5055) : 0;
    }

    bool fixedPorts() const {
        return board_.radios && count() > 0 && at(0).tcp_port != 0;
    }

    bool acceptsPrimaryPort(uint16_t requested) const {
        return requested != 0 && requested != 80 &&
            (!fixedPorts() || requested == at(0).tcp_port);
    }

    bool valid() const {
        if ((board_.radios == nullptr) != (board_.radio_count == 0)) return false;
        if (count() == 0 || count() > CAPACITY) return false;
        for (size_t i = 0; i < count(); ++i) {
            const auto& d = at(i);
            if (!d.hardware || !d.label || !*d.label || d.spi_bus < -1) return false;
            if ((count() > 1 && !d.tcp_port) || port(i) == 80) return false;
            const auto& hw = *d.hardware;
            if (hw.has_lora_radio && (hw.pin_lora_nss < 0 || hw.pin_lora_rst < 0 ||
                hw.pin_lora_busy < 0 || hw.pin_lora_dio1 < 0)) return false;
            for (size_t j = 0; j < i; ++j) {
                const auto& other = at(j);
                if (port(i) == port(j) || strcmp(d.label, other.label) == 0) return false;
                const auto& ohw = *other.hardware;
                if (!hw.has_lora_radio || !ohw.has_lora_radio) continue;
                const int8_t controls[] = {hw.pin_lora_nss, hw.pin_lora_rst,
                    hw.pin_lora_busy, hw.pin_lora_dio1};
                const int8_t otherControls[] = {ohw.pin_lora_nss, ohw.pin_lora_rst,
                    ohw.pin_lora_busy, ohw.pin_lora_dio1};
                for (auto pin : controls) {
                    for (auto otherPin : otherControls) {
                        if (pin >= 0 && pin == otherPin) return false;
                    }
                }
                if (d.spi_bus == other.spi_bus &&
                    (hw.pin_lora_sck != ohw.pin_lora_sck ||
                     hw.pin_lora_miso != ohw.pin_lora_miso ||
                     hw.pin_lora_mosi != ohw.pin_lora_mosi)) return false;
            }
        }
        return true;
    }

private:
    const BoardConfig& board_;
    const RadioDescriptor primary_;
    const RadioDescriptor invalid_{nullptr, nullptr, 0, -1};
};

inline const Plan& plan() { static const Plan value(BOARD); return value; }
inline size_t count() { return plan().count(); }
inline const RadioDescriptor& at(size_t index) { return plan().at(index); }
inline uint16_t port(size_t index, uint16_t configuredPrimaryPort = 5055) {
    return plan().port(index, configuredPrimaryPort);
}
inline bool fixedPorts() { return plan().fixedPorts(); }
inline bool acceptsPrimaryPort(uint16_t requested) { return plan().acceptsPrimaryPort(requested); }
inline bool valid() { return plan().valid(); }

} // namespace RadioLayout
