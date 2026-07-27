#pragma once

#include <stddef.h>
#include <stdint.h>

namespace OtaStagedWriter {

static constexpr uint32_t STAGING_START = 0x88000u;
static constexpr uint32_t STAGING_END = 0xEA000u;
static constexpr uint32_t PAGE_SIZE = 4096u;
static constexpr uint32_t PAYLOAD_MAX = STAGING_END - STAGING_START;
static constexpr size_t HEADER_MAX = 160u;
// Match the nRF52840 erase page. Adafruit's flash_nrf5x cache flush rewrites a
// complete page, so smaller chunks would multiply erase/program wear.
static constexpr size_t WRITE_CHUNK = PAGE_SIZE;

class Flash {
public:
    virtual ~Flash() = default;
    virtual bool erasePage(uint32_t address) = 0;
    virtual bool write(uint32_t address, const uint8_t* data, size_t length) = 0;
    virtual bool read(uint32_t address, uint8_t* data, size_t length) = 0;
    virtual bool writeErasesPage() const { return false; }
};

class ServiceHooks {
public:
    virtual ~ServiceHooks() = default;
    virtual void betweenFlashUnits() = 0;
};

enum class Status : uint8_t {
    RECEIVING_HEADER,
    ERASING,
    RECEIVING_PAYLOAD,
    READING_BACK,
    VERIFIED,
    EMPTY_INPUT,
    TRUNCATED,
    TRAILING_DATA,
    BAD_MAGIC,
    BAD_SCHEMA,
    BAD_HEADER_LENGTH,
    BAD_TARGET_LENGTH,
    BAD_VERSION_LENGTH,
    BAD_SIGNATURE,
    WRONG_ORIGIN,
    WRONG_FWID,
    EMPTY_PAYLOAD,
    PAYLOAD_TOO_LARGE,
    WRONG_TARGET,
    BAD_VERSION,
    HASH_MISMATCH,
    ERASE_FAILED,
    WRITE_FAILED,
    READ_FAILED,
    READBACK_MISMATCH,
    CANCELED,
};

class Writer {
public:
    Writer(Flash& flash, ServiceHooks& hooks);

    // Accepts as many bytes as bounded internal storage/state allows. The caller
    // must retain and re-offer any unconsumed suffix after service().
    size_t push(const uint8_t* data, size_t length);
    void finish();
    void cancel();
    bool service(); // performs at most one erase, write, or read flash unit

    Status status() const { return status_; }
    bool isTerminal() const;
    bool verified() const { return status_ == Status::VERIFIED; }
    uint32_t receivedPayload() const { return payloadReceived_; }
    uint32_t declaredPayload() const { return payloadLength_; }

private:
    class Sha256 {
    public:
        Sha256();
        void reset();
        void update(const uint8_t* data, size_t length);
        void finish(uint8_t digest[32]);
    private:
        void transform();
        uint32_t state_[8];
        uint8_t block_[64];
        size_t used_;
        uint64_t bits_;
    };

    bool parseFixedHeader();
    bool validateVariableHeader();
    void fail(Status status);
    bool writeBuffered();
    static bool equalFixedTime(const uint8_t* a, const uint8_t* b, size_t length);

    Flash& flash_;
    ServiceHooks& hooks_;
    Status status_;
    uint8_t header_[HEADER_MAX];
    size_t headerUsed_;
    uint16_t headerLength_;
    uint16_t targetLength_;
    uint16_t versionLength_;
    uint32_t payloadLength_;
    uint8_t expectedDigest_[32];
    uint32_t eraseAddress_;
    uint32_t eraseEnd_;
    uint32_t payloadReceived_;
    uint32_t payloadWritten_;
    uint8_t writeBuffer_[WRITE_CHUNK];
    size_t writeUsed_;
    bool inputFinished_;
    Sha256 streamHash_;
    Sha256 readbackHash_;
    uint32_t readbackOffset_;
    uint8_t readBuffer_[WRITE_CHUNK];
};

#if defined(BOARD_RAK4631_WISMESH_ETH)
class Nrf52Flash final : public Flash {
public:
    bool erasePage(uint32_t address) override;
    bool write(uint32_t address, const uint8_t* data, size_t length) override;
    bool read(uint32_t address, uint8_t* data, size_t length) override;
    bool writeErasesPage() const override { return true; }
};
class Nrf52ServiceHooks final : public ServiceHooks {
public:
    void betweenFlashUnits() override;
};
#endif

} // namespace OtaStagedWriter
