// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/namespaces/generated/ResourceTree.hpp"

namespace signalwire {
namespace rest {

using json = nlohmann::json;

// The generated REST resource layer lives in signalwire::rest::generated
// (headers under include/signalwire/rest/namespaces/generated/). RestClient composes the
// generated ResourceTree (§8) and exposes one accessor per namespace/flat
// resource. The generated classes carry ALL the resource surface — CRUD verbs
// (from base_resource.hpp bases), operation methods, command dispatch and
// set_* wrappers — so the hand client keeps ONLY the non-spec-derivable bits:
// auth / HTTP construction / env handling. There is no hand-written resource
// class anymore.

/// Top-level SignalWire REST client. Composes the generated ResourceTree and
/// re-exports every namespace / flat resource as a stable accessor.
class RestClient {
 public:
  RestClient(const std::string& space, const std::string& project_id, const std::string& token);

  /// Initialize from environment variables
  [[nodiscard]] static RestClient from_env();

  /// Construct with an explicit pre-built base URL (`http://...` or
  /// `https://...`) instead of synthesizing one from the SignalWire
  /// space hostname. Used by audit harnesses pointing the client at
  /// loopback fixtures. The space-based constructor remains the
  /// production path.
  [[nodiscard]] static RestClient with_base_url(const std::string& base_url,
                                                const std::string& project_id,
                                                const std::string& token);

  /// Project ID accessor (read-only).
  const std::string& project_id() const { return project_id_; }

  // ========================================================================
  // Namespace / flat-resource accessors — each returns the generated resource
  // (or container) held by the composed ResourceTree. The user-facing shape
  // (client.fabric().tokens, client.registry().brands, client.calling().dial,
  // client.phone_numbers().search, …) is unchanged from the pre-generation
  // client; only the implementation moved into the generated layer.
  // ========================================================================

  generated::FabricNamespace& fabric() { return tree_->fabric; }
  generated::Calling& calling() { return tree_->calling; }
  generated::PhoneNumbers& phone_numbers() { return tree_->phone_numbers; }
  generated::DatasphereNamespace& datasphere() { return tree_->datasphere; }
  generated::VideoNamespace& video() { return tree_->video; }
  generated::Addresses& addresses() { return tree_->addresses; }
  generated::Queues& queues() { return tree_->queues; }
  generated::Recordings& recordings() { return tree_->recordings; }
  generated::NumberGroups& number_groups() { return tree_->number_groups; }
  generated::VerifiedCallers& verified_callers() { return tree_->verified_callers; }
  generated::SipProfile& sip_profile() { return tree_->sip_profile; }
  generated::Lookup& lookup() { return tree_->lookup; }
  generated::ShortCodes& short_codes() { return tree_->short_codes; }
  generated::ImportedNumbers& imported_numbers() { return tree_->imported_numbers; }
  generated::Mfa& mfa() { return tree_->mfa; }
  generated::RegistryNamespace& registry() { return tree_->registry; }
  generated::LogsNamespace& logs() { return tree_->logs; }
  generated::ProjectNamespace& project() { return tree_->project; }
  generated::PubSub& pubsub() { return tree_->pubsub; }
  generated::Chat& chat() { return tree_->chat; }

  /// Get the underlying HTTP client
  const HttpClient& http_client() const { return *client_; }

 private:
  /// (Re)build the composed generated ResourceTree against the current
  /// HttpClient. Called by both constructors after client_ is wired.
  void init_tree() { tree_ = std::make_unique<generated::ResourceTree>(*client_); }

  std::string project_id_;

  std::unique_ptr<HttpClient> client_;
  std::unique_ptr<generated::ResourceTree> tree_;
};

}  // namespace rest
}  // namespace signalwire
