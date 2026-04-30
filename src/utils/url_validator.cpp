// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "signalwire/utils/url_validator.hpp"

#include "signalwire/logging.hpp"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <sys/socket.h>
#include <utility>

namespace signalwire {
namespace utils {
namespace url_validator {

const std::array<const char*, 9> BLOCKED_NETWORKS = {
    "10.0.0.0/8",
    "172.16.0.0/12",
    "192.168.0.0/16",
    "127.0.0.0/8",
    "169.254.0.0/16",  // link-local / cloud metadata
    "0.0.0.0/8",
    "::1/128",
    "fc00::/7",  // IPv6 private (ULA)
    "fe80::/10", // IPv6 link-local
};

namespace {

ResolverFn& resolver_slot() {
    static ResolverFn slot;
    return slot;
}

bool env_allows_private() {
    const char* raw = std::getenv("SWML_ALLOW_PRIVATE_URLS");
    if (!raw) return false;
    std::string v(raw);
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "1" || v == "true" || v == "yes";
}

// Minimal URL splitter producing (scheme, host).  Hostname is lower-cased
// and unwrapped from IPv6 brackets if present.  Returns empty scheme on
// parse failure.
struct ParsedUrl {
    std::string scheme;
    std::string host;
    bool ok = false;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl p;
    auto colon = url.find(':');
    if (colon == std::string::npos) return p;
    p.scheme = url.substr(0, colon);
    std::string lower = p.scheme;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    p.scheme = lower;
    // Look for "//" host marker.
    if (url.size() < colon + 3 || url[colon + 1] != '/' || url[colon + 2] != '/') {
        // No authority component: e.g. "javascript:alert(1)".
        p.ok = true;
        return p;
    }
    auto auth_start = colon + 3;
    auto auth_end = url.size();
    for (size_t i = auth_start; i < url.size(); ++i) {
        if (url[i] == '/' || url[i] == '?' || url[i] == '#') {
            auth_end = i;
            break;
        }
    }
    std::string authority = url.substr(auth_start, auth_end - auth_start);
    // Strip userinfo (anything before '@').
    auto at = authority.rfind('@');
    if (at != std::string::npos) {
        authority = authority.substr(at + 1);
    }
    // IPv6 in [...]
    if (!authority.empty() && authority.front() == '[') {
        auto rb = authority.find(']');
        if (rb != std::string::npos) {
            p.host = authority.substr(1, rb - 1);
        }
    } else {
        // Strip port
        auto colon2 = authority.find(':');
        p.host = authority.substr(0, colon2);
    }
    p.ok = true;
    return p;
}

// Convert IP string to packed byte sequence.
// IPv4 -> 4 bytes, IPv6 -> 16 bytes.  Returns empty vector on failure.
std::vector<uint8_t> ip_to_bytes(const std::string& ip) {
    in_addr a4{};
    if (::inet_pton(AF_INET, ip.c_str(), &a4) == 1) {
        std::vector<uint8_t> out(4);
        std::memcpy(out.data(), &a4, 4);
        return out;
    }
    in6_addr a6{};
    if (::inet_pton(AF_INET6, ip.c_str(), &a6) == 1) {
        std::vector<uint8_t> out(16);
        std::memcpy(out.data(), &a6, 16);
        return out;
    }
    return {};
}

bool cidr_contains(const std::string& cidr, const std::string& ip) {
    auto slash = cidr.find('/');
    if (slash == std::string::npos) return false;
    std::string net_str = cidr.substr(0, slash);
    int prefix = std::atoi(cidr.substr(slash + 1).c_str());

    auto ip_bytes = ip_to_bytes(ip);
    auto net_bytes = ip_to_bytes(net_str);
    if (ip_bytes.empty() || net_bytes.empty()) return false;
    if (ip_bytes.size() != net_bytes.size()) return false;
    int total = static_cast<int>(ip_bytes.size()) * 8;
    if (prefix < 0 || prefix > total) return false;
    int full = prefix / 8;
    int rem = prefix % 8;
    for (int i = 0; i < full; ++i) {
        if (ip_bytes[i] != net_bytes[i]) return false;
    }
    if (rem > 0) {
        uint8_t mask = static_cast<uint8_t>((0xFF << (8 - rem)) & 0xFF);
        if ((ip_bytes[full] & mask) != (net_bytes[full] & mask)) return false;
    }
    return true;
}

std::optional<std::vector<std::string>> resolve(const std::string& hostname) {
    if (resolver_slot()) {
        return resolver_slot()(hostname);
    }
    // Literal-IP shortcut.
    if (!ip_to_bytes(hostname).empty()) {
        return std::vector<std::string>{hostname};
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    int rc = ::getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (rc != 0) return std::nullopt;
    std::vector<std::string> ips;
    for (auto* p = res; p; p = p->ai_next) {
        char buf[INET6_ADDRSTRLEN] = {0};
        if (p->ai_family == AF_INET) {
            auto* sin = reinterpret_cast<sockaddr_in*>(p->ai_addr);
            ::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
            ips.emplace_back(buf);
        } else if (p->ai_family == AF_INET6) {
            auto* sin6 = reinterpret_cast<sockaddr_in6*>(p->ai_addr);
            ::inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
            ips.emplace_back(buf);
        }
    }
    ::freeaddrinfo(res);
    if (ips.empty()) return std::nullopt;
    return ips;
}

}  // anonymous namespace

void _set_resolver(ResolverFn resolver) {
    resolver_slot() = std::move(resolver);
}

bool validate_url(const std::string& url, bool allow_private) {
    auto& log = ::signalwire::Logger::instance();

    auto p = parse_url(url);
    if (!p.ok) {
        log.warn("URL validation error: malformed URL");
        return false;
    }
    if (p.scheme != "http" && p.scheme != "https") {
        log.warn("URL rejected: invalid scheme " + p.scheme);
        return false;
    }
    if (p.host.empty()) {
        log.warn("URL rejected: no hostname");
        return false;
    }

    if (allow_private || env_allows_private()) {
        return true;
    }

    auto ips = resolve(p.host);
    if (!ips || ips->empty()) {
        log.warn("URL rejected: could not resolve hostname " + p.host);
        return false;
    }

    for (const auto& ip : *ips) {
        for (const auto& cidr : BLOCKED_NETWORKS) {
            if (cidr_contains(cidr, ip)) {
                std::ostringstream oss;
                oss << "URL rejected: " << p.host
                    << " resolves to blocked IP " << ip << " (in " << cidr << ")";
                log.warn(oss.str());
                return false;
            }
        }
    }
    return true;
}

}  // namespace url_validator
}  // namespace utils
}  // namespace signalwire
