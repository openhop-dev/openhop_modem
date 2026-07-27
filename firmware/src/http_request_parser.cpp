#include "http_request_parser.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace HttpRequest {
namespace {

bool asciiEqualIgnoreCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

char* trim(char* text) {
    while (*text == ' ' || *text == '\t') ++text;
    char* end = text + std::strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return text;
}

bool parseSize(const char* text, size_t& value) {
    if (!text || !*text) return false;
    size_t out = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (!std::isdigit(*p)) return false;
        const size_t digit = *p - '0';
        if (out > (static_cast<size_t>(-1) - digit) / 10) return false;
        out = out * 10 + digit;
    }
    value = out;
    return true;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool normalizeTarget(const char* target, char* route, size_t routeCapacity,
                     char* query, size_t queryCapacity) {
    if (!target || target[0] != '/') return false;
    const char* question = std::strchr(target, '?');
    const size_t pathLength = question ? static_cast<size_t>(question - target) : std::strlen(target);
    if (pathLength > MAX_ROUTE_BYTES || (question && std::strlen(question + 1) > MAX_QUERY_BYTES)) return false;

    char decoded[MAX_ROUTE_BYTES + 1];
    char encodedPath[MAX_ROUTE_BYTES + 1];
    std::memcpy(encodedPath, target, pathLength);
    encodedPath[pathLength] = '\0';
    if (!urlDecode(encodedPath, decoded, sizeof(decoded), false)) return false;

    size_t out = 0;
    route[out++] = '/';
    const char* p = decoded + 1;
    while (*p) {
        while (*p == '/') ++p;
        if (!*p) break;
        const char* segment = p;
        while (*p && *p != '/') {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '\\' || c < 0x20 || c == 0x7f) return false;
            ++p;
        }
        const size_t segmentLength = static_cast<size_t>(p - segment);
        if ((segmentLength == 1 && segment[0] == '.') ||
            (segmentLength == 2 && segment[0] == '.' && segment[1] == '.')) return false;
        if (out > 1) {
            if (out + 1 >= routeCapacity) return false;
            route[out++] = '/';
        }
        if (out + segmentLength >= routeCapacity) return false;
        std::memcpy(route + out, segment, segmentLength);
        out += segmentLength;
    }
    route[out] = '\0';

    if (question) {
        if (std::strlen(question + 1) >= queryCapacity) return false;
        std::strcpy(query, question + 1);
    } else {
        query[0] = '\0';
    }
    return true;
}

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool base64Decode(const char* input, uint8_t* output, size_t capacity, size_t& written) {
    written = 0;
    if (!input) return false;
    const size_t length = std::strlen(input);
    if (length == 0 || length % 4 != 0) return false;
    for (size_t i = 0; i < length; i += 4) {
        const bool last = i + 4 == length;
        const int a = base64Value(static_cast<unsigned char>(input[i]));
        const int b = base64Value(static_cast<unsigned char>(input[i + 1]));
        const int c = input[i + 2] == '=' ? -2 : base64Value(static_cast<unsigned char>(input[i + 2]));
        const int d = input[i + 3] == '=' ? -2 : base64Value(static_cast<unsigned char>(input[i + 3]));
        if (a < 0 || b < 0 || c == -1 || d == -1) return false;
        if ((c == -2 && d != -2) || ((c == -2 || d == -2) && !last)) return false;
        const size_t bytes = c == -2 ? 1 : (d == -2 ? 2 : 3);
        if (written + bytes > capacity) return false;
        const uint32_t bits = (static_cast<uint32_t>(a) << 18) |
                              (static_cast<uint32_t>(b) << 12) |
                              (static_cast<uint32_t>(c < 0 ? 0 : c) << 6) |
                              static_cast<uint32_t>(d < 0 ? 0 : d);
        output[written++] = static_cast<uint8_t>(bits >> 16);
        if (bytes > 1) output[written++] = static_cast<uint8_t>(bits >> 8);
        if (bytes > 2) output[written++] = static_cast<uint8_t>(bits);
        if (c == -2 && (b & 0x0f) != 0) return false;
        if (d == -2 && c >= 0 && (c & 0x03) != 0) return false;
    }
    return true;
}

bool constantTimeEqual(const uint8_t* a, size_t aLength, const uint8_t* b, size_t bLength) {
    constexpr size_t comparisonBytes = 2 * 64 + 2;
    uint8_t diff = static_cast<uint8_t>(aLength ^ bLength);
    for (size_t i = 0; i < comparisonBytes; ++i) {
        const uint8_t av = i < aLength ? a[i] : 0;
        const uint8_t bv = i < bLength ? b[i] : 0;
        diff |= static_cast<uint8_t>(av ^ bv);
    }
    return diff == 0;
}

}  // namespace

