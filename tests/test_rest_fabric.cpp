// REST Fabric namespace tests
#include "signalwire/rest/rest_client.hpp"
using namespace signalwire::rest;
using json = nlohmann::json;

TEST(rest_fabric_namespace_accessible) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& fabric = client.fabric();
    (void)fabric;
    return true;
}

TEST(rest_fabric_has_subscribers) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& subs = client.fabric().subscribers;
    (void)subs;
    return true;
}

TEST(rest_fabric_has_addresses) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& addr = client.fabric().addresses;
    (void)addr;
    return true;
}

TEST(rest_fabric_has_sip_endpoints) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& sip = client.fabric().sip_endpoints;
    (void)sip;
    return true;
}

TEST(rest_fabric_has_call_flows) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& cf = client.fabric().call_flows;
    (void)cf;
    return true;
}

TEST(rest_fabric_has_swml_scripts) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& s = client.fabric().swml_scripts;
    (void)s;
    return true;
}

TEST(rest_fabric_has_conference_rooms) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& c = client.fabric().conference_rooms;
    (void)c;
    return true;
}

TEST(rest_fabric_has_all_sub_resources) {
    RestClient client("example.signalwire.com", "proj", "tok");
    auto& f = client.fabric();
    (void)f.resources;
    (void)f.tokens;
    (void)f.addresses;
    (void)f.subscribers;
    (void)f.swml_scripts;
    (void)f.cxml_applications;
    return true;
}
