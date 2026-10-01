// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/ai_chat/gateway.hpp"

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <utility>

#include "gateway_detail.hpp"
#include "httplib.h"
#include "signalwire/common.hpp"
#include "signalwire/logging.hpp"

namespace signalwire {
namespace ai_chat {

namespace detail {

std::string b64url_encode(const std::string& raw) {
  static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  for (unsigned char c : raw) {
    buf = (buf << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out.push_back(kAlphabet[(buf >> bits) & 0x3F]);
    }
  }
  if (bits > 0) {
    out.push_back(kAlphabet[(buf << (6 - bits)) & 0x3F]);
  }
  return out;  // unpadded
}

std::optional<std::string> b64url_decode(const std::string& text) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '-') {
      return 62;
    }
    if (c == '_') {
      return 63;
    }
    return -1;
  };
  std::string out;
  uint32_t buf = 0;
  int bits = 0;
  size_t symbols = 0;
  for (char c : text) {
    int v = val(c);
    if (v < 0) {
      continue;  // the reference's decoder discards non-alphabet characters
    }
    ++symbols;
    buf = (buf << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xFF));
    }
  }
  if (symbols % 4 == 1) {
    return std::nullopt;  // no valid padding makes this a whole number of bytes
  }
  return out;
}

std::string hmac_sha256_raw(const std::string& key, const std::string& message) {
  unsigned char out[EVP_MAX_MD_SIZE];
  unsigned int out_len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(message.data()), message.size(), out, &out_len);
  return std::string(reinterpret_cast<const char*>(out), out_len);
}

bool constant_time_equal(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return false;
  }
  return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string random_bytes(size_t n) {
  std::string out(n, '\0');
  if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(n)) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  return out;
}

double monotonic_seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::optional<std::string> header_value(const httplib::Request& req, const std::string& name) {
  if (!req.has_header(name)) {
    return std::nullopt;
  }
  return req.get_header_value(name);
}

json read_json_body(const httplib::Request& req, size_t limit) {
  auto declared = header_value(req, "Content-Length");
  if (declared.has_value() && !declared->empty() &&
      std::all_of(declared->begin(), declared->end(),
                  [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
      (declared->size() > 18 || std::stoull(*declared) > limit)) {
    throw GatewayRejection(413, "request too large");
  }
  if (req.body.size() > limit) {
    throw GatewayRejection(413, "request too large");
  }
  json parsed = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    throw std::invalid_argument("body is not JSON");
  }
  return parsed;
}

void send_json(httplib::Response& res, int status, const json& body,
               const std::map<std::string, std::string>& headers) {
  res.status = status;
  for (const auto& [k, v] : headers) {
    res.set_header(k, v);
  }
  res.set_content(body.dump(), "application/json");
}

bool truthy(const json& v) {
  if (v.is_null()) {
    return false;
  }
  if (v.is_boolean()) {
    return v.get<bool>();
  }
  if (v.is_number_integer() || v.is_number_unsigned()) {
    return v.get<long long>() != 0;
  }
  if (v.is_number_float()) {
    return v.get<double>() != 0.0;
  }
  if (v.is_string()) {
    return !v.get_ref<const std::string&>().empty();
  }
  return !v.empty();
}

}  // namespace detail

namespace {

constexpr const char* kLocalHosts[] = {"localhost", "127.0.0.1", "::1", "[::1]"};

// The hostname of an origin URL (scheme://host[:port]), lowercased; IPv6
// brackets stripped as urllib's ``hostname`` does.
std::string origin_hostname(const std::string& origin) {
  std::string rest = origin;
  auto scheme = rest.find("://");
  if (scheme != std::string::npos) {
    rest = rest.substr(scheme + 3);
  }
  auto slash = rest.find_first_of("/?#");
  if (slash != std::string::npos) {
    rest = rest.substr(0, slash);
  }
  auto at = rest.rfind('@');
  if (at != std::string::npos) {
    rest = rest.substr(at + 1);
  }
  std::string host;
  if (!rest.empty() && rest.front() == '[') {
    auto close = rest.find(']');
    host = rest.substr(1, close == std::string::npos ? std::string::npos : close - 1);
  } else {
    auto colon = rest.find(':');
    host = rest.substr(0, colon);
  }
  std::transform(host.begin(), host.end(), host.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return host;
}

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  return s;
}

bool is_dated(const json& ts) {
  return (ts.is_number_integer() || ts.is_number_unsigned()) && ts.get<long long>() > 0;
}

bool has_text(const json& content) {
  if (!content.is_string()) {
    return false;
  }
  const auto& s = content.get_ref<const std::string&>();
  return std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) == 0; });
}

}  // namespace

