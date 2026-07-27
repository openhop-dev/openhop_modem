#include "../include/http_request_parser.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace HttpRequest;

static Result feed(Parser& parser, const std::string& text, uint32_t now = 1) {
    return parser.feed(reinterpret_cast<const uint8_t*>(text.data()), text.size(), now);
}

static void expect(Result actual, Result expected) {
    if (actual != expected) {
        std::cerr << "result mismatch actual=" << static_cast<int>(actual)
                  << " expected=" << static_cast<int>(expected) << "\n";
    }
    assert(actual == expected);
}

static void test_fragmented_request_and_case_insensitive_headers() {
    Parser parser;
    parser.reset(100, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nhOsT: modem\r\ncontent-", 150), Result::NEED_MORE);
    expect(feed(parser, "LENGTH: 7\r\nX-Test: Value\r\n\r\na=1&b=", 200), Result::NEED_MORE);
    expect(feed(parser, "2", 250), Result::COMPLETE);
    assert(parser.request().method == Method::POST);
    assert(std::strcmp(parser.request().route, "/api/config") == 0);
    assert(std::strcmp(parser.header("HOST"), "modem") == 0);
    assert(std::strcmp(parser.header("x-test"), "Value") == 0);
    assert(parser.request().bodyLength == 7);
}

static void test_request_line_and_header_limits() {
    Parser parser;
    parser.reset(0, 1000);
    std::string longTarget(250, 'a');
    expect(feed(parser, "GET /" + longTarget + " HTTP/1.1\r\n"), Result::BAD_REQUEST);

    parser.reset(0, 1000);
    std::string headers = "GET / HTTP/1.1\r\nX: ";
    headers.append(MAX_HEADER_BYTES, 'a');
    expect(feed(parser, headers), Result::PAYLOAD_TOO_LARGE);
}

static void test_methods_and_request_line_validation() {
    Parser parser;
    parser.reset(0, 1000);
    expect(feed(parser, "PUT / HTTP/1.1\r\n\r\n"), Result::METHOD_NOT_ALLOWED);
    parser.reset(0, 1000);
    expect(feed(parser, "GET / HTTP/2\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "GET  / HTTP/1.1\r\n\r\n"), Result::BAD_REQUEST);
}

static void test_content_length_rules_and_transfer_encoding() {
    Parser parser;
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: x\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\na"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nAuthorization: Basic YQ==\r\nauthorization: Basic Yg==\r\nContent-Length: 0\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Type: application/json\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 2049\r\n\r\n"), Result::PAYLOAD_TOO_LARGE);
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n"), Result::BAD_REQUEST);
    parser.reset(0, 1000);
    expect(feed(parser, "GET / HTTP/1.1\r\nContent-Length: 1\r\n\r\na"), Result::BAD_REQUEST);
}

static void test_exact_body_truncation_and_extra_detection() {
    Parser parser;
    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 4\r\n\r\na=1", 1), Result::NEED_MORE);
    expect(parser.finish(), Result::BAD_REQUEST);

    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 3\r\n\r\na=1X", 1), Result::BAD_REQUEST);

    parser.reset(0, 1000);
    expect(feed(parser, "POST /api/config HTTP/1.1\r\nContent-Length: 3\r\n\r\na=1", 1), Result::COMPLETE);
    expect(feed(parser, "X", 2), Result::BAD_REQUEST);
}

static void test_timeout_resets_on_progress_and_wraps_safely() {
    Parser parser;
    parser.reset(100, 50);
    expect(parser.poll(149), Result::NEED_MORE);
    expect(feed(parser, "G", 149), Result::NEED_MORE);
    expect(parser.poll(198), Result::NEED_MORE);
    expect(parser.poll(199), Result::TIMED_OUT);

    parser.reset(0xfffffff0u, 32);
    expect(feed(parser, "G", 0xfffffff8u), Result::NEED_MORE);
    expect(parser.poll(0x00000010u), Result::NEED_MORE);
    expect(parser.poll(0x00000018u), Result::TIMED_OUT);
}

static void test_route_normalization_and_traversal_rejection() {
    Parser parser;
    parser.reset(0, 1000);
    expect(feed(parser, "GET //api//stats?x=1 HTTP/1.1\r\n\r\n"), Result::COMPLETE);
    assert(parser.routeEquals("/api/stats"));
    assert(std::strcmp(parser.request().query, "x=1") == 0);
    assert(!parser.routeEquals("/api/stat"));

    const char* bad[] = {"/../stats", "/a/./b", "/%2e%2e/stats", "/a%2fb/../c", "/a\\b"};
    for (const char* route : bad) {
        parser.reset(0, 1000);
        expect(feed(parser, std::string("GET ") + route + " HTTP/1.1\r\n\r\n"), Result::BAD_REQUEST);
    }
}

static void test_url_decode_and_form_parser() {
    char decoded[64];
    assert(urlDecode("hello+world%21", decoded, sizeof(decoded), true));
    assert(std::strcmp(decoded, "hello world!") == 0);
    assert(!urlDecode("bad%2", decoded, sizeof(decoded), true));
    assert(!urlDecode("%00", decoded, sizeof(decoded), true));

    FormData form;
    assert(parseForm("name=RAK%204631&empty=&plus=a%2Bb", form));
    assert(std::strcmp(form.get("name"), "RAK 4631") == 0);
    assert(std::strcmp(form.get("empty"), "") == 0);
    assert(std::strcmp(form.get("plus"), "a+b") == 0);
    assert(form.get("missing") == nullptr);
    assert(!parseForm("broken", form));
}

static void test_base64_and_basic_auth_without_exposing_credentials() {
    char encoded[128];
    assert(base64Encode(reinterpret_cast<const uint8_t*>("admin:password"), 14,
                        encoded, sizeof(encoded)));
    assert(std::strcmp(encoded, "YWRtaW46cGFzc3dvcmQ=") == 0);
    assert(basicAuthMatches("Basic YWRtaW46cGFzc3dvcmQ=", "admin", "password"));
    assert(basicAuthMatches("basic YWRtaW46cGFzc3dvcmQ=", "admin", "password"));
    assert(!basicAuthMatches("Bearer YWRtaW46cGFzc3dvcmQ=", "admin", "password"));
    assert(!basicAuthMatches("Basic !!!", "admin", "password"));
    assert(!basicAuthMatches("Basic YWRtaW46cGFzc3dvcmQ= trailing", "admin", "password"));
    assert(!basicAuthMatches("Basic YWRtaW46d3Jvbmc=", "admin", "password"));
}

static void test_connection_close_contract() {
    Parser parser;
    parser.reset(0, 1000);
    expect(feed(parser, "GET / HTTP/1.1\r\nConnection: keep-alive\r\n\r\n"), Result::COMPLETE);
    assert(parser.request().connectionClose);
}

int main() {
    test_fragmented_request_and_case_insensitive_headers();
    test_request_line_and_header_limits();
    test_methods_and_request_line_validation();
    test_content_length_rules_and_transfer_encoding();
    test_exact_body_truncation_and_extra_detection();
    test_timeout_resets_on_progress_and_wraps_safely();
    test_route_normalization_and_traversal_rejection();
    test_url_decode_and_form_parser();
    test_base64_and_basic_auth_without_exposing_credentials();
    test_connection_close_contract();
    std::cout << "http_request_parser_test: OK\n";
    return 0;
}
