#include "radio_hardware.h"
#include <SPI.h>
#include <new>
#include <type_traits>

int16_t OpenHopSX1262::startReceive() {
    return SX1262::startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF,
        RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
        RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
}
int16_t OpenHopSX1262::applyRegisterPatch08B5() {
    uint8_t value = 0;
    const int16_t state = readRegister(0x08B5, &value, 1);
    if (state != RADIOLIB_ERR_NONE) return state;
    value |= 0x01;
    return writeRegister(0x08B5, &value, 1);
}
int16_t OpenHopSX1262::strictIrqStatus(uint8_t status) {
    // A completed TX may already be in standby. Validate the awake status
    // encoding, not one particular operating mode, before trusting IRQ data.
    // Bits 7 and 0 are RFU, not zero-required validity bits. The fitted module
    // returns D4 for a real RX_DONE sample; rejecting its bit 7 loses all RX.
    // Validate only the defined awake-mode and native command-status fields.
    switch (status & 0x70U) {
    case RADIOLIB_SX126X_STATUS_MODE_STDBY_RC:
    case RADIOLIB_SX126X_STATUS_MODE_STDBY_XOSC:
    case RADIOLIB_SX126X_STATUS_MODE_FS:
    case RADIOLIB_SX126X_STATUS_MODE_RX:
    case RADIOLIB_SX126X_STATUS_MODE_TX: break;
    default: return RADIOLIB_ERR_SPI_CMD_INVALID;
    }
    return SX126x::SPIparseStatus(status);
}
OpenHopSX1262::IrqSample OpenHopSX1262::readIrqChecked() {
    Module* module = getMod();
    // The stricter parser is scoped to this one owner's one IRQ transaction.
    // Configuration/SetTx/Standby parsing and the dependency remain unchanged.
    struct Restore {
        Module* module; Module::SPIparseStatusCb_t parser;
        ~Restore() { module->spiConfig.parseStatusCb = parser; }
    } restore{module, module->spiConfig.parseStatusCb};
    module->spiConfig.parseStatusCb = strictIrqStatus;
    uint8_t bytes[2]{};
    const int16_t state = module->SPIreadStream(RADIOLIB_SX126X_CMD_GET_IRQ_STATUS, bytes, 2, true, false);
    const uint16_t flags = (uint16_t(bytes[0]) << 8) | bytes[1];
    constexpr uint16_t loraBits = RADIOLIB_SX126X_IRQ_TX_DONE | RADIOLIB_SX126X_IRQ_RX_DONE |
        RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID |
        RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_HEADER_ERR |
        RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_CAD_DONE |
        RADIOLIB_SX126X_IRQ_CAD_DETECTED | RADIOLIB_SX126X_IRQ_TIMEOUT;
    return {state, flags, state == RADIOLIB_ERR_NONE && (flags & uint16_t(~loraBits)) == 0};
}
bool OpenHopSX1262::dio1Asserted() {
    Module* module = getMod();
    return module->hal->digitalRead(module->getIrq()) == module->hal->GpioLevelHigh;
}
namespace {
template <typename Begin> bool checkedBegin(Begin begin) {
    if constexpr (std::is_void<decltype(begin())>::value) { begin(); return true; }
    else return static_cast<bool>(begin());
}
struct Bus {
    SPIClass* spi = nullptr;
    int8_t sck = -1, miso = -1, mosi = -1;
};
// Bus ownership is intentionally shared; Module/chip and all RF state are not.
Bus buses[3];
SPIClass& defaultBus() {
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
    static SPIClass loraSpi(0);
    return loraSpi;
#elif defined(BOARD_RAK4631_WISMESH_ETH) || defined(BOARD_RAK4631_USB)
    static SPIClass loraSpi(NRF_SPIM2, 45, 43, 44);
    return loraSpi;
#else
    return SPI;
#endif
}
SPIClass* busFor(const RadioDescriptor& d) {
    if (d.spi_bus < -1 || d.spi_bus > 1) return nullptr;
#ifndef ARDUINO_ARCH_ESP32
    if (d.spi_bus != -1) return nullptr;
#endif
    Bus& bus = buses[d.spi_bus + 1];
    const BoardConfig& h = *d.hardware;
    if (bus.spi) {
        if (bus.sck != h.pin_lora_sck || bus.miso != h.pin_lora_miso ||
            bus.mosi != h.pin_lora_mosi) return nullptr;
        return bus.spi;
    }
    SPIClass* spi = nullptr;
    if (d.spi_bus == -1) spi = &defaultBus();
#ifdef ARDUINO_ARCH_ESP32
    else spi = new (std::nothrow) SPIClass(d.spi_bus == 0 ? FSPI : HSPI);
#endif
    if (!spi) return nullptr;
    bool started;
#ifdef ARDUINO_ARCH_ESP32
    if (h.pin_lora_sck >= 0 || h.pin_lora_miso >= 0 || h.pin_lora_mosi >= 0)
        started = checkedBegin([&]() { return spi->begin(h.pin_lora_sck, h.pin_lora_miso, h.pin_lora_mosi); });
    else started = checkedBegin([&]() { return spi->begin(); });
#else
    started = checkedBegin([&]() { return spi->begin(); });
#endif
    if (!started) {
        if (d.spi_bus != -1) delete spi;
        return nullptr; // never cache a bus that could not be initialized
    }
    bus = {spi, h.pin_lora_sck, h.pin_lora_miso, h.pin_lora_mosi};
    return spi;
}
uint32_t bootStartedMs = 0;
}
RadioHardware* RadioHardware::create(const RadioDescriptor& d) {
    if (!d.hardware || !d.hardware->has_lora_radio) return nullptr;
    SPIClass* spi = busFor(d);
    if (!spi) return nullptr;
    const BoardConfig& h = *d.hardware;
    Module* module = new (std::nothrow) Module(h.pin_lora_nss, h.pin_lora_dio1,
                                            h.pin_lora_rst, h.pin_lora_busy, *spi);
    if (!module) return nullptr;
    OpenHopSX1262* chip = new (std::nothrow) OpenHopSX1262(module);
    auto* owner = chip ? new (std::nothrow) RadioHardware(module, chip) : nullptr;
    if (!owner) {
        delete chip; delete module;
    }
    return owner;
}
RadioHardware::~RadioHardware() {
    delete chip_; delete module_;
}

