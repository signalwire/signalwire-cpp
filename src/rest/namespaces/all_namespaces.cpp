// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// The REST resource surface is GENERATED
// (include/signalwire/rest/namespaces/generated/) and header-only; RestClient
// composes the generated ResourceTree (§8). There are no hand-written resource
// implementations to compile here. This translation unit exists only to satisfy
// the build system's expectation of a source file in the namespaces directory.
//
// User-facing accessors (unchanged across the generated-surface adoption):
//   client.fabric().subscribers.list()
//   client.calling().dial({...})
//   client.phone_numbers().search({{"areacode", "512"}})
//   client.datasphere().documents.search({...})
//   client.video().rooms.list()
//   client.registry().brands.list()
//   client.logs().messages.list()
//   client.project().tokens.create({...})
//   client.pubsub().create_token({...})   // generated snake_case op methods
//   client.chat().create_token({...})
