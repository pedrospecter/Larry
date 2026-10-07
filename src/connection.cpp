#include "larry/connection.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace larry {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

// A key as libpq names it, from any spelling, or empty when libpq has no
// such key.
std::string libpq_key(std::string_view given) {
    std::string key;
    for (const char c : given) {
        if (c != ' ' && c != '_' && c != '-') {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    static const std::vector<std::pair<std::string_view, std::string_view>> names = {
        {"host", "host"},           {"server", "host"},          {"datasource", "host"},
        {"hostaddr", "hostaddr"},   {"port", "port"},            {"user", "user"},
        {"username", "user"},       {"userid", "user"},          {"uid", "user"},
        {"login", "user"},          {"password", "password"},    {"pwd", "password"},
        {"pass", "password"},       {"passfile", "passfile"},    {"database", "dbname"},
        {"db", "dbname"},           {"dbname", "dbname"},        {"initialcatalog", "dbname"},
        {"sslmode", "sslmode"},     {"sslrootcert", "sslrootcert"}, {"sslcert", "sslcert"},
        {"sslkey", "sslkey"},       {"connecttimeout", "connect_timeout"},
        {"timeout", "connect_timeout"}, {"applicationname", "application_name"},
        {"options", "options"},     {"service", "service"},      {"clientencoding", "client_encoding"},
        {"keepalives", "keepalives"}, {"targetsessionattrs", "target_session_attrs"},
        {"channelbinding", "channel_binding"}, {"requiressl", "requiressl"},
    };
    for (const auto& [name, libpq] : names) {
        if (key == name) {
            return std::string{libpq};
        }
    }
    return "";
}

std::string lower(std::string_view s) {
    std::string out;
    for (const char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// A value as libpq reads it: quoted when it has spaces, quotes or is empty.
std::string quoted(std::string_view value) {
    const bool plain = !value.empty() && std::ranges::none_of(value, [](char c) {
        return c == ' ' || c == '\'' || c == '\\' || c == '\t';
    });
    if (plain) {
        return std::string{value};
    }
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// One value from a libpq-style string, from `at` (after the '='), quoted or not.
std::string read_value(std::string_view text, std::size_t& at) {
    std::string out;
    if (at < text.size() && text[at] == '\'') {
        ++at;
        while (at < text.size() && text[at] != '\'') {
            if (text[at] == '\\' && at + 1 < text.size()) {
                ++at;
            }
            out.push_back(text[at++]);
        }
        if (at < text.size()) {
            ++at;
        }
        return out;
    }
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) == 0) {
        out.push_back(text[at++]);
    }
    return out;
}

bool is_azure(std::string_view host) {
    static constexpr std::string_view suffix = ".postgres.database.azure.com";
    return host.size() > suffix.size() &&
           lower(host.substr(host.size() - suffix.size())) == suffix;
}

}  // namespace

std::string libpq_connection(std::string_view given, std::string_view default_database) {
    const std::string_view text = trim(given);
    if (text.empty()) {
        return "";
    }
    // A URI stays a URI; Azure gets its SSL mode.
    if (text.starts_with("postgres://") || text.starts_with("postgresql://")) {
        std::string out{text};
        const std::size_t at = out.find('@');
        const std::size_t slash = at == std::string::npos ? std::string::npos : out.find('/', at);
        const std::string host = at == std::string::npos ? std::string{} : out.substr(at + 1, slash == std::string::npos ? std::string::npos : slash - at - 1);
        if (is_azure(host.substr(0, host.find(':'))) && lower(out).find("sslmode=") == std::string::npos) {
            out += out.find('?') == std::string::npos ? "?sslmode=require" : "&sslmode=require";
        }
        return out;
    }
    // Pairs: "key=value" separated by ';' (the .NET form) or by spaces (libpq).
    std::vector<std::pair<std::string, std::string>> pairs;
    if (text.find(';') != std::string_view::npos) {
        std::string_view rest = text;
        while (!rest.empty()) {
            const std::size_t semicolon = rest.find(';');
            const std::string_view item = trim(rest.substr(0, semicolon));
            const std::size_t equals = item.find('=');
            if (equals != std::string_view::npos) {
                pairs.emplace_back(std::string{trim(item.substr(0, equals))},
                                   std::string{trim(item.substr(equals + 1))});
            }
            if (semicolon == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(semicolon + 1);
        }
    } else {
        std::size_t at = 0;
        while (at < text.size()) {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) {
                ++at;
            }
            const std::size_t equals = text.find('=', at);
            if (equals == std::string_view::npos) {
                break;
            }
            std::string key{trim(text.substr(at, equals - at))};
            at = equals + 1;
            pairs.emplace_back(std::move(key), read_value(text, at));
        }
    }
    std::string out;
    std::string host;
    bool has_sslmode = false;
    for (auto& [given_key, value] : pairs) {
        const std::string key = libpq_key(given_key);
        if (key.empty()) {
            continue;
        }
        if (key == "dbname" && (value.empty() || value == "{0}")) {
            value = std::string{default_database};
        }
        if (key == "sslmode") {
            value = lower(value);
            if (value == "verifyfull") {
                value = "verify-full";
            } else if (value == "verifyca") {
                value = "verify-ca";
            }
            has_sslmode = true;
        }
        if (key == "host") {
            host = value;
        }
        out += out.empty() ? "" : " ";
        out += key + '=' + quoted(value);
    }
    if (!has_sslmode && is_azure(host)) {
        out += out.empty() ? "" : " ";
        out += "sslmode=require";
    }
    return out;
}

}  // namespace larry