ChatGateway::ChatGateway(const ChatGatewayOptions& options)
    : config_url_(options.config_url),
      handle_ttl_(options.handle_ttl),
      conversation_timeout_(options.conversation_timeout),
      max_new_conversations_(options.max_new_conversations),
      max_turns_(options.max_turns),
      window_seconds_(options.window_seconds),
      client_(options.client),
      owns_client_(options.client == nullptr) {
  if (config_url_.empty()) {
    throw std::invalid_argument("config_url is required — it is what a key is scoped to.");
  }
  key_ = options.key;
  if (key_.empty()) {
    key_ = get_env("SIGNALWIRE_CHAT_GATEWAY_KEY");
  }
  if (key_.empty()) {
    key_ = "pk_" + detail::b64url_encode(detail::random_bytes(24));
  }
  for (const auto& o : options.allowed_origins) {
    allowed_origins_.insert(rstrip_slash(o));
  }
  if (!client_) {
    client_ = std::make_shared<AIChatClient>();
  }
  secret_ = options.secret;
  if (secret_.empty()) {
    secret_ = get_env("SIGNALWIRE_CHAT_GATEWAY_SECRET");
  }
  if (secret_.empty()) {
    secret_ = detail::random_bytes(32);
  }
}

std::optional<double> ChatGateway::last_activity(const std::vector<json>& messages) {
  std::optional<long long> newest;
  {
    for (const auto& msg : messages) {
      if (!msg.is_object() || !msg.contains("timestamp")) {
        continue;
      }
      const json& ts = msg["timestamp"];
      if (is_dated(ts) && (!newest || ts.get<long long>() > *newest)) {
        newest = ts.get<long long>();
      }
    }
  }
  if (!newest) {
    return std::nullopt;
  }
  return static_cast<double>(*newest) / 1000000.0;
}

int ChatGateway::effective_timeout() const {
  return (conversation_timeout_ && *conversation_timeout_ != 0)
             ? *conversation_timeout_
             : SERVICE_DEFAULT_CONVERSATION_TIMEOUT;
}

void ChatGateway::close() {
  if (owns_client_ && client_) {
    client_->close();
  }
}

std::string ChatGateway::mint_handle(const std::optional<std::string>& conversation_id) const {
  std::string id = (conversation_id && !conversation_id->empty())
                       ? *conversation_id
                       : "chat-" + detail::b64url_encode(detail::random_bytes(18));
  long long expires = static_cast<long long>(std::time(nullptr)) + handle_ttl_;
  std::string payload = id + ":" + std::to_string(expires);
  std::string sig = detail::hmac_sha256_raw(secret_, payload);
  return detail::b64url_encode(payload) + "." + detail::b64url_encode(sig);
}

std::string ChatGateway::read_handle(const std::string& handle) const {
  auto dot = handle.find('.');
  if (dot == std::string::npos) {
    throw GatewayRejection(400, "malformed handle");
  }
  auto payload = detail::b64url_decode(handle.substr(0, dot));
  auto given = detail::b64url_decode(handle.substr(dot + 1));
  if (!payload || !given) {
    throw GatewayRejection(400, "malformed handle");
  }
  if (!detail::constant_time_equal(*given, detail::hmac_sha256_raw(secret_, *payload))) {
    throw GatewayRejection(403, "invalid handle");
  }
  auto colon = payload->rfind(':');
  if (colon == std::string::npos) {
    throw GatewayRejection(400, "malformed handle");
  }
  std::string conversation_id = payload->substr(0, colon);
  long long expires = 0;
  try {
    size_t used = 0;
    expires = std::stoll(payload->substr(colon + 1), &used);
    if (used != payload->size() - colon - 1) {
      throw GatewayRejection(400, "malformed handle");
    }
  } catch (const GatewayRejection&) {
    throw;
  } catch (const std::exception&) {
    throw GatewayRejection(400, "malformed handle");
  }
  if (static_cast<long long>(std::time(nullptr)) > expires) {
    throw GatewayRejection(403, "expired handle");
  }
  return conversation_id;
}

void ChatGateway::check_origin(const std::optional<std::string>& origin) const {
  if (!origin.has_value()) {
    return;
  }
  std::string host = origin_hostname(*origin);
  for (const char* local : kLocalHosts) {
    if (host == local) {
      return;
    }
  }
  const std::string suffix = ".localhost";
  if (host.size() > suffix.size() &&
      host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return;
  }
  if (allowed_origins_.count(rstrip_slash(*origin)) > 0) {
    return;
  }
  throw GatewayRejection(403, "origin not allowed");
}

void ChatGateway::check_key(const std::optional<std::string>& presented) const {
  if (!presented || presented->empty() || !detail::constant_time_equal(*presented, key_)) {
    throw GatewayRejection(401, "bad key");
  }
}