Parser::Parser() { reset(0, 0); }

void Parser::reset(uint32_t nowMs, uint32_t timeoutMs) {
    state_ = State::REQUEST_LINE;
    result_ = Result::NEED_MORE;
    request_ = Request{};
    lastActivityMs_ = nowMs;
    timeoutMs_ = timeoutMs;
    lineLength_ = 0;
    sawCr_ = false;
    headerStorageLength_ = 0;
    headerWireBytes_ = 0;
    headerCount_ = 0;
    contentLengthSeen_ = false;
    line_[0] = '\0';
    headerStorage_[0] = '\0';
    for (auto& item : headers_) item = HeaderView{};
}

Result Parser::fail(Result result) {
    state_ = State::FAILED;
    result_ = result;
    return result_;
}

Result Parser::feed(const uint8_t* data, size_t length, uint32_t nowMs) {
    if (!data && length) return fail(Result::BAD_REQUEST);
    if (length) lastActivityMs_ = nowMs;
    for (size_t i = 0; i < length; ++i) {
        if (state_ == State::DONE) return fail(Result::BAD_REQUEST);
        if (state_ == State::FAILED) return result_;
        const Result step = feedByte(data[i]);
        if (step != Result::NEED_MORE && step != Result::COMPLETE) return step;
        if (step == Result::COMPLETE && i + 1 < length) return fail(Result::BAD_REQUEST);
    }
    return result_;
}

Result Parser::feedByte(uint8_t byte) {
    if (state_ == State::BODY) {
        if (request_.bodyLength >= request_.contentLength || request_.bodyLength >= MAX_BODY_BYTES) {
            return fail(Result::BAD_REQUEST);
        }
        request_.body[request_.bodyLength++] = static_cast<char>(byte);
        request_.body[request_.bodyLength] = '\0';
        if (request_.bodyLength == request_.contentLength) {
            state_ = State::DONE;
            return result_ = Result::COMPLETE;
        }
        return Result::NEED_MORE;
    }

    if (byte == '\n' && !sawCr_) return fail(Result::BAD_REQUEST);
    if (sawCr_) {
        if (byte != '\n') return fail(Result::BAD_REQUEST);
        sawCr_ = false;
        return completeLine();
    }
    if (byte == '\r') {
        sawCr_ = true;
        return Result::NEED_MORE;
    }
    const size_t limit = state_ == State::REQUEST_LINE ? MAX_REQUEST_LINE_BYTES : MAX_HEADER_BYTES;
    if (lineLength_ >= limit) {
        return fail(state_ == State::REQUEST_LINE ? Result::BAD_REQUEST : Result::PAYLOAD_TOO_LARGE);
    }
    line_[lineLength_++] = static_cast<char>(byte);
    line_[lineLength_] = '\0';
    return Result::NEED_MORE;
}

Result Parser::completeLine() {
    if (state_ == State::REQUEST_LINE) {
        Result parsed = parseRequestLine();
        lineLength_ = 0;
        line_[0] = '\0';
        if (parsed != Result::NEED_MORE) return parsed;
        state_ = State::HEADERS;
        return Result::NEED_MORE;
    }

    headerWireBytes_ += lineLength_ + 2;
    if (headerWireBytes_ > MAX_HEADER_BYTES) return fail(Result::PAYLOAD_TOO_LARGE);
    if (lineLength_ == 0) return finalizeHeaders();
    if (headerCount_ >= MAX_HEADER_COUNT || headerStorageLength_ + lineLength_ + 1 > MAX_HEADER_BYTES) {
        return fail(Result::PAYLOAD_TOO_LARGE);
    }
    char* destination = headerStorage_ + headerStorageLength_;
    std::memcpy(destination, line_, lineLength_ + 1);
    headerStorageLength_ += lineLength_ + 1;
    char* colon = std::strchr(destination, ':');
    if (!colon || colon == destination) return fail(Result::BAD_REQUEST);
    for (char* p = destination; p < colon; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (!(std::isalnum(c) || c == '-' || c == '_')) return fail(Result::BAD_REQUEST);
    }
    *colon = '\0';
    for (size_t i = 0; i < headerCount_; ++i) {
        // Singleton interpretation is the only policy used by this server.
        // Reject every duplicate rather than letting auth/content parsers and
        // intermediaries disagree over first-vs-last semantics.
        if (asciiEqualIgnoreCase(headers_[i].name, destination)) {
            return fail(Result::BAD_REQUEST);
        }
    }
    headers_[headerCount_++] = {destination, trim(colon + 1)};
    lineLength_ = 0;
    line_[0] = '\0';
    return Result::NEED_MORE;
}

