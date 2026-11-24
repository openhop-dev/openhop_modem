#include "radio_runtime.h"
#include "pa_ramp.h"
#include <string.h>

namespace {
RadioRuntime* irqOwners[RadioLayout::CAPACITY]{};
#if defined(ESP32)
#define OPENHOP_ISR IRAM_ATTR
#else
#define OPENHOP_ISR
#endif
void OPENHOP_ISR irq0() { if (irqOwners[0]) irqOwners[0]->onIrq(); }
void OPENHOP_ISR irq1() { if (irqOwners[1]) irqOwners[1]->onIrq(); }
void OPENHOP_ISR irq2() { if (irqOwners[2]) irqOwners[2]->onIrq(); }
void OPENHOP_ISR irq3() { if (irqOwners[3]) irqOwners[3]->onIrq(); }
void (*const irqThunks[RadioLayout::CAPACITY])() = {irq0, irq1, irq2, irq3};
}
RadioRuntime::RadioRuntime(uint8_t id, const RadioDescriptor& descriptor,
                           const Hooks& hooks, RFFrontEnd::Controller* frontEnd, bool enabled)
    : id_(id), enabled_(enabled), descriptor_(descriptor), hooks_(hooks),
      ownedFrontEnd_(*descriptor.hardware, false),
      frontEnd_(frontEnd ? frontEnd : &ownedFrontEnd_) {
    if (descriptor.hardware->boot_rx_frequency_hz)
        config_.freq_hz = descriptor.hardware->boot_rx_frequency_hz;
    frontEnd_->setMutationGuard([](void* context, bool receiveSensitive) {
        auto* owner = static_cast<RadioRuntime*>(context);
        return owner->present() && owner->optionsReady_ && !owner->busy() &&
               (!receiveSensitive || !owner->receiving());
    }, this);
}
RadioRuntime::~RadioRuntime() {
    if (id_ < RadioLayout::CAPACITY && irqOwners[id_] == this) irqOwners[id_] = nullptr;
    frontEnd_->setMutationGuard(nullptr, nullptr);
    delete hardware_;
}
bool RadioRuntime::begin() {
    if (!enabled_) return false;
    if (initialized_ || hardware_) return ready_;
    if (!present() || id_ >= RadioLayout::CAPACITY || irqOwners[id_]) return false;
    frontEnd_->begin(); // conservative GPIO state must precede chip initialization
    hardware_ = RadioHardware::create(descriptor_);
    if (!hardware_) return false;
    const BoardConfig& h = *descriptor_.hardware;
    const float initialFrequency = h.boot_rx_frequency_hz ? config_.freq_hz / 1e6f : 434.0f;
    if (chip().begin(initialFrequency, 125.0f, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                     10, 8, h.use_dio3_tcxo ? h.tcxo_voltage : 0.0f) != RADIOLIB_ERR_NONE)
        return false;
    initialized_ = true;
    if (h.rf_switch.dio2_as_rf_switch && chip().setDio2AsRfSwitch(true) != RADIOLIB_ERR_NONE) {
        fault(); return false;
    }
    if (h.rf_switch.rx_pin >= 0 || h.rf_switch.tx_pin >= 0)
        chip().setRfSwitchPins(h.rf_switch.rx_pin >= 0 ? (uint32_t)h.rf_switch.rx_pin : RADIOLIB_NC,
                              h.rf_switch.tx_pin >= 0 ? (uint32_t)h.rf_switch.tx_pin : RADIOLIB_NC);
    if (h.sx126x_current_limit_ma > 0 && chip().setCurrentLimit(h.sx126x_current_limit_ma) != RADIOLIB_ERR_NONE) {
        fault(); return false;
    }
    if (h.sx126x_rx_boosted_gain && setBoost(true, false).outcome != RxBoostControl::Outcome::Applied) {
        fault(); return false;
    }
    if (h.sx126x_register_patch && chip().applyRegisterPatch08B5() != RADIOLIB_ERR_NONE) {
        fault(); return false;
    }
    optionsReady_ = true;
    // Bind terminal IRQ ownership even if the boot preset fails. A later
    // successful host configuration must not recover into a receiver with no ISR.
    irqOwners[id_] = this;
    chip().setDio1Action(irqThunks[id_]);
    if (!applyConfig(config_)) return false;
    if (!startReceive()) return false;
    agcSchedule_.recordAttempt(millis());
    return true;
}
bool RadioRuntime::applyConfig(const RadioConfig& cfg) {
    txReady_ = false; // invalidate before *any* hardware mutation
    ready_ = false;
    configApplied_ = false;
    if (!initialized_ || !optionsReady_) return false;
    // SX1262's unchanged wire contract accepts -9..22 dBm chip drive.
    // Reject an invalid host request before standby or any setter, rather
    // than silently authorizing TX at a clamped but differently echoed value.
    if (cfg.power_dbm < -9 || cfg.power_dbm > 22) return false;
    frontEnd_->prepareStandby();
    if (chip().standby() != RADIOLIB_ERR_NONE ||
        chip().setFrequency(cfg.freq_hz / 1e6f) != RADIOLIB_ERR_NONE ||
        chip().setBandwidth(cfg.bandwidth_hz / 1000.0f) != RADIOLIB_ERR_NONE ||
        chip().setSpreadingFactor(cfg.sf) != RADIOLIB_ERR_NONE ||
        chip().setCodingRate(cfg.cr) != RADIOLIB_ERR_NONE) return false;
    const auto& h = *descriptor_.hardware;
    int8_t power = cfg.power_dbm > h.max_tx_power_dbm ? h.max_tx_power_dbm : cfg.power_dbm;
    const uint8_t ramp = h.pa_ramp_time_us == 1700 ? RADIOLIB_SX126X_PA_RAMP_1700U : 0;
    const int ocpBefore = (int)chip().getCurrentLimit();
    const int16_t powerState = applyOutputPowerAndRamp(chip(), power, ramp);
    const int ocpAfter = (int)chip().getCurrentLimit();
    if (hooks_.diagnostic) {
        char line[224];
        snprintf(line, sizeof(line), "applyConfig board=%s pwr_req=%d pwr=%d max=%d setOutputPower=%d ocp_before=%dmA ocp_after=%dmA",
                 h.name, (int)cfg.power_dbm, (int)power, (int)h.max_tx_power_dbm,
                 (int)powerState, ocpBefore, ocpAfter);
        hooks_.diagnostic(id_, line);
    }
    if (powerState != RADIOLIB_ERR_NONE ||
        chip().setSyncWord(cfg.syncword) != RADIOLIB_ERR_NONE ||
        chip().setPreambleLength(cfg.preamble_len) != RADIOLIB_ERR_NONE ||
        chip().explicitHeader() != RADIOLIB_ERR_NONE ||
        chip().setCRC(1) != RADIOLIB_ERR_NONE ||
        chip().invertIQ(false) != RADIOLIB_ERR_NONE ||
        chip().autoLDRO() != RADIOLIB_ERR_NONE) return false;
    config_ = cfg;
    configApplied_ = true;
    ready_ = true;
    txReady_ = hostConfigured_ && !rxBoost_.fault;
    return true;
}
bool RadioRuntime::startReceive() {
    if (!present() || !initialized_ || !optionsReady_ || !configApplied_) return false;
    if (standby_) { frontEnd_->prepareStandby(); return true; }
    frontEnd_->prepareReceive();
    irqPending_ = false;
    if (chip().startReceive() != RADIOLIB_ERR_NONE) { fault(); return false; }
    ready_ = true;
    rxActivity_ = rxHeader_ = false;
    return true;
}
void RadioRuntime::fault() {
    ready_ = txReady_ = false;
    rxBoost_.fault = true;
    frontEnd_->prepareStandby();
}
bool RadioRuntime::current(const ResponseRoute& r) const {
    return r.radio == id_ && (r.transport == TransportSource::TCP || id_ == 0) &&
           hooks_.current && hooks_.current(r);
}
void RadioRuntime::reply(uint8_t cmd, const uint8_t* data, uint16_t len, const ResponseRoute& r) {
    if (!current(r) || !hooks_.reply) return;
    if (!replyPending_ && hooks_.canReply && hooks_.canReply(r)) {
        hooks_.reply(r, cmd, data, len);
    } else if (!replyPending_ && len <= sizeof(replyPayload_)) {
        replyPending_ = true; replyRoute_ = r; replyCmd_ = cmd; replyLength_ = len;
        if (len) memcpy(replyPayload_, data, len);
    }
}
void RadioRuntime::error(uint8_t code, const ResponseRoute& r) { reply(CMD_ERROR, &code, 1, r); }
StatusResp RadioRuntime::status() const {
    StatusResp result = counters_;
    result.uptime_sec = millis() / 1000;
    result.noise_floor_x10 = (int16_t)(noiseFloor_ * 10.0f);
    result.radio_state = operation_ == Operation::Tx ? 1 : (!ready_ || rxBoost_.fault ? 2 : 0);
    if (hooks_.deviceStatus) hooks_.deviceStatus(result);
    return result;
}
void RadioRuntime::receivePacket() {
    const size_t len = chip().getPacketLength();
    if (!len || len > MAX_LORA_PAYLOAD) { startReceive(); return; }
    uint8_t payload[6 + MAX_LORA_PAYLOAD];
    if (chip().readData(payload + 6,len) != RADIOLIB_ERR_NONE) {
        ++counters_.crc_errors; startReceive(); return;
    }
    int16_t rssi = (int16_t)chip().getRSSI();
    int16_t snr = (int16_t)(chip().getSNR() * 10.0f);
    memcpy(payload, &rssi, 2); memcpy(payload + 2, &snr, 2); memcpy(payload + 4, &rssi, 2);
    ++counters_.rx_count; counters_.last_rssi = rssi; counters_.last_snr = snr;
    lastPacketMs_ = millis();
    if (hooks_.event) hooks_.event(id_, CMD_RX_PACKET, payload, 6 + len);
    startReceive();
}
void RadioRuntime::serviceIrq() {
    if (!initialized_) return;
    if (operation_ == Operation::Idle && (!ready_ || standby_)) return;
    const uint32_t edgeGeneration = irqCount_;
    const bool notified = irqPending_;
    // Consume before the potentially delayed SPI read. An overlapping ISR is
    // retained below; never disable interrupts across a radio/BUSY transaction.
    irqPending_ = false;
    const bool asserted = chip().dio1Asserted();
    const bool terminalOperation = operation_ == Operation::Tx || operation_ == Operation::Cad || operation_ == Operation::AutoCad;
    uint32_t irq = 0;
    // Do not poll SPI throughout an on-air TX/CAD. A real latched DIO also
    // recovers a missed edge. Idle/backoff retries may resolve stale RX notices.
    if (asserted || (!terminalOperation && notified)) {
        const auto sample = chip().readIrqChecked();
        if (sample.valid) irq = sample.flags;
        else irqPending_ = true; // one retry on a later invocation, never a loop
    }
    if (irqCount_ != edgeGeneration) irqPending_ = true;
    if (operation_ == Operation::Tx) {
        // Neither garbage containing bit 0 nor a stale/wrong-kind edge may
        // finish TX. Existing deadlines below advance even without a valid IRQ.
        if (asserted && irq == RADIOLIB_SX126X_IRQ_TX_DONE) {
            const bool finished = chip().finishTransmit() == RADIOLIB_ERR_NONE;
            if (!finished) fault();
            if (finished) ++counters_.tx_count;
            finishOperation(finished ? 0 : ERR_RADIO_FAULT);
        } else if ((uint32_t)(millis() - operationStartedMs_) >= 4500U) {
            const bool finished = chip().finishTransmit() == RADIOLIB_ERR_NONE;
            if (!finished) fault();
            const bool recovered = applyConfig(config_);
            if (!recovered) fault();
            finishOperation(finished ? ERR_TX_TIMEOUT : ERR_RADIO_FAULT);
        }
        return;
    }
    if (operation_ == Operation::Cad || operation_ == Operation::AutoCad) {
        const bool automatic = operation_ == Operation::AutoCad;
        if (!current(owner_)) { finishOperation(); return; }
        constexpr uint32_t cadBits = RADIOLIB_SX126X_IRQ_CAD_DONE | RADIOLIB_SX126X_IRQ_CAD_DETECTED;
        if (asserted && (irq & RADIOLIB_SX126X_IRQ_CAD_DONE) && !(irq & ~cadBits)) {
            if (chip().clearIrqFlags(RADIOLIB_SX126X_IRQ_CAD_DONE |
                                     RADIOLIB_SX126X_IRQ_CAD_DETECTED) != RADIOLIB_ERR_NONE) {
                fault(); finishOperation(ERR_RADIO_FAULT); return;
            }
            const uint8_t detected = (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) ? 1 : 0;
            if (!automatic) {
                if (finishOperation()) reply(CMD_CAD_RESP, &detected, 1, owner_);
            } else if (!detected) {
                startTx();
            } else if (attempts_ >= 2) {
                finishOperation(ERR_CHANNEL_BUSY);
            } else {
                operation_ = Operation::Backoff;
                operationStartedMs_ = millis();
                backoffMs_ = 50U + micros() % 150U;
                if (!startReceive()) finishOperation(ERR_RADIO_FAULT);
            }
        } else if ((uint32_t)(millis() - operationStartedMs_) >= (automatic ? 200U : 500U)) {
            const bool recovered = applyConfig(config_);
            if (!recovered) fault();
            finishOperation(automatic ? ERR_CHANNEL_BUSY : ERR_CAD_FAILED);
        }
        return;
    }
    if (operation_ == Operation::Backoff) {
        // Listen during jitter; terminal packets on this owner still belong to RX.
        if (asserted && (irq & RADIOLIB_SX126X_IRQ_RX_DONE)) receivePacket();
        if (!current(owner_)) { operation_ = Operation::Idle; return; }
        if ((uint32_t)(millis() - operationStartedMs_) >= backoffMs_) {
            if (receiving()) {
                operation_ = Operation::Idle;
                error(ERR_CHANNEL_BUSY, owner_); // preserve the packet already in flight
            } else startCad(true);
        }
        return;
    }
    if (!ready_ || standby_ || operation_ != Operation::Idle) return;
    if (asserted && (irq & RADIOLIB_SX126X_IRQ_RX_DONE)) receivePacket();
}
RxBoostControl::Result RadioRuntime::setBoost(bool enabled, bool resume) {
    auto result = RxBoostControl::set(rxBoost_, enabled, resume, (int16_t)RADIOLIB_ERR_NONE,
        [this]() { frontEnd_->prepareStandby(); return chip().standby(); },
        [this](bool on) { return chip().setRxBoostedGainMode(on); },
        [this]() { return startReceive(); });
    if (rxBoost_.fault) { ready_ = false; txReady_ = false; }
    return result;
}
bool RadioRuntime::command(uint8_t cmd, const uint8_t* p, uint16_t len, const ResponseRoute& r) {
    if (!current(r)) return true;
    // Only RF opcodes belong here. Device commands (including PING) stay live
    // during pending operations and must reach main's device dispatcher.
    switch (cmd) {
    case CMD_TX_REQUEST: case CMD_SET_CONFIG: case CMD_GET_CONFIG:
    case CMD_GET_RF_CAPS: case CMD_GET_AGC_INTERVAL: case CMD_SET_AGC_INTERVAL:
    case CMD_GET_FEM_STATE: case CMD_SET_FEM_STATE: case CMD_GET_RX_BOOST: case CMD_SET_RX_BOOST:
    case CMD_STATUS_REQ: case CMD_NOISE_REQ: case CMD_CAD_REQUEST: case CMD_RX_START:
    case CMD_SET_CAD_PARAMS: case CMD_RADIO_STANDBY: case CMD_RADIO_RESUME: case CMD_SET_AUTO_CAD:
        break;
    default: return false;
    }
    if (replyPending_ && !current(replyRoute_)) replyPending_ = false;
    if (replyPending_) return true; // output backpressure, never mutate without reply capacity
    // Status is cached RF counters plus device telemetry; it needs no driver.
    if (!optionsReady_ && cmd != CMD_STATUS_REQ) { error(ERR_NO_RADIO, r); return true; }
    if (busy() && cmd != CMD_GET_CONFIG && cmd != CMD_STATUS_REQ &&
        cmd != CMD_NOISE_REQ && cmd != CMD_GET_RF_CAPS &&
        cmd != CMD_GET_AGC_INTERVAL && cmd != CMD_GET_FEM_STATE &&
        cmd != CMD_GET_RX_BOOST) { error(ERR_RADIO_BUSY, r); return true; }
    switch (cmd) {
    case CMD_TX_REQUEST:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (!txReady_) { error(ERR_INVALID_CONFIG, r); break; }
        if (!len || len > MAX_LORA_PAYLOAD || !p) { error(ERR_PAYLOAD_TOO_BIG, r); break; }
        if (!standby_ && receiving()) { error(ERR_CHANNEL_BUSY, r); break; }
        owner_ = r; txLength_ = len; memcpy(txPayload_, p, len);
        attempts_ = 0;
        if (autoCad_) startCad(true); else startTx();
        break;
    case CMD_CAD_REQUEST:
        if (!initialized_ || !ready_) { error(ERR_NO_RADIO, r); break; }
        if (!standby_ && receiving()) { const uint8_t value = 1; reply(CMD_CAD_RESP, &value, 1, r); break; }
        owner_ = r; attempts_ = 0; startCad(false);
        break;
    case CMD_SET_CAD_PARAMS:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (len != 4 || !p) { error(ERR_INVALID_CONFIG, r); break; }
        cadSymbols_ = p[0]; cadPeak_ = p[1]; cadMin_ = p[2]; cadExit_ = p[3]; cadCustom_ = true;
        reply(CMD_CAD_PARAMS_RESP, p, 4, r);
        break;
    case CMD_SET_AUTO_CAD: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (!len || !p) { error(ERR_INVALID_CMD, r); break; }
        autoCad_ = p[0] != 0;
        const uint8_t ok = 0; reply(CMD_SET_AUTO_CAD_RESP, &ok, 1, r);
        break;
    }
    case CMD_SET_CONFIG:
        txReady_ = false;
        hostConfigured_ = false;
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (len != sizeof(RadioConfig) || !p) { error(ERR_INVALID_CONFIG, r); break; }
        { RadioConfig cfg; memcpy(&cfg, p, sizeof(cfg));
          if (hooks_.diagnostic) {
              char line[224];
              snprintf(line, sizeof(line), "SET_CONFIG recv src=%u board=%s freq=%lu bw=%lu sf=%u cr=%u pwr_req=%d sync=0x%04X pre=%u",
                       (unsigned)r.transport, descriptor_.hardware->name,
                       (unsigned long)cfg.freq_hz, (unsigned long)cfg.bandwidth_hz,
                       (unsigned)cfg.sf, (unsigned)cfg.cr, (int)cfg.power_dbm,
                       (unsigned)cfg.syncword, (unsigned)cfg.preamble_len);
              hooks_.diagnostic(id_, line);
          }
          if (!applyConfig(cfg) || !startReceive()) error(ERR_INVALID_CONFIG, r);
          else {
              hostConfigured_ = true;
              txReady_ = !rxBoost_.fault;
              reply(CMD_CONFIG_RESP, (const uint8_t*)&config_, sizeof(config_), r);
          } }
        break;
    case CMD_GET_CONFIG:
        if (!initialized_) error(ERR_NO_RADIO, r);
        else reply(CMD_CONFIG_RESP, (const uint8_t*)&config_, sizeof(config_), r);
        break;
    case CMD_GET_FEM_STATE:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        submit(RfRequests::Kind::Get, RfRequests::Target::Fem, 0, 0, r);
        break;
    case CMD_SET_FEM_STATE:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (len != 2 || !p) submit(RfRequests::Kind::Error, RfRequests::Target::Fem, 0, ERR_INVALID_CONFIG, r);
        else submit(RfRequests::Kind::Set, RfRequests::Target::Fem, p[0], p[1], r);
        break;
    case CMD_GET_RX_BOOST:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        submit(RfRequests::Kind::Get, RfRequests::Target::RxBoost, 0, 0, r);
        break;
    case CMD_SET_RX_BOOST:
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (len != 1 || !p || p[0] > 1) submit(RfRequests::Kind::Error, RfRequests::Target::RxBoost, 0, ERR_INVALID_CONFIG, r);
        else submit(RfRequests::Kind::Set, RfRequests::Target::RxBoost, 0, p[0], r);
        break;
    case CMD_RADIO_STANDBY: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        frontEnd_->prepareStandby();
        const bool ok = chip().standby() == RADIOLIB_ERR_NONE;
        if (ok) standby_ = true; else fault();
        const uint8_t result = ok ? 0 : 1;
        reply(CMD_RADIO_STANDBY_RESP, &result, 1, r);
        break;
    }
    case CMD_RADIO_RESUME: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        standby_ = false;
        const bool ok = applyConfig(config_) && startReceive();
        const uint8_t result = ok ? 0 : 1;
        reply(CMD_RADIO_RESUME_RESP, &result, 1, r);
        break;
    }
    case CMD_RX_START:
        if (!initialized_) error(ERR_NO_RADIO, r);
        else if (!startReceive()) error(ERR_RADIO_FAULT, r);
        else reply(CMD_RX_STARTED, nullptr, 0, r);
        break;
    case CMD_GET_AGC_INTERVAL: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        const uint16_t sec = frontEnd_->getAgcResetIntervalSec();
        reply(CMD_AGC_INTERVAL_RESP, (const uint8_t*)&sec, sizeof(sec), r); break;
    }
    case CMD_SET_AGC_INTERVAL: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        if (len != 2 || !p) { error(ERR_INVALID_CONFIG, r); break; }
        const uint16_t sec = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        if (sec > RFFrontEnd::MAX_AGC_RESET_INTERVAL_SEC) { error(ERR_INVALID_CONFIG, r); break; }
        if (!frontEnd_->setAgcResetIntervalSec(sec, false)) { error(ERR_UNSUPPORTED, r); break; }
        agcSchedule_.recordAttempt(millis());
        reply(CMD_AGC_INTERVAL_RESP, (const uint8_t*)&sec, sizeof(sec), r); break;
    }
    case CMD_GET_RF_CAPS: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        uint32_t bits = RF_CAP_AGC | RF_CAP_RX_BOOSTED_GAIN;
        const auto fem = frontEnd_->getFemState();
        if (fem.capability & FEM_STATE_RX_LNA) bits |= RF_CAP_FEM_RX_LNA;
        if (fem.capability & FEM_STATE_TX_PA) bits |= RF_CAP_FEM_TX_PA;
        reply(CMD_RF_CAPS_RESP, (const uint8_t*)&bits, sizeof(bits), r); break;
    }
    case CMD_NOISE_REQ: {
        if (!initialized_) { error(ERR_NO_RADIO, r); break; }
        const int16_t noise = (int16_t)(noiseFloor_ * 10.0f);
        reply(CMD_NOISE_RESP, (const uint8_t*)&noise, sizeof(noise), r); break;
    }
    case CMD_STATUS_REQ: {
        const StatusResp live = status();
        reply(CMD_STATUS_RESP, (const uint8_t*)&live, sizeof(live), r);
        break;
    }
    default: return false;
    }
    return true;
}
bool RadioRuntime::receiving() {
    if (!initialized_ || !ready_ || standby_) return false;
    // Preamble/header do not assert RX's terminal DIO mask, so this guard must
    // still poll. Unknown is busy: do not authorize TX/FEM/AGC on a bad read or
    // erase an in-flight receive's activity/deadline bookkeeping.
    const auto sample = chip().readIrqChecked();
    if (!sample.valid) { irqPending_ = true; return true; }
    const uint32_t irq = sample.flags;
    if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) return true; // never discard terminal RX
    const bool header = (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID) != 0;
    const bool preamble = (irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED) != 0;
    const uint32_t now = millis();
    if (!header && rxHeader_) { rxActivity_ = rxHeader_ = false; return false; }
    if (header || preamble) {
        if (!rxActivity_ || (header && !rxHeader_)) { rxActivityMs_ = now; rxActivity_ = true; }
        rxHeader_ = header;
        const uint32_t bound = chip().getTimeOnAir(header ? MAX_LORA_PAYLOAD : 1) / 1000U * 3U / 2U + 100U;
        if ((uint32_t)(now - rxActivityMs_) <= bound) return true;
        if (chip().clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED |
                                RADIOLIB_SX126X_IRQ_HEADER_VALID) != RADIOLIB_ERR_NONE) fault();
    }
    rxActivity_ = rxHeader_ = false;
    return false;
}
void RadioRuntime::txLed(bool on) {
    const auto& h = *descriptor_.hardware;
    if (h.pin_lora_tx_led < 0) return;
    pinMode(h.pin_lora_tx_led, OUTPUT);
    digitalWrite(h.pin_lora_tx_led, on == h.lora_tx_led_active_high ? HIGH : LOW);
}
bool RadioRuntime::startCad(bool automatic) {
    if (!ready_) { operation_ = Operation::Idle; error(ERR_RADIO_FAULT, owner_); return false; }
    if (!current(owner_)) { operation_ = Operation::Idle; return false; }
    operation_ = automatic ? Operation::AutoCad : Operation::Cad;
    if (chip().standby() != RADIOLIB_ERR_NONE || chip().clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL) != RADIOLIB_ERR_NONE) {
        fault(); finishOperation(ERR_RADIO_FAULT); return false;
    }
    frontEnd_->prepareReceive();
    irqPending_ = false;
    ChannelScanConfig_t cfg = {};
    cfg.cad.symNum = cadSymbols_; cfg.cad.detPeak = cadPeak_;
    cfg.cad.detMin = cadMin_; cfg.cad.exitMode = cadExit_;
    cfg.cad.timeout = 0;
    cfg.cad.irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS;
    cfg.cad.irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK;
    ++attempts_;
    operationStartedMs_ = millis();
    const int16_t state = (automatic || cadCustom_) ? chip().startChannelScan(cfg) : chip().startChannelScan();
    if (state != RADIOLIB_ERR_NONE) { finishOperation(automatic ? ERR_CHANNEL_BUSY : ERR_CAD_FAILED); return false; }
    return true;
}
void RadioRuntime::startTx() {
    if (!ready_ || !txReady_) { operation_ = Operation::Idle; error(ERR_RADIO_FAULT, owner_); return; }
    if (!current(owner_)) { operation_ = Operation::Idle; startReceive(); return; }
    operation_ = Operation::Tx;
    if (chip().standby() != RADIOLIB_ERR_NONE || chip().clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL) != RADIOLIB_ERR_NONE) {
        fault(); finishOperation(ERR_RADIO_FAULT); return;
    }
    frontEnd_->prepareTransmit();
    irqPending_ = false;
    txLed(true);
    operationStartedMs_ = millis();
    if (chip().startTransmit(txPayload_,txLength_) != RADIOLIB_ERR_NONE) {
        const bool finished = chip().finishTransmit() == RADIOLIB_ERR_NONE;
        if (!finished) fault();
        finishOperation(finished ? ERR_TX_TIMEOUT : ERR_RADIO_FAULT);
    }
}
void RadioRuntime::submit(RfRequests::Kind kind, RfRequests::Target target,
                           uint8_t apply, uint8_t value, const ResponseRoute& r) {
    requests_.discardIf([this](const RfRequests::Request<ResponseRoute>& req) { return !current(req.src); });
    if (!requests_.push({kind, target, apply, value, r})) { error(ERR_RADIO_BUSY, r); return; }
    drainRequests();
}
void RadioRuntime::drainRequests() {
    requests_.discardIf([this](const RfRequests::Request<ResponseRoute>& req) { return !current(req.src); });
    if (!initialized_ || busy()) return;
    requests_.drain([this]() { return !standby_ && receiving(); },
        [this](const RfRequests::Request<ResponseRoute>& req) { answer(req); },
        [this](const RfRequests::Request<ResponseRoute>& req) {
            return current(req.src) && hooks_.canReply && hooks_.canReply(req.src);
        });
}
void RadioRuntime::answer(const RfRequests::Request<ResponseRoute>& req) {
    if (!current(req.src)) return; // revalidate immediately before mutation
    if (req.kind == RfRequests::Kind::Error) { error(req.value, req.src); return; }
    if (req.target == RfRequests::Target::Fem) {
        RFFrontEnd::FemState fem;
        if (req.kind == RfRequests::Kind::Set) {
            if (!frontEnd_->setFemState(req.apply, req.value, false, fem)) { error(ERR_UNSUPPORTED, req.src); return; }
        } else fem = frontEnd_->getFemState();
        uint8_t data[] = {fem.capability, fem.value};
        reply(CMD_FEM_STATE_RESP, data, sizeof(data), req.src);
    } else {
        const RxBoostControl::Reply result = req.kind == RfRequests::Kind::Set
            ? RxBoostControl::replyFor(rxBoost_, setBoost(req.value != 0, !standby_).outcome)
            : RxBoostControl::replyForGet(rxBoost_);
        if (result.error) error(result.error, req.src);
        else reply(CMD_RX_BOOST_RESP, &result.enabled, 1, req.src);
    }
}
void RadioRuntime::service() {
    if (replyPending_) {
        if (!current(replyRoute_)) replyPending_ = false;
        else if (hooks_.canReply && hooks_.canReply(replyRoute_)) {
            replyPending_ = false;
            hooks_.reply(replyRoute_, replyCmd_, replyPayload_, replyLength_);
        }
    }
    drainRequests();
}
void RadioRuntime::maintenance() {
    if (!initialized_ || !ready_ || standby_ || busy()) return;
    const uint32_t now = millis();
    if (!irqPending_ && (uint32_t)(now - lastPacketMs_) >= 500U &&
        (uint32_t)(now - lastNoiseMs_) >= 10U && !receiving()) {
        lastNoiseMs_ = now;
        const float value = chip().getRSSI(false);
        if (value >= -150.0f && value <= -30.0f) {
            noiseSum_ += value;
            if (++noiseSamples_ == 20) {
                noiseFloor_ = noiseSum_ / 20.0f;
                if (noiseFloor_ > -50.0f) noiseFloor_ = -50.0f;
                if (noiseFloor_ < -150.0f) noiseFloor_ = -150.0f;
                noiseSum_ = 0; noiseSamples_ = 0;
            }
        }
    }
    AgcMaintenance::Conditions conditions;
    conditions.radioReady = ready_;
    conditions.intentionalStandby = standby_;
    conditions.txActive = busy();
    conditions.dio1Pending = irqPending_;
    conditions.intervalSec = frontEnd_->getAgcResetIntervalSec();
    conditions.nowMs = now;
    conditions.lastPacketMs = lastPacketMs_;
#if defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)
    conditions.lastTxCompleteMs = lastTxMs_;
    conditions.postTxQuietMs = AgcMaintenance::STATION_POST_TX_QUIET_MS;
#endif
    if (!AgcMaintenance::shouldAttempt(agcSchedule_, conditions,
                                      [this]() { return receiving() || irqPending_; })) return;
#if defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)
    const auto result = AgcMaintenance::run(RADIOLIB_ERR_NONE,
        [this]() { frontEnd_->prepareStandby(); },
        [this]() { return chip().standby(); },
        [this]() { return chip().resetAGC(); },
        [this]() { return startReceive(); });
    RxBoostControl::noteAgcReset(rxBoost_, result.resetAttempted,
                                result.resetState == RADIOLIB_ERR_NONE, result.rxRestarted);
    if (!result.succeeded(RADIOLIB_ERR_NONE)) fault();
    else { ++agcResetCount_; lastAgcResetMs_ = millis(); }
#else
    const auto result = setBoost(rxBoost_.enabled, true);
    if (result.outcome == RxBoostControl::Outcome::Applied) { ++agcResetCount_; lastAgcResetMs_ = millis(); }
#endif
    agcSchedule_.recordAttempt(millis());
    noiseSum_ = 0; noiseSamples_ = 0;
}
void RadioRuntime::noteFrameError(uint8_t code) { if (code == ERR_CRC_MISMATCH) ++counters_.crc_errors; }
void RadioRuntime::restoreState(bool standby, bool autoCad) {
    if (!hardware_) { standby_ = standby; autoCad_ = autoCad; }
}
bool RadioRuntime::finishOperation(uint8_t errorCode) {
    const Operation finished = operation_;
    operation_ = Operation::Idle;
    txLed(false);
    irqPending_ = false;
    if (chip().clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL) != RADIOLIB_ERR_NONE) { fault(); errorCode = ERR_RADIO_FAULT; }
    lastPacketMs_ = millis();
    if (finished == Operation::Tx) lastTxMs_ = lastPacketMs_;
    if (ready_ && !startReceive()) errorCode = ERR_RADIO_FAULT;
    if (errorCode) error(errorCode, owner_);
    else if (finished == Operation::Tx) {
        const uint32_t airtime = chip().getTimeOnAir(txLength_);
        reply(CMD_TX_DONE, (const uint8_t*)&airtime, sizeof(airtime), owner_);
    }
    return errorCode == 0;
}