bool RadioHardware::validBindings() {
    if (!RadioLayout::valid()) return false;
#ifndef ARDUINO_ARCH_ESP32
    // The legacy W5100S and USB adapters have no multi-listener backend.
    if (RadioLayout::count() != 1) return false;
#endif
    for (size_t i = 0; i < RadioLayout::count(); ++i) {
        const auto& d = RadioLayout::at(i);
        if (d.spi_bus > 1) return false;
#ifndef ARDUINO_ARCH_ESP32
        if (d.spi_bus != -1) return false;
#endif
        for (size_t j = 0; j < i; ++j) {
            const auto& other = RadioLayout::at(j);
            if (!d.hardware->has_lora_radio || !other.hardware->has_lora_radio) continue;
            // Default-vs-explicit may alias an SDK peripheral: unsupported mixed
            // layouts must fail before any bus is rebound or listener starts.
            if ((d.spi_bus == -1) != (other.spi_bus == -1)) return false;
        }
    }
    return true;
}
void RadioHardware::prepareBoot(uint8_t enabledMask) {
    bootStartedMs = millis();
    if (!validBindings()) return;
    for (size_t i = 0; i < RadioLayout::count(); ++i) {
        const auto& h = *RadioLayout::at(i).hardware;
        if (!(enabledMask & (1U << i)) || !h.has_lora_radio) continue;
        // Every enabled peer must be inactive before the first chip or any
        // shared-bus peripheral can clock SPI. Module::init handles only its
        // own CS. Preserve legacy one-radio ordering and disabled-slot silence.
        if (RadioLayout::count() > 1) {
            pinMode(h.pin_lora_nss, OUTPUT);
            digitalWrite(h.pin_lora_nss, HIGH);
        }
        if (h.rf_switch.en_pin >= 0) {
            pinMode(h.rf_switch.en_pin, OUTPUT);
            digitalWrite(h.rf_switch.en_pin, LOW);
        }
        for (size_t n = 0; n < h.static_gpio_count && n < 4; ++n) {
            if (h.static_gpios[n].pin < 0) continue;
            pinMode(h.static_gpios[n].pin, OUTPUT);
            digitalWrite(h.static_gpios[n].pin, h.static_gpios[n].level_high ? HIGH : LOW);
        }
        if (h.pin_lora_tx_led >= 0) {
            pinMode(h.pin_lora_tx_led, OUTPUT);
            digitalWrite(h.pin_lora_tx_led, h.lora_tx_led_active_high ? LOW : HIGH);
        }
    }
}
void RadioHardware::enableRails(uint8_t enabledMask) {
    if (!validBindings()) return;
    uint32_t hold = 0;
    for (size_t i = 0; i < RadioLayout::count(); ++i) {
        const auto& h = *RadioLayout::at(i).hardware;
        if ((enabledMask & (1U << i)) && h.has_lora_radio && h.rf_switch.en_pin >= 0 && h.rf_switch.en_low_hold_ms > hold)
            hold = h.rf_switch.en_low_hold_ms;
    }
    const uint32_t elapsed = millis() - bootStartedMs;
    if (elapsed < hold) delay(hold - elapsed); // boot-only, never the service path
    bool raised = false;
    for (size_t i = 0; i < RadioLayout::count(); ++i) {
        const auto& h = *RadioLayout::at(i).hardware;
        if (!(enabledMask & (1U << i)) || !h.has_lora_radio || h.rf_switch.en_pin < 0) continue;
        // A shared supply is raised once, only after every front end is safe.
        bool seen = false;
        for (size_t j = 0; j < i; ++j) {
            const auto& o = *RadioLayout::at(j).hardware;
            if ((enabledMask & (1U << j)) && o.has_lora_radio && o.rf_switch.en_pin == h.rf_switch.en_pin) seen = true;
        }
        if (!seen) { digitalWrite(h.rf_switch.en_pin, HIGH); raised = true; }
    }
    if (raised) delay(20);
}