Result Parser::parseRequestLine() {
    if (lineLength_ == 0) return fail(Result::BAD_REQUEST);
    char* firstSpace = std::strchr(line_, ' ');
    if (!firstSpace || firstSpace == line_) return fail(Result::BAD_REQUEST);
    char* secondSpace = std::strchr(firstSpace + 1, ' ');
    if (!secondSpace || secondSpace == firstSpace + 1 || std::strchr(secondSpace + 1, ' ')) {
        return fail(Result::BAD_REQUEST);
    }
    *firstSpace = '\0';
    *secondSpace = '\0';
    if (std::strcmp(line_, "GET") == 0) request_.method = Method::GET;
    else if (std::strcmp(line_, "POST") == 0) request_.method = Method::POST;
    else return fail(Result::METHOD_NOT_ALLOWED);
    if (std::strcmp(secondSpace + 1, "HTTP/1.0") != 0 &&
        std::strcmp(secondSpace + 1, "HTTP/1.1") != 0) return fail(Result::BAD_REQUEST);
    if (!normalizeTarget(firstSpace + 1, request_.route, sizeof(request_.route),
                         request_.query, sizeof(request_.query))) return fail(Result::BAD_REQUEST);
    return Result::NEED_MORE;
}

Result Parser::finalizeHeaders() {
    size_t declaredLength = 0;
    for (size_t i = 0; i < headerCount_; ++i) {
        if (asciiEqualIgnoreCase(headers_[i].name, "Transfer-Encoding")) return fail(Result::BAD_REQUEST);
        if (asciiEqualIgnoreCase(headers_[i].name, "Content-Length")) {
            size_t parsed = 0;
            if (!parseSize(headers_[i].value, parsed)) return fail(Result::BAD_REQUEST);
            // Reject duplicates even when values match. Accepting repeated
            // framing headers creates request-smuggling ambiguity.
            if (contentLengthSeen_) return fail(Result::BAD_REQUEST);
            contentLengthSeen_ = true;
            declaredLength = parsed;
        }
    }
    if (declaredLength > MAX_BODY_BYTES) return fail(Result::PAYLOAD_TOO_LARGE);
    if (request_.method == Method::POST && !contentLengthSeen_) return fail(Result::BAD_REQUEST);
    if (request_.method == Method::GET && declaredLength != 0) return fail(Result::BAD_REQUEST);
    request_.contentLength = declaredLength;
    request_.connectionClose = true;
    lineLength_ = 0;
    line_[0] = '\0';
    if (declaredLength == 0) {
        state_ = State::DONE;
        return result_ = Result::COMPLETE;
    }
    state_ = State::BODY;
    return Result::NEED_MORE;
}

Result Parser::poll(uint32_t nowMs) {
    if (state_ == State::DONE || state_ == State::FAILED) return result_;
    if (timeoutMs_ != 0 && static_cast<uint32_t>(nowMs - lastActivityMs_) >= timeoutMs_) {
        return fail(Result::TIMED_OUT);
    }
    return Result::NEED_MORE;
}

Result Parser::finish() {
    if (state_ == State::DONE || state_ == State::FAILED) return result_;
    return fail(Result::BAD_REQUEST);
}

const char* Parser::header(const char* name) const {
    for (size_t i = 0; i < headerCount_; ++i) {
        if (asciiEqualIgnoreCase(headers_[i].name, name)) return headers_[i].value;
    }
    return nullptr;
}

bool Parser::routeEquals(const char* exactRoute) const {
    return exactRoute && std::strcmp(request_.route, exactRoute) == 0;
}

const char* FormData::get(const char* key) const {
    if (!key) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(fields[i].key, key) == 0) return fields[i].value;
    }
    return nullptr;
}

