// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "signalwire/rest/http_client.hpp"

// Hand-written base hierarchy for the GENERATED REST resource layer
// (include/signalwire/rest/namespaces/generated/*.hpp). Mirrors Python's
// signalwire/rest/_base.py: BaseResource (bare receiver), ReadResource
// (list/get), CrudResource (list/create/get/update/delete with a per-resource
// update verb), FabricResource (CrudResource + list_addresses). Each generated
// resource class EXTENDS one of these and supplies only its resource-specific
// operation / command / set_* / sub-collection methods; the shared CRUD verbs
// live here so the emitted headers stay thin.
//
// These live in ``signalwire::rest::generated``; the generated subclasses
// resolve the unqualified base name here first.

namespace signalwire {
namespace rest {
namespace generated {

using json = nlohmann::json;

/// Bare resource: holds the receiver (HttpClient) + composed base path.
/// Generated ``BaseResource`` subclasses declare every method themselves.
class BaseResource {
 public:
  BaseResource(const HttpClient& client, const std::string& base_path)
      : client_(client), base_path_(base_path) {}

  const std::string& base_path() const { return base_path_; }

 protected:
  const HttpClient& client_;
  std::string base_path_;
};

/// Read-only resource: ``list`` + ``get`` (Python ReadResource).
class ReadResource : public BaseResource {
 public:
  ReadResource(const HttpClient& client, const std::string& base_path)
      : BaseResource(client, base_path) {}

  [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
    return client_.get(base_path_, params);
  }
  [[nodiscard]] json get(const std::string& id,
                         const std::map<std::string, std::string>& params = {}) const {
    return client_.get(base_path_ + "/" + id, params);
  }
};

/// Full CRUD resource (Python CrudResource). The update verb (PUT vs PATCH) is
/// baked in at construction, mirroring Python's ``_update_method``.
class CrudResource : public BaseResource {
 public:
  CrudResource(const HttpClient& client, const std::string& base_path,
               const std::string& update_method = "PATCH")
      : BaseResource(client, base_path), update_method_(update_method) {}

  [[nodiscard]] json list(const std::map<std::string, std::string>& params = {}) const {
    return client_.get(base_path_, params);
  }
  [[nodiscard]] json create(const json& data) const { return client_.post(base_path_, data); }
  [[nodiscard]] json get(const std::string& id,
                         const std::map<std::string, std::string>& params = {}) const {
    return client_.get(base_path_ + "/" + id, params);
  }
  [[nodiscard]] json update(const std::string& id, const json& data) const {
    return (update_method_ == "PUT") ? client_.put(base_path_ + "/" + id, data)
                                     : client_.patch(base_path_ + "/" + id, data);
  }
  /// ``delete_`` — ``delete`` is a C++ keyword; the enumerator renames this to
  /// the canonical ``delete`` (reserved-word rename, wire verb DELETE preserved).
  [[nodiscard]] json delete_(const std::string& id) const {
    return client_.del(base_path_ + "/" + id);
  }

 protected:
  std::string update_method_;
};

/// Fabric resource: CRUD + the ``list_addresses`` sub-collection (Python
/// FabricResource / CrudWithAddresses). The base ``list_addresses`` hangs off
/// this resource's own base path; the four sibling-path fabric subclasses
/// (CallFlows / ConferenceRooms / CxmlApplications / GenericResources) override
/// it with their singularised sub-path in the generated header (L12).
class FabricResource : public CrudResource {
 public:
  FabricResource(const HttpClient& client, const std::string& base_path,
                 const std::string& update_method = "PATCH")
      : CrudResource(client, base_path, update_method) {}

  [[nodiscard]] json list_addresses(const std::string& id,
                                    const std::map<std::string, std::string>& params = {}) const {
    return client_.get(base_path_ + "/" + id + "/addresses", params);
  }
};

}  // namespace generated
}  // namespace rest
}  // namespace signalwire
