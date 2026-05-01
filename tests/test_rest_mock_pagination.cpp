// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_pagination_mock.py.
//
// Drives ``PaginatedIterator`` end-to-end against the local mock_signalwire
// HTTP server. We stage two FIFO scenarios on a known mock endpoint and
// then walk the iterator, verifying:
//   1. items collected (across both pages),
//   2. the journal contains exactly two GETs at the same path,
//   3. the second fetch carries a parsed cursor=page2 query param.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"
#include "signalwire/rest/http_client.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kFabricAddressesPath = "/api/fabric/addresses";
const std::string kFabricAddressesEndpointId = "fabric.list_fabric_addresses";
}

// ---------------------------------------------------------------------------
// Constructor: records http/path/params/data_key without fetching.
// ---------------------------------------------------------------------------

TEST(rest_mock_pagination_init_state) {
    auto client = mocktest::make_client();
    PaginatedIterator it(
        client.http_client(),
        kFabricAddressesPath,
        {{"page_size", "2"}},
        "data");
    ASSERT_EQ(it.path(), kFabricAddressesPath);
    auto p = it.params();
    auto pit = p.find("page_size");
    ASSERT_TRUE(pit != p.end());
    ASSERT_EQ(pit->second, std::string("2"));
    ASSERT_EQ(it.data_key(), std::string("data"));
    ASSERT_EQ(it.index(), (size_t)0);
    ASSERT_TRUE(it.items().empty());
    ASSERT_FALSE(it.done());
    // Journal must be empty -- no HTTP went out.
    auto entries = mocktest::journal();
    ASSERT_TRUE(entries.empty());
    return true;
}

// ---------------------------------------------------------------------------
// Iterator-style usage: nothing happens until you ask for an item.
// ---------------------------------------------------------------------------

TEST(rest_mock_pagination_iter_returns_self) {
    auto client = mocktest::make_client();
    PaginatedIterator it(
        client.http_client(), kFabricAddressesPath, {}, "data");
    // C++ doesn't have a Python-style ``__iter__`` returning self, but the
    // semantic equivalent here is: constructing the iterator and never
    // calling has_next/next leaves the journal empty.
    auto entries = mocktest::journal();
    ASSERT_TRUE(entries.empty());
    return true;
}

// ---------------------------------------------------------------------------
// Walks two pages and stops on the page without ``links.next``.
// ---------------------------------------------------------------------------

TEST(rest_mock_pagination_next_pages_through_all_items) {
    auto client = mocktest::make_client();

    // Page 1 -- has a next cursor.
    mocktest::scenario_set(
        kFabricAddressesEndpointId, 200,
        {
            {"data", json::array({
                {{"id", "addr-1"}, {"name", "first"}},
                {{"id", "addr-2"}, {"name", "second"}},
            })},
            {"links", {{"next", "http://example.com/api/fabric/addresses?cursor=page2"}}},
        });
    // Page 2 -- terminal (no next).
    mocktest::scenario_set(
        kFabricAddressesEndpointId, 200,
        {
            {"data", json::array({
                {{"id", "addr-3"}, {"name", "third"}},
            })},
            {"links", json::object()},
        });

    PaginatedIterator it(
        client.http_client(), kFabricAddressesPath, {}, "data");

    std::vector<std::string> ids;
    while (it.has_next()) {
        json item = it.next();
        ids.push_back(item.value("id", std::string()));
    }

    // All three items, in order.
    ASSERT_EQ(ids.size(), (size_t)3);
    ASSERT_EQ(ids[0], std::string("addr-1"));
    ASSERT_EQ(ids[1], std::string("addr-2"));
    ASSERT_EQ(ids[2], std::string("addr-3"));

    // Journal must have exactly two GETs at the same path.
    auto entries = mocktest::journal();
    int gets = 0;
    int got_cursor_page2 = 0;
    for (const auto& e : entries) {
        if (e.method == "GET" && e.path == kFabricAddressesPath) {
            ++gets;
            auto cit = e.query_params.find("cursor");
            if (cit != e.query_params.end()
                && !cit->second.empty()
                && cit->second.front() == "page2") {
                ++got_cursor_page2;
            }
        }
    }
    ASSERT_EQ(gets, 2);
    ASSERT_EQ(got_cursor_page2, 1);
    return true;
}

// ---------------------------------------------------------------------------
// next() throws after exhaustion (Python: StopIteration).
// ---------------------------------------------------------------------------

TEST(rest_mock_pagination_next_raises_when_done) {
    auto client = mocktest::make_client();

    // One terminal page with a single item.
    mocktest::scenario_set(
        kFabricAddressesEndpointId, 200,
        {
            {"data", json::array({{{"id", "only-one"}}})},
            {"links", json::object()},
        });

    PaginatedIterator it(
        client.http_client(), kFabricAddressesPath, {}, "data");

    json first = it.next();
    ASSERT_EQ(first.value("id", std::string()), std::string("only-one"));

    bool threw = false;
    try {
        (void)it.next();
    } catch (const std::out_of_range&) {
        threw = true;
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
    return true;
}