bool urlDecode(const char* encoded, char* output, size_t capacity, bool plusAsSpace) {
    if (!encoded || !output || capacity == 0) return false;
    size_t written = 0;
    for (size_t i = 0; encoded[i]; ++i) {
        unsigned char value = static_cast<unsigned char>(encoded[i]);
        if (value == '%' ) {
            const int hi = hexValue(encoded[i + 1]);
            const int lo = encoded[i + 1] ? hexValue(encoded[i + 2]) : -1;
            if (hi < 0 || lo < 0) return false;
            value = static_cast<unsigned char>((hi << 4) | lo);
            i += 2;
        } else if (value == '+' && plusAsSpace) {
            value = ' ';
        }
        if (value == 0 || value < 0x20 || value == 0x7f || written + 1 >= capacity) return false;
        output[written++] = static_cast<char>(value);
    }
    output[written] = '\0';
    return true;
}

bool parseForm(const char* encoded, FormData& output) {
    output = FormData{};
    if (!encoded) return false;
    if (*encoded == '\0') return true;
    const char* cursor = encoded;
    while (*cursor) {
        if (output.count >= MAX_FORM_FIELDS) return false;
        const char* amp = std::strchr(cursor, '&');
        const size_t pairLength = amp ? static_cast<size_t>(amp - cursor) : std::strlen(cursor);
        const char* equals = static_cast<const char*>(std::memchr(cursor, '=', pairLength));
        if (!equals || equals == cursor) return false;
        char keyEncoded[MAX_FORM_KEY_BYTES * 3 + 1];
        char valueEncoded[MAX_FORM_VALUE_BYTES * 3 + 1];
        const size_t keyLength = static_cast<size_t>(equals - cursor);
        const size_t valueLength = pairLength - keyLength - 1;
        if (keyLength >= sizeof(keyEncoded) || valueLength >= sizeof(valueEncoded)) return false;
        std::memcpy(keyEncoded, cursor, keyLength); keyEncoded[keyLength] = '\0';
        std::memcpy(valueEncoded, equals + 1, valueLength); valueEncoded[valueLength] = '\0';
        FormField& field = output.fields[output.count++];
        if (!urlDecode(keyEncoded, field.key, sizeof(field.key), true) ||
            !urlDecode(valueEncoded, field.value, sizeof(field.value), true)) return false;
        cursor += pairLength;
        if (*cursor == '&') ++cursor;
        else break;
        if (!*cursor) return false;
    }
    return true;
}

bool base64Encode(const uint8_t* input, size_t length, char* output, size_t capacity) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if ((!input && length) || !output) return false;
    const size_t required = ((length + 2) / 3) * 4;
    if (capacity <= required) return false;
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
        const size_t remaining = length - i;
        const uint32_t bits = (static_cast<uint32_t>(input[i]) << 16) |
                              (remaining > 1 ? static_cast<uint32_t>(input[i + 1]) << 8 : 0) |
                              (remaining > 2 ? input[i + 2] : 0);
        output[out++] = alphabet[(bits >> 18) & 63];
        output[out++] = alphabet[(bits >> 12) & 63];
        output[out++] = remaining > 1 ? alphabet[(bits >> 6) & 63] : '=';
        output[out++] = remaining > 2 ? alphabet[bits & 63] : '=';
    }
    output[out] = '\0';
    return true;
}

bool basicAuthMatches(const char* authorization, const char* expectedUser,
                      const char* expectedPassword) {
    if (!authorization || !expectedUser || !expectedPassword) return false;
    const char* space = std::strchr(authorization, ' ');
    if (!space || space == authorization || std::strchr(space + 1, ' ')) return false;
    char scheme[16];
    const size_t schemeLength = static_cast<size_t>(space - authorization);
    if (schemeLength >= sizeof(scheme)) return false;
    std::memcpy(scheme, authorization, schemeLength); scheme[schemeLength] = '\0';
    if (!asciiEqualIgnoreCase(scheme, "Basic")) return false;

    uint8_t decoded[2 * 64 + 2];
    size_t decodedLength = 0;
    if (!base64Decode(space + 1, decoded, sizeof(decoded), decodedLength)) return false;
    char expected[2 * 64 + 2];
    const int expectedLength = std::snprintf(expected, sizeof(expected), "%s:%s",
                                             expectedUser, expectedPassword);
    if (expectedLength < 0 || static_cast<size_t>(expectedLength) >= sizeof(expected)) return false;
    const bool matches = constantTimeEqual(
        decoded, decodedLength, reinterpret_cast<const uint8_t*>(expected),
        static_cast<size_t>(expectedLength));
    volatile uint8_t* decodedWipe = decoded;
    volatile char* expectedWipe = expected;
    for (size_t i = 0; i < sizeof(decoded); ++i) decodedWipe[i] = 0;
    for (size_t i = 0; i < sizeof(expected); ++i) expectedWipe[i] = 0;
    return matches;
}

}  // namespace HttpRequest
