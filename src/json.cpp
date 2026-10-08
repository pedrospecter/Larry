#include "larry/json.hpp"

#include <cctype>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace larry {

namespace {

class JsonReader {
public:
    JsonReader(std::string_view text, std::string_view who) : text_(text), who_(who) {}

    Json read() {
        Json out = value();
        skip_space();
        if (at_ != text_.size()) {
            fail("bytes after the end");
        }
        return out;
    }

private:
    [[noreturn]] void fail(const char* why) const {
        throw std::runtime_error(std::format("{}: the text is not JSON ({} at byte {})", who_, why, at_));
    }
    void skip_space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) {
            ++at_;
        }
    }
    char peek() {
        skip_space();
        if (at_ >= text_.size()) {
            fail("the answer ends early");
        }
        return text_[at_];
    }
    void expect(char c) {
        if (peek() != c) {
            fail("an unexpected character");
        }
        ++at_;
    }
    Json value() {
        const char c = peek();
        if (c == '{') {
            ++at_;
            JsonObject object;
            if (peek() == '}') {
                ++at_;
                return Json{std::move(object)};
            }
            for (;;) {
                std::string key = string();
                expect(':');
                Json v = value();
                object.emplace_back(std::move(key), std::move(v));
                if (peek() == ',') {
                    ++at_;
                    continue;
                }
                expect('}');
                return Json{std::move(object)};
            }
        }
        if (c == '[') {
            ++at_;
            JsonArray array;
            if (peek() == ']') {
                ++at_;
                return Json{std::move(array)};
            }
            for (;;) {
                array.push_back(value());
                if (peek() == ',') {
                    ++at_;
                    continue;
                }
                expect(']');
                return Json{std::move(array)};
            }
        }
        if (c == '"') {
            return Json{string()};
        }
        if (text_.substr(at_, 4) == "true") {
            at_ += 4;
            return Json{true};
        }
        if (text_.substr(at_, 5) == "false") {
            at_ += 5;
            return Json{false};
        }
        if (text_.substr(at_, 4) == "null") {
            at_ += 4;
            return Json{nullptr};
        }
        // A number.
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[at_])) != 0 ||
                                      text_[at_] == '-' || text_[at_] == '+' || text_[at_] == '.' ||
                                      text_[at_] == 'e' || text_[at_] == 'E')) {
            ++at_;
        }
        if (start == at_) {
            fail("an unexpected character");
        }
        return Json{std::strtod(std::string{text_.substr(start, at_ - start)}.c_str(), nullptr)};
    }
    static void put_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    unsigned hex4() {
        if (at_ + 4 > text_.size()) {
            fail("a short escape");
        }
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                fail("a bad escape");
            }
        }
        return code;
    }
    std::string string() {
        expect('"');
        std::string out;
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_ >= text_.size()) {
                break;
            }
            const char e = text_[at_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned code = hex4();
                if (code >= 0xD800 && code <= 0xDBFF && text_.substr(at_, 2) == "\\u") {
                    at_ += 2;
                    const unsigned low = hex4();
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                put_utf8(out, code);
                break;
            }
            default:
                fail("a bad escape");
            }
        }
        fail("a string without an end");
    }

    std::string_view text_;
    std::string_view who_;
    std::size_t at_ = 0;
};

}  // namespace

Json parse_json(std::string_view text, std::string_view who) {
    return JsonReader{text, who}.read();
}

}  // namespace larry
