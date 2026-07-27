#pragma once

#include <cstddef>
#include <cstdint>

namespace HttpRequest {

constexpr size_t MAX_REQUEST_LINE_BYTES = 256;
constexpr size_t MAX_HEADER_BYTES = 2048;
constexpr size_t MAX_BODY_BYTES = 2048;
constexpr size_t MAX_ROUTE_BYTES = 256;
constexpr size_t MAX_QUERY_BYTES = 256;
constexpr size_t MAX_HEADER_COUNT = 24;
constexpr size_t MAX_FORM_FIELDS = 16;
constexpr size_t MAX_FORM_KEY_BYTES = 64;
constexpr size_t MAX_FORM_VALUE_BYTES = 256;

enum class Method : uint8_t { NONE = 0, GET, POST };
enum class Result : uint8_t {
    NEED_MORE = 0,
    COMPLETE,
    BAD_REQUEST,
    METHOD_NOT_ALLOWED,
    PAYLOAD_TOO_LARGE,
    TIMED_OUT,
};

struct HeaderView {
    const char* name = nullptr;
    const char* value = nullptr;
};

struct Request {
    Method method = Method::NONE;
    char route[MAX_ROUTE_BYTES + 1] = {};
    char query[MAX_QUERY_BYTES + 1] = {};
    char body[MAX_BODY_BYTES + 1] = {};
    size_t bodyLength = 0;
    size_t contentLength = 0;
    bool connectionClose = true;
};

class Parser {
public:
    Parser();
    void reset(uint32_t nowMs, uint32_t timeoutMs);
    Result feed(const uint8_t* data, size_t length, uint32_t nowMs);
    Result poll(uint32_t nowMs);
    Result finish();
    Result result() const { return result_; }
    const Request& request() const { return request_; }
    const char* header(const char* name) const;
    bool routeEquals(const char* exactRoute) const;

private:
    enum class State : uint8_t { REQUEST_LINE, HEADERS, BODY, DONE, FAILED };
    Result feedByte(uint8_t byte);
    Result completeLine();
    Result parseRequestLine();
    Result finalizeHeaders();
    Result fail(Result result);

    State state_ = State::REQUEST_LINE;
    Result result_ = Result::NEED_MORE;
    Request request_{};
    uint32_t lastActivityMs_ = 0;
    uint32_t timeoutMs_ = 0;
    char line_[MAX_HEADER_BYTES + 1] = {};
    size_t lineLength_ = 0;
    bool sawCr_ = false;
    char headerStorage_[MAX_HEADER_BYTES + 1] = {};
    size_t headerStorageLength_ = 0;
    size_t headerWireBytes_ = 0;
    HeaderView headers_[MAX_HEADER_COUNT] = {};
    size_t headerCount_ = 0;
    bool contentLengthSeen_ = false;
};

struct FormField {
    char key[MAX_FORM_KEY_BYTES + 1] = {};
    char value[MAX_FORM_VALUE_BYTES + 1] = {};
};

struct FormData {
    FormField fields[MAX_FORM_FIELDS] = {};
    size_t count = 0;
    const char* get(const char* key) const;
};

bool urlDecode(const char* encoded, char* output, size_t capacity, bool plusAsSpace);
bool parseForm(const char* encoded, FormData& output);
bool base64Encode(const uint8_t* input, size_t length, char* output, size_t capacity);
bool basicAuthMatches(const char* authorization, const char* expectedUser,
                      const char* expectedPassword);

}  // namespace HttpRequest
