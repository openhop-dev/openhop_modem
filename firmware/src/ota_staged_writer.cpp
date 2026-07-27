#include "ota_staged_writer.h"

#include "ota_package.h"

#include <string.h>

#if defined(BOARD_RAK4631_WISMESH_ETH)
#include "compat.h"
#include <Arduino.h>
#include <flash/flash_nrf5x.h>
#include <nrf_sdm.h>
#endif

namespace OtaStagedWriter {
namespace {
uint16_t read16(const uint8_t* p, size_t offset) {
    return static_cast<uint16_t>(p[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[offset + 1]) << 8);
}
uint32_t read32(const uint8_t* p, size_t offset) {
    return static_cast<uint32_t>(p[offset]) |
           (static_cast<uint32_t>(p[offset + 1]) << 8) |
           (static_cast<uint32_t>(p[offset + 2]) << 16) |
           (static_cast<uint32_t>(p[offset + 3]) << 24);
}
bool validUtf8NoNul(const uint8_t* p, size_t length) {
    size_t i = 0;
    while (i < length) {
        const uint8_t c = p[i++];
        if (c == 0) return false;
        if (c < 0x80) continue;
        size_t continuation = 0;
        uint32_t value = 0;
        if ((c & 0xe0) == 0xc0) { continuation = 1; value = c & 0x1f; if (value < 2) return false; }
        else if ((c & 0xf0) == 0xe0) { continuation = 2; value = c & 0x0f; }
        else if ((c & 0xf8) == 0xf0) { continuation = 3; value = c & 0x07; }
        else return false;
        if (continuation > length - i) return false;
        for (size_t j = 0; j < continuation; ++j) {
            const uint8_t next = p[i++];
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if ((continuation == 2 && value < 0x800) ||
            (continuation == 3 && value < 0x10000) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return false;
    }
    return true;
}
uint32_t rotateRight(uint32_t value, uint32_t amount) {
    return (value >> amount) | (value << (32u - amount));
}
#if defined(BOARD_RAK4631_WISMESH_ETH)
bool softDeviceEnabled() {
    uint8_t enabled = 0;
    const uint32_t result = sd_softdevice_is_enabled(&enabled);
    return result != NRF_SUCCESS || enabled != 0;
}
#endif
} // namespace

Writer::Sha256::Sha256() { reset(); }
void Writer::Sha256::reset() {
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(state_, initial, sizeof(state_));
    memset(block_, 0, sizeof(block_));
    used_ = 0;
    bits_ = 0;
}
void Writer::Sha256::update(const uint8_t* data, size_t length) {
    bits_ += static_cast<uint64_t>(length) * 8u;
    while (length != 0) {
        size_t take = sizeof(block_) - used_;
        if (take > length) take = length;
        memcpy(block_ + used_, data, take);
        used_ += take;
        data += take;
        length -= take;
        if (used_ == sizeof(block_)) { transform(); used_ = 0; }
    }
}
void Writer::Sha256::finish(uint8_t digest[32]) {
    block_[used_++] = 0x80;
    if (used_ > 56) {
        memset(block_ + used_, 0, sizeof(block_) - used_);
        transform();
        used_ = 0;
    }
    memset(block_ + used_, 0, 56 - used_);
    used_ = 56;
    for (int shift = 56; shift >= 0; shift -= 8) block_[used_++] = static_cast<uint8_t>(bits_ >> shift);
    transform();
    for (size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>(state_[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(state_[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(state_[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(state_[i]);
    }
}
void Writer::Sha256::transform() {
    static const uint32_t constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) words[i] = (static_cast<uint32_t>(block_[i*4])<<24) | (static_cast<uint32_t>(block_[i*4+1])<<16) | (static_cast<uint32_t>(block_[i*4+2])<<8) | block_[i*4+3];
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotateRight(words[i-15],7)^rotateRight(words[i-15],18)^(words[i-15]>>3);
        const uint32_t s1 = rotateRight(words[i-2],17)^rotateRight(words[i-2],19)^(words[i-2]>>10);
        words[i] = words[i-16] + s0 + words[i-7] + s1;
    }
    uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for(size_t i=0;i<64;++i){const uint32_t s1=rotateRight(e,6)^rotateRight(e,11)^rotateRight(e,25),choice=(e&f)^((~e)&g),t1=h+s1+choice+constants[i]+words[i],s0=rotateRight(a,2)^rotateRight(a,13)^rotateRight(a,22),majority=(a&b)^(a&c)^(b&c),t2=s0+majority;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
}

Writer::Writer(Flash& flash, ServiceHooks& hooks)
    : flash_(flash), hooks_(hooks), status_(Status::RECEIVING_HEADER), header_{}, headerUsed_(0),
      headerLength_(0), targetLength_(0), versionLength_(0), payloadLength_(0), expectedDigest_{},
      eraseAddress_(STAGING_START), eraseEnd_(STAGING_START), payloadReceived_(0), payloadWritten_(0),
      writeBuffer_{}, writeUsed_(0), inputFinished_(false), streamHash_(), readbackHash_(),
      readbackOffset_(0), readBuffer_{} {}

bool Writer::isTerminal() const {
    return status_ == Status::VERIFIED || static_cast<uint8_t>(status_) >= static_cast<uint8_t>(Status::EMPTY_INPUT);
}
void Writer::fail(Status status) {
    status_ = status;
    inputFinished_ = true;
    writeUsed_ = 0;
    memset(expectedDigest_, 0, sizeof(expectedDigest_));
}
bool Writer::equalFixedTime(const uint8_t* a, const uint8_t* b, size_t length) {
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference = static_cast<uint8_t>(difference | (a[i] ^ b[i]));
    return difference == 0;
}
bool Writer::parseFixedHeader() {
    using namespace OtaPackage;
    if (!equalFixedTime(header_ + OTA_PACKAGE_MAGIC_OFFSET, MAGIC, sizeof(MAGIC))) { fail(Status::BAD_MAGIC); return false; }
    if (read16(header_, OTA_PACKAGE_SCHEMA_OFFSET) != OTA_PACKAGE_SCHEMA) { fail(Status::BAD_SCHEMA); return false; }
    headerLength_ = read16(header_, OTA_PACKAGE_HEADER_LENGTH_OFFSET);
    targetLength_ = read16(header_, OTA_PACKAGE_TARGET_LENGTH_OFFSET);
    versionLength_ = read16(header_, OTA_PACKAGE_VERSION_LENGTH_OFFSET);
    const uint16_t signatureType = read16(header_, OTA_PACKAGE_SIGNATURE_TYPE_OFFSET);
    const uint16_t signatureLength = read16(header_, OTA_PACKAGE_SIGNATURE_LENGTH_OFFSET);
    if (targetLength_ == 0 || targetLength_ > OTA_PACKAGE_TARGET_MAX_LENGTH) { fail(Status::BAD_TARGET_LENGTH); return false; }
    if (versionLength_ == 0 || versionLength_ > OTA_PACKAGE_VERSION_MAX_LENGTH) { fail(Status::BAD_VERSION_LENGTH); return false; }
    if (signatureType != OTA_PACKAGE_SIGNATURE_NONE || signatureLength != 0) { fail(Status::BAD_SIGNATURE); return false; }
    const uint32_t calculated = static_cast<uint32_t>(OTA_PACKAGE_FIXED_HEADER_LENGTH) + targetLength_ + versionLength_ + signatureLength;
    if (headerLength_ < OTA_PACKAGE_FIXED_HEADER_LENGTH || headerLength_ > HEADER_MAX || headerLength_ != calculated) { fail(Status::BAD_HEADER_LENGTH); return false; }
    if (read32(header_, OTA_PACKAGE_APPLICATION_ORIGIN_OFFSET) != OTA_PACKAGE_APPLICATION_ORIGIN) { fail(Status::WRONG_ORIGIN); return false; }
    if (read32(header_, OTA_PACKAGE_SOFTDEVICE_FWID_OFFSET) != OTA_PACKAGE_SOFTDEVICE_FWID) { fail(Status::WRONG_FWID); return false; }
    payloadLength_ = read32(header_, OTA_PACKAGE_PAYLOAD_LENGTH_OFFSET);
    if (payloadLength_ == 0) { fail(Status::EMPTY_PAYLOAD); return false; }
    if (payloadLength_ > PAYLOAD_MAX || payloadLength_ > OTA_PACKAGE_PAYLOAD_MAX_LENGTH) { fail(Status::PAYLOAD_TOO_LARGE); return false; }
    memcpy(expectedDigest_, header_ + OTA_PACKAGE_PAYLOAD_SHA256_OFFSET, sizeof(expectedDigest_));
    return true;
}
bool Writer::validateVariableHeader() {
    using namespace OtaPackage;
    const size_t targetOffset = OTA_PACKAGE_FIXED_HEADER_LENGTH;
    const size_t versionOffset = targetOffset + targetLength_;
    const size_t expectedTargetLength = sizeof(TARGET_ID) - 1;
    if (targetLength_ != expectedTargetLength || !equalFixedTime(header_ + targetOffset, reinterpret_cast<const uint8_t*>(TARGET_ID), expectedTargetLength)) { fail(Status::WRONG_TARGET); return false; }
    if (!validUtf8NoNul(header_ + versionOffset, versionLength_)) { fail(Status::BAD_VERSION); return false; }
    eraseAddress_ = STAGING_START;
    const uint32_t pages = (payloadLength_ + PAGE_SIZE - 1u) / PAGE_SIZE;
    eraseEnd_ = STAGING_START + pages * PAGE_SIZE;
    if (eraseEnd_ < STAGING_START || eraseEnd_ > STAGING_END) { fail(Status::PAYLOAD_TOO_LARGE); return false; }
    // Adafruit's nRF flash cache performs erase+program as one page flush.
    // Other backends retain the explicit erase phase.
    status_ = flash_.writeErasesPage() ? Status::RECEIVING_PAYLOAD : Status::ERASING;
    return true;
}

size_t Writer::push(const uint8_t* data, size_t length) {
    if (isTerminal() || inputFinished_ || data == nullptr || length == 0) return 0;
    if (status_ == Status::RECEIVING_HEADER) {
        size_t needed = headerLength_ == 0 ? OtaPackage::OTA_PACKAGE_FIXED_HEADER_LENGTH - headerUsed_ : headerLength_ - headerUsed_;
        size_t take = length < needed ? length : needed;
        memcpy(header_ + headerUsed_, data, take);
        headerUsed_ += take;
        if (headerUsed_ == OtaPackage::OTA_PACKAGE_FIXED_HEADER_LENGTH && headerLength_ == 0 && !parseFixedHeader()) return take;
        if (!isTerminal() && headerLength_ != 0 && headerUsed_ == headerLength_) validateVariableHeader();
        return take;
    }
    if (status_ != Status::RECEIVING_PAYLOAD) return 0;
    if (payloadReceived_ == payloadLength_) { fail(Status::TRAILING_DATA); return 0; }
    const size_t capacity = WRITE_CHUNK - writeUsed_;
    const uint32_t remainingPayload = payloadLength_ - payloadReceived_;
    size_t take = length < capacity ? length : capacity;
    if (take > remainingPayload) take = remainingPayload;
    memcpy(writeBuffer_ + writeUsed_, data, take);
    streamHash_.update(data, take);
    writeUsed_ += take;
    payloadReceived_ += static_cast<uint32_t>(take);
    if (take < length && payloadReceived_ == payloadLength_) fail(Status::TRAILING_DATA);
    return take;
}

void Writer::finish() {
    if (isTerminal()) return;
    inputFinished_ = true;
    if (headerUsed_ == 0) { fail(Status::EMPTY_INPUT); return; }
    if (status_ == Status::RECEIVING_HEADER || payloadReceived_ != payloadLength_) { fail(Status::TRUNCATED); }
}
void Writer::cancel() { if (!isTerminal()) fail(Status::CANCELED); }

bool Writer::writeBuffered() {
    if (writeUsed_ == 0) return false;
    const size_t rawLength = writeUsed_;
    const size_t alignedLength = WRITE_CHUNK;
    memset(writeBuffer_ + rawLength, 0xff, alignedLength - rawLength);
    const uint32_t address = STAGING_START + payloadWritten_;
    if (address < STAGING_START || address > STAGING_END ||
        alignedLength > STAGING_END - address ||
        !flash_.write(address, writeBuffer_, alignedLength)) {
        fail(Status::WRITE_FAILED); hooks_.betweenFlashUnits(); return true;
    }
    hooks_.betweenFlashUnits();
    payloadWritten_ += static_cast<uint32_t>(rawLength);
    writeUsed_ = 0;
    return true;
}

bool Writer::service() {
    if (isTerminal()) return false;
    if (status_ == Status::ERASING) {
        if (eraseAddress_ < eraseEnd_) {
            const uint32_t address = eraseAddress_;
            if (address < STAGING_START || address > STAGING_END - PAGE_SIZE || !flash_.erasePage(address)) { fail(Status::ERASE_FAILED); hooks_.betweenFlashUnits(); return true; }
            hooks_.betweenFlashUnits();
            eraseAddress_ += PAGE_SIZE;
            return true;
        }
        status_ = Status::RECEIVING_PAYLOAD;
        return false;
    }
    if (status_ == Status::RECEIVING_PAYLOAD) {
        if (writeUsed_ == WRITE_CHUNK || (inputFinished_ && writeUsed_ != 0)) return writeBuffered();
        if (inputFinished_ && payloadWritten_ == payloadLength_) {
            uint8_t digest[32]; streamHash_.finish(digest);
            if (!equalFixedTime(digest, expectedDigest_, sizeof(digest))) { fail(Status::HASH_MISMATCH); return false; }
            readbackHash_.reset(); readbackOffset_ = 0; status_ = Status::READING_BACK;
        }
        return false;
    }
    if (status_ == Status::READING_BACK) {
        if (readbackOffset_ < payloadLength_) {
            size_t length = payloadLength_ - readbackOffset_;
            if (length > sizeof(readBuffer_)) length = sizeof(readBuffer_);
            const uint32_t address = STAGING_START + readbackOffset_;
            if (address < STAGING_START || address > STAGING_END ||
                length > STAGING_END - address ||
                !flash_.read(address, readBuffer_, length)) {
                fail(Status::READ_FAILED); hooks_.betweenFlashUnits(); return true;
            }
            hooks_.betweenFlashUnits();
            readbackHash_.update(readBuffer_, length);
            readbackOffset_ += static_cast<uint32_t>(length);
            return true;
        }
        uint8_t digest[32]; readbackHash_.finish(digest);
        if (!equalFixedTime(digest, expectedDigest_, sizeof(digest))) fail(Status::READBACK_MISMATCH);
        else status_ = Status::VERIFIED;
    }
    return false;
}

#if defined(BOARD_RAK4631_WISMESH_ETH)
bool Nrf52Flash::erasePage(uint32_t address) {
    if (address < STAGING_START || address > STAGING_END - PAGE_SIZE || (address % PAGE_SIZE) != 0) return false;
    // The pinned framework waits forever for SoftDevice flash events. This
    // application does not enable Bluefruit/SoftDevice during Ethernet mode;
    // fail closed instead of entering that unbounded path if this changes.
    if (softDeviceEnabled()) return false;
    flash_nrf5x_flush();
    return flash_nrf5x_erase(address);
}
bool Nrf52Flash::write(uint32_t address, const uint8_t* data, size_t length) {
    if (address < STAGING_START || address > STAGING_END || length == 0 ||
        (address & 3u) != 0 || (length & 3u) != 0 ||
        length != PAGE_SIZE || length > STAGING_END - address ||
        softDeviceEnabled()) return false;
    const int result = flash_nrf5x_write(address, data, static_cast<uint32_t>(length));
    flash_nrf5x_flush();
    if (result != static_cast<int>(length)) return false;
    return memcmp(reinterpret_cast<const void*>(address), data, PAGE_SIZE) == 0;
}
bool Nrf52Flash::read(uint32_t address, uint8_t* data, size_t length) {
    if (address < STAGING_START || address > STAGING_END || length == 0 ||
        length > STAGING_END - address) return false;
    flash_nrf5x_flush();
    return flash_nrf5x_read(data, address, static_cast<uint32_t>(length)) == static_cast<int>(length);
}
void Nrf52ServiceHooks::betweenFlashUnits() { compatWdtReset(); yield(); }
#endif

} // namespace OtaStagedWriter
