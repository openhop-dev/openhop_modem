#pragma once
#include "radio_hardware.h"
#include "response_route.h"
#include "rf_frontend.h"
#include "rf_request_queue.h"
#include "rx_boost_control.h"
#include "agc_maintenance.h"

class RadioRuntime {
public:
    struct Hooks {
        bool (*current)(const ResponseRoute&);
        bool (*canReply)(const ResponseRoute&);
        void (*reply)(const ResponseRoute&, uint8_t, const uint8_t*, uint16_t);
        void (*event)(uint8_t, uint8_t, const uint8_t*, uint16_t);
        void (*deviceStatus)(StatusResp&);
        void (*diagnostic)(uint8_t, const char*) = nullptr;
    };
    RadioRuntime(uint8_t id, const RadioDescriptor&, const Hooks&,
                 RFFrontEnd::Controller* frontEnd = nullptr, bool enabled = true);
    ~RadioRuntime();
    RadioRuntime(const RadioRuntime&) = delete;
    RadioRuntime& operator=(const RadioRuntime&) = delete;
    bool begin();
    // These are the production operations; tests fake only the hardware SDK.
    bool applyConfig(const RadioConfig&);
    bool startReceive();
    bool command(uint8_t, const uint8_t*, uint16_t, const ResponseRoute&);
    void serviceIrq();
    void service();
    void maintenance();
    void noteFrameError(uint8_t);
    void restoreState(bool standby, bool autoCad);
    uint8_t id() const { return id_; }
    bool present() const { return descriptor_.hardware->has_lora_radio; }
    bool enabled() const { return enabled_; }
    bool ready() const { return ready_; }
    bool txReady() const { return txReady_; }
    bool standby() const { return standby_; }
    bool busy() const { return operation_ != Operation::Idle || replyPending_; }
    bool autoCad() const { return autoCad_; }
    const RadioConfig& config() const { return config_; }
    StatusResp status() const;
    uint32_t irqCount() const { return irqCount_; }
    uint8_t queuedRequests() const { return requests_.size(); }
    uint32_t agcResetCount() const { return agcResetCount_; }
    uint32_t lastAgcResetMs() const { return lastAgcResetMs_; }
    RFFrontEnd::Controller& frontEnd() { return *frontEnd_; }
#if defined(ESP32)
    void IRAM_ATTR onIrq() { irqPending_ = true; irqCount_ = irqCount_ + 1; }
#else
    void onIrq() { irqPending_ = true; irqCount_ = irqCount_ + 1; }
#endif
private:
    enum class Operation : uint8_t { Idle, Cad, AutoCad, Backoff, Tx };
    const uint8_t id_;
    const bool enabled_;
    const RadioDescriptor& descriptor_;
    const Hooks hooks_;
    RFFrontEnd::Controller ownedFrontEnd_;
    RFFrontEnd::Controller* const frontEnd_;
    RadioHardware* hardware_ = nullptr;
    bool initialized_ = false, optionsReady_ = false, ready_ = false, txReady_ = false;
    bool standby_ = false, autoCad_ = false;
    bool configApplied_ = false, hostConfigured_ = false;
    RadioConfig config_{869618000, 62500, 8, 8, 22, 0x12, 16};
    StatusResp counters_{};
    volatile bool irqPending_ = false;
    volatile uint32_t irqCount_ = 0;
    RxBoostControl::State rxBoost_;
    float noiseFloor_ = -99, noiseSum_ = 0;
    uint8_t noiseSamples_ = 0;
    uint32_t lastPacketMs_ = 0, lastNoiseMs_ = 0;
    uint32_t rxActivityMs_ = 0;
    bool rxActivity_ = false, rxHeader_ = false;
    AgcMaintenance::Schedule agcSchedule_;
    uint32_t lastTxMs_ = 0, agcResetCount_ = 0, lastAgcResetMs_ = 0;
    bool cadCustom_ = false;
    uint8_t cadSymbols_ = 1, cadPeak_ = 22, cadMin_ = 10, cadExit_ = 0;
    Operation operation_ = Operation::Idle;
    ResponseRoute owner_{};
    uint32_t operationStartedMs_ = 0, backoffMs_ = 0;
    uint8_t attempts_ = 0;
    uint8_t txPayload_[MAX_LORA_PAYLOAD]{};
    uint16_t txLength_ = 0;
    RfRequests::Queue<ResponseRoute, 4> requests_;
    // One reserved control-completion slot per owner. The W5100S writer can
    // already be occupied by a query when an on-air TX/CAD completes.
    bool replyPending_ = false;
    ResponseRoute replyRoute_{};
    uint8_t replyCmd_ = 0;
    uint16_t replyLength_ = 0;
    uint8_t replyPayload_[MAX_LORA_PAYLOAD + 6]{};
    OpenHopSX1262& chip() { return hardware_->chip(); }
    bool current(const ResponseRoute&) const;
    void reply(uint8_t, const uint8_t*, uint16_t, const ResponseRoute&);
    void error(uint8_t, const ResponseRoute&);
    void fault();
    void txLed(bool);
    bool receiving();
    void receivePacket();
    RxBoostControl::Result setBoost(bool, bool);
    void submit(RfRequests::Kind, RfRequests::Target, uint8_t, uint8_t, const ResponseRoute&);
    void drainRequests();
    void answer(const RfRequests::Request<ResponseRoute>&);
    bool startCad(bool automatic);
    void startTx();
    bool finishOperation(uint8_t errorCode = 0);
};