json ChatGateway::visible_messages(const std::vector<json>& messages) {
  json out = json::array();
  for (const auto& msg : messages) {
    if (!msg.is_object()) {
      continue;
    }
    std::string role = msg.value("role", json()).is_string() ? msg["role"].get<std::string>() : "";
    if ((role != "user" && role != "assistant") || !msg.contains("content") ||
        !has_text(msg["content"])) {
      continue;
    }
    json entry = {{"role", role}, {"content", msg["content"]}};
    if (msg.contains("timestamp") && is_dated(msg["timestamp"])) {
      entry["timestamp"] = static_cast<double>(msg["timestamp"].get<long long>()) / 1000000.0;
    }
    out.push_back(entry);
  }
  return out;
}

void ChatGateway::charge_mint() {
  const std::lock_guard<std::mutex> lock(mutex_);
  double now = detail::monotonic_seconds();
  double cutoff = now - window_seconds_;
  mints_.erase(
      std::remove_if(mints_.begin(), mints_.end(), [cutoff](double t) { return t <= cutoff; }),
      mints_.end());
  if (static_cast<int>(mints_.size()) >= max_new_conversations_) {
    throw GatewayRejection(429, "too many new conversations");
  }
  mints_.push_back(now);
}

void ChatGateway::charge_turn(const std::string& conversation_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  double now = detail::monotonic_seconds();
  // Sweep here rather than on a timer: a handle cannot outlive its TTL.
  double cutoff = now - handle_ttl_;
  for (auto it = turns_.begin(); it != turns_.end();) {
    it = (it->second.second > cutoff) ? std::next(it) : turns_.erase(it);
  }
  int count = 0;
  auto it = turns_.find(conversation_id);
  if (it != turns_.end()) {
    count = it->second.first;
  }
  if (count >= max_turns_) {
    throw GatewayRejection(429, "conversation turn limit reached");
  }
  turns_[conversation_id] = {count + 1, now};
}

std::optional<json> ChatGateway::read_user_metadata(const std::map<std::string, json>& body) const {
  auto it = body.find("user_meta_data");
  if (it == body.end() || it->second.is_null()) {
    return std::nullopt;
  }
  const json& raw = it->second;
  if (!raw.is_object()) {
    throw GatewayRejection(400, "user_meta_data must be an object");
  }
  if (raw.empty()) {
    return std::nullopt;
  }
  if (raw.dump().size() > MAX_USER_METADATA_BYTES) {
    throw GatewayRejection(413, "user_meta_data too large");
  }
  return raw;
}

std::tuple<std::string, json, std::optional<std::string>> ChatGateway::prepare(
    const std::map<std::string, json>& body_map, const std::optional<std::string>& origin,
    const std::optional<std::string>& key) {
  const json body(body_map);
  check_key(key);
  check_origin(origin);

  std::string method = "chat";
  if (body.contains("method")) {
    if (!body["method"].is_string()) {
      throw GatewayRejection(400, "method not allowed");
    }
    method = body["method"].get<std::string>();
  }
  if (method != "start" && method != "chat" && method != "log" && method != "end") {
    throw GatewayRejection(400, "method not allowed");
  }

  // Read before minting so a malformed bag costs the caller nothing.
  std::optional<json> user_metadata = read_user_metadata(body_map);

  const json message = body.contains("message") ? body["message"] : json();
  if (method == "chat" && message.is_string() &&
      message.get_ref<const std::string&>().size() > MAX_MESSAGE_BYTES) {
    throw GatewayRejection(413, "message too large");
  }

  std::string conversation_id;
  std::optional<std::string> minted;
  const json handle = body.contains("handle") ? body["handle"] : json();
  if (detail::truthy(handle)) {
    if (!handle.is_string()) {
      throw GatewayRejection(400, "malformed handle");
    }
    conversation_id = read_handle(handle.get<std::string>());
  } else if (method == "end" || method == "log") {
    throw GatewayRejection(400, method + " requires a handle");
  } else {
    charge_mint();
    minted = mint_handle();
    conversation_id = read_handle(*minted);
  }

  if (method == "end") {
    return {"end_conversation", json{{"id", conversation_id}}, std::nullopt};
  }
  if (method == "log") {
    return {"chat_log", json{{"id", conversation_id}}, std::nullopt};
  }
  if (method == "start") {
    json params = {{"id", conversation_id}, {"config_url", config_url_}};
    if (conversation_timeout_ && *conversation_timeout_ != 0) {
      params["conversation_timeout"] = *conversation_timeout_;
    }
    if (user_metadata) {
      params["user_meta_data"] = *user_metadata;
    }
    return {"create_conversation", params, minted};
  }

  if (!has_text(message)) {
    throw GatewayRejection(400, "message is required");
  }
  charge_turn(conversation_id);
  json chat_params = {{"id", conversation_id}, {"message", message}, {"config_url", config_url_}};
  if (conversation_timeout_ && *conversation_timeout_ != 0) {
    chat_params["conversation_timeout"] = *conversation_timeout_;
  }
  if (user_metadata) {
    chat_params["user_meta_data"] = *user_metadata;
  }
  return {"chat", chat_params, minted};
}

