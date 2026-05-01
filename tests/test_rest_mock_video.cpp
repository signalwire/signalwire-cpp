// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_video_mock.py.
//
// Covers the Video API surface: room sub-streams, room sessions, room
// recordings, conference sub-collections, conference tokens, and
// top-level streams. Each test asserts on body shape AND
// mocktest::journal_last() so both halves of the contract are checked.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// Rooms - streams sub-resource
// ---------------------------------------------------------------------------

TEST(rest_mock_video_rooms_list_streams_returns_data_collection) {
    auto client = mocktest::make_client();
    auto body = client.video().rooms.list_streams("room-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/rooms/room-1/streams"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_video_rooms_create_stream_posts_kwargs_in_body) {
    auto client = mocktest::make_client();
    auto body = client.video().rooms.create_stream(
        "room-1", {{"url", "rtmp://example.com/live"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/video/rooms/room-1/streams"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("url", std::string()), std::string("rtmp://example.com/live"));
    return true;
}

// ---------------------------------------------------------------------------
// Room Sessions
// ---------------------------------------------------------------------------

TEST(rest_mock_video_room_sessions_list_returns_data_collection) {
    auto client = mocktest::make_client();
    auto body = client.video().room_sessions.list();
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_sessions"));
    return true;
}

TEST(rest_mock_video_room_sessions_get_returns_session_object) {
    auto client = mocktest::make_client();
    auto body = client.video().room_sessions.get("sess-abc");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_sessions/sess-abc"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_video_room_sessions_list_events_uses_events_subpath) {
    auto client = mocktest::make_client();
    auto body = client.video().room_sessions.list_events("sess-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_sessions/sess-1/events"));
    return true;
}

TEST(rest_mock_video_room_sessions_list_recordings_uses_recordings_subpath) {
    auto client = mocktest::make_client();
    auto body = client.video().room_sessions.list_recordings("sess-2");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_sessions/sess-2/recordings"));
    return true;
}

// ---------------------------------------------------------------------------
// Room Recordings
// ---------------------------------------------------------------------------

TEST(rest_mock_video_room_recordings_list_returns_data_collection) {
    auto client = mocktest::make_client();
    auto body = client.video().room_recordings.list();
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_recordings"));
    return true;
}

TEST(rest_mock_video_room_recordings_get_returns_single_recording) {
    auto client = mocktest::make_client();
    auto body = client.video().room_recordings.get("rec-xyz");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_recordings/rec-xyz"));
    return true;
}

TEST(rest_mock_video_room_recordings_delete_returns_dict_for_204) {
    auto client = mocktest::make_client();
    auto body = client.video().room_recordings.delete_("rec-del");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/video/room_recordings/rec-del"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_video_room_recordings_list_events_uses_events_subpath) {
    auto client = mocktest::make_client();
    auto body = client.video().room_recordings.list_events("rec-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/room_recordings/rec-1/events"));
    return true;
}

// ---------------------------------------------------------------------------
// Conferences sub-collections
// ---------------------------------------------------------------------------

TEST(rest_mock_video_conferences_list_conference_tokens) {
    auto client = mocktest::make_client();
    auto body = client.video().conferences.list_conference_tokens("conf-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/conferences/conf-1/conference_tokens"));
    return true;
}

TEST(rest_mock_video_conferences_list_streams) {
    auto client = mocktest::make_client();
    auto body = client.video().conferences.list_streams("conf-2");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/conferences/conf-2/streams"));
    return true;
}

// ---------------------------------------------------------------------------
// Conference Tokens (top-level resource)
// ---------------------------------------------------------------------------

TEST(rest_mock_video_conference_tokens_get_returns_single_token) {
    auto client = mocktest::make_client();
    auto body = client.video().conference_tokens.get("tok-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/conference_tokens/tok-1"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_video_conference_tokens_reset_posts_to_reset_subpath) {
    auto client = mocktest::make_client();
    auto body = client.video().conference_tokens.reset("tok-2");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/video/conference_tokens/tok-2/reset"));
    // reset is a no-body POST -- empty json::object() becomes "{}" or similar.
    // We accept null/empty-object/empty-string as "no body".
    bool empty_body = j.body.is_null()
                      || (j.body.is_object() && j.body.empty())
                      || (j.body.is_string() && j.body.get<std::string>().empty());
    ASSERT_TRUE(empty_body);
    return true;
}

// ---------------------------------------------------------------------------
// Streams (top-level)
// ---------------------------------------------------------------------------

TEST(rest_mock_video_streams_get_returns_stream_resource) {
    auto client = mocktest::make_client();
    auto body = client.video().streams.get("stream-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/video/streams/stream-1"));
    return true;
}

TEST(rest_mock_video_streams_update_uses_put_with_kwargs) {
    auto client = mocktest::make_client();
    auto body = client.video().streams.update(
        "stream-2", {{"url", "rtmp://example.com/new"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PUT"));
    ASSERT_EQ(j.path, std::string("/api/video/streams/stream-2"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("url", std::string()), std::string("rtmp://example.com/new"));
    return true;
}

TEST(rest_mock_video_streams_delete) {
    auto client = mocktest::make_client();
    auto body = client.video().streams.delete_("stream-3");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/video/streams/stream-3"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}