server::Router ChatGateway::router() {
  return [this](httplib::Server& server, const std::string& prefix) {
    const std::string path = prefix + "/";

    auto cors = [this](const std::optional<std::string>& origin) {
      std::map<std::string, std::string> headers;
      if (!origin.has_value()) {
        return headers;
      }
      try {
        check_origin(origin);
      } catch (const GatewayRejection&) {
        return headers;
      }
      headers["Access-Control-Allow-Origin"] = *origin;
      headers["Access-Control-Expose-Headers"] = "X-Chat-Handle";
      headers["Vary"] = "Origin";
      return headers;
    };

    server.Options(path, [cors](const httplib::Request& req, httplib::Response& res) {
      auto headers = cors(detail::header_value(req, "Origin"));
      if (!headers.empty()) {
        headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type";
        headers["Access-Control-Allow-Methods"] = "POST, OPTIONS";
        headers["Access-Control-Max-Age"] = "600";
      }
      res.status = 204;
      for (const auto& [k, v] : headers) {
        res.set_header(k, v);
      }
    });

    server.Post(path, [this, cors](const httplib::Request& req, httplib::Response& res) {
      auto origin = detail::header_value(req, "Origin");
      std::optional<std::string> key;
      auto auth = detail::header_value(req, "Authorization");
      if (auth && auth->size() >= 7) {
        std::string scheme = auth->substr(0, 7);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (scheme == "bearer ") {
          key = auth->substr(7);
        }
      }
      auto headers = cors(origin);

      std::string method;
      json params;
      std::optional<std::string> minted;
      try {
        json body = detail::read_json_body(req, MAX_REQUEST_BODY_BYTES);
        if (!body.is_object()) {
          throw GatewayRejection(400, "body must be an object");
        }
        std::tie(method, params, minted) =
            prepare(body.get<std::map<std::string, json>>(), origin, key);
      } catch (const GatewayRejection& rej) {
        detail::send_json(res, rej.status(), json{{"error", rej.reason()}}, headers);
        return;
      } catch (const std::exception&) {
        detail::send_json(res, 400, json{{"error", "bad request"}}, headers);
        return;
      }

      try {
        if (method == "end_conversation") {
          (void)client_->end(params["id"].get<std::string>());
          detail::send_json(res, 200, json{{"status", "ended"}}, headers);
          return;
        }
        if (method == "create_conversation") {
          CreateConversationOptions opts;
          opts.config_url = params["config_url"].get<std::string>();
          if (params.contains("conversation_timeout")) {
            opts.timeout = params["conversation_timeout"].get<int>();
          }
          if (params.contains("user_meta_data")) {
            opts.user_metadata = params["user_meta_data"];
          }
          ConversationInfo info =
              client_->create_conversation(params["id"].get<std::string>(), opts);
          if (minted) {
            headers["X-Chat-Handle"] = *minted;
          }
          detail::send_json(res, 200,
                            json{{"greeting", info.has_initial_message ? json(info.initial_message)
                                                                       : json(nullptr)},
                                 {"status", info.status},
                                 {"timeout", effective_timeout()}},
                            headers);
          return;
        }
        if (method == "chat_log") {
          ChatLog log = client_->log(params["id"].get<std::string>());
          auto last = last_activity(log.messages);
          detail::send_json(res, 200,
                            json{{"messages", visible_messages(log.messages)},
                                 {"timeout", effective_timeout()},
                                 {"last_activity", last ? json(*last) : json(nullptr)}},
                            headers);
          return;
        }
      } catch (const std::exception&) {
        detail::send_json(res, 502, json{{"error", "upstream error"}}, headers);
        return;
      }

      // chat: stream the service's response body through unbuffered.
      if (minted) {
        headers["X-Chat-Handle"] = *minted;
      }
      for (const auto& [k, v] : headers) {
        res.set_header(k, v);
      }
      res.status = 200;
      auto client = client_;
      res.set_chunked_content_provider(
          "application/json", [client, method, params](size_t, httplib::DataSink& sink) {
            try {
              (void)client->raw_post(method, params, [&sink](const char* data, size_t length) {
                return sink.write(data, length);
              });
            } catch (const std::exception& e) {
              // Headers are already committed; log and end the body.
              get_logger().warn(std::string("ai_chat_gateway_stream_failed error=") + e.what());
            }
            sink.done();
            return true;
          });
    });
  };
}

}  // namespace ai_chat
}  // namespace signalwire
