// RELAY Call split tests — deeper call lifecycle, properties, action registry
#include "signalwire/relay/call.hpp"
#include "signalwire/relay/constants.hpp"
using namespace signalwire::relay;
using json = nlohmann::json;

TEST(relay_call_default_state_empty) {
    Call call;
    ASSERT_EQ(call.state(), "");
    ASSERT_EQ(call.direction(), "");
    ASSERT_EQ(call.from(), "");
    ASSERT_EQ(call.to(), "");
    ASSERT_EQ(call.tag(), "");
    return true;
}

TEST(relay_call_initial_state_is_created) {
    Call call("c-1", "n-1");
    ASSERT_EQ(call.state(), "created");
    return true;
}

TEST(relay_call_full_lifecycle) {
    Call call("c-2", "n-2");
    ASSERT_EQ(call.state(), "created");
    ASSERT_FALSE(call.is_answered());
    ASSERT_FALSE(call.is_ended());

    call.update_state("ringing");
    ASSERT_EQ(call.state(), "ringing");

    call.update_state("answered");
    ASSERT_TRUE(call.is_answered());

    call.update_state("ending");
    ASSERT_FALSE(call.is_answered());

    call.update_state("ended");
    ASSERT_TRUE(call.is_ended());
    return true;
}

TEST(relay_call_set_all_properties) {
    Call call("c-3", "n-3");
    call.set_direction("outbound");
    call.set_from("+15551111111");
    call.set_to("+15552222222");
    call.set_tag("tag-123");

    ASSERT_EQ(call.direction(), "outbound");
    ASSERT_EQ(call.from(), "+15551111111");
    ASSERT_EQ(call.to(), "+15552222222");
    ASSERT_EQ(call.tag(), "tag-123");
    return true;
}

TEST(relay_call_action_register_find_unregister) {
    Call call("c-4", "n-4");
    Action a1("ctl-1");
    Action a2("ctl-2");

    call.register_action("ctl-1", &a1);
    call.register_action("ctl-2", &a2);

    ASSERT_TRUE(call.find_action("ctl-1") != nullptr);
    ASSERT_TRUE(call.find_action("ctl-2") != nullptr);
    ASSERT_TRUE(call.find_action("ctl-3") == nullptr);

    call.unregister_action("ctl-1");
    ASSERT_TRUE(call.find_action("ctl-1") == nullptr);
    ASSERT_TRUE(call.find_action("ctl-2") != nullptr);
    return true;
}

TEST(relay_call_ended_resolves_all_actions) {
    Call call("c-5", "n-5");
    Action a1("ctl-10");
    Action a2("ctl-20");
    call.register_action("ctl-10", &a1);
    call.register_action("ctl-20", &a2);

    ASSERT_FALSE(a1.completed());
    ASSERT_FALSE(a2.completed());

    call.update_state("ended");

    ASSERT_TRUE(a1.completed());
    ASSERT_TRUE(a2.completed());
    return true;
}

TEST(relay_call_event_handler_receives_event) {
    Call call("c-6", "n-6");
    std::string captured_state;
    call.on_event([&](const CallEvent& ev) {
        captured_state = ev.call_state;
    });

    CallEvent ev;
    ev.event_type = "calling.call.state";
    ev.call_id = "c-6";
    ev.call_state = "answered";
    call.dispatch_event(ev);

    ASSERT_EQ(captured_state, "answered");
    return true;
}

TEST(relay_call_all_methods_return_completed_without_client) {
    Call call("c-7", "n-7");
    ASSERT_TRUE(call.answer().completed());
    ASSERT_TRUE(call.hangup().completed());
    ASSERT_TRUE(call.hold().completed());
    ASSERT_TRUE(call.unhold().completed());
    ASSERT_TRUE(call.disconnect().completed());
    return true;
}

TEST(relay_call_wait_for_ended_threaded) {
    Call call("c-8", "n-8");
    std::thread t([&call]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        call.update_state("ended");
    });
    bool ok = call.wait_for_ended(5000);
    ASSERT_TRUE(ok);
    t.join();
    return true;
}

// CPP-1 regression: the action registry must OWN its entries, not hold a raw
// pointer to a caller-scoped Action. Before the fix, execute_action() stored
// `&action` (a stack local) and returned the Action by value; the registered
// pointer dangled the moment execute_action returned, so a later
// resolve_all_actions / find_action dereferenced freed memory (UAF). Here we
// register an Action that then leaves scope BEFORE resolve_all_actions runs —
// with a raw-pointer registry this is a read of a destroyed object (ASAN
// heap/stack-use-after-scope); with an owning registry it is safe and the
// registered copy resolves cleanly.
TEST(relay_call_registry_owns_action_no_uaf) {
    Call call("c-uaf", "n-uaf");
    {
        // Mimic execute_action: build a local Action, hand a pointer to the
        // registry, then let the local go out of scope.
        Action local("ctl-uaf");
        call.register_action("ctl-uaf", &local);
    }  // `local` destroyed here — a raw-pointer registry now dangles.

    // Touching the entry must not read freed memory. With the owning registry
    // the stored copy is alive and resolvable.
    call.resolve_all_actions("finished");
    // After resolve_all, the entry is cleared.
    ASSERT_TRUE(call.find_action("ctl-uaf") == nullptr);
    return true;
}

// CPP-2 regression: registering handlers (user thread) concurrently with
// dispatching events (reader thread) must not race on the handler vector.
// Before the fix, on_event() push_back'd without a lock while dispatch_event()
// iterated by reference — a concurrent push_back could reallocate the backing
// buffer and invalidate the loop iterator (UB / crash). This test drives both
// concurrently; it is data-race-clean only with handlers_mutex in place (the
// nightly TSAN lane asserts the absence of the race directly).
TEST(relay_call_concurrent_on_event_dispatch_no_race) {
    Call call("c-race", "n-race");
    std::atomic<bool> stop{false};
    std::atomic<int> dispatched{0};

    std::thread registrar([&]() {
        for (int i = 0; i < 500 && !stop.load(); ++i) {
            call.on_event([&](const CallEvent&) { dispatched.fetch_add(1); });
            std::this_thread::yield();
        }
    });
    std::thread dispatcher([&]() {
        CallEvent ev;
        ev.event_type = "calling.call.state";
        ev.call_id = "c-race";
        ev.call_state = "answered";
        for (int i = 0; i < 500 && !stop.load(); ++i) {
            call.dispatch_event(ev);
            std::this_thread::yield();
        }
    });

    registrar.join();
    dispatcher.join();
    stop.store(true);

    // Content-shaped assertion: after the concurrent churn, all 500 registered
    // handlers must be present and functional. Dispatch ONE more event single-
    // threaded and assert exactly 500 handlers fire on it — a race that
    // corrupted the vector (lost/duplicated entries, or a torn buffer) would
    // yield a wrong count or crash here, so this fails iff the fix is absent.
    int before = dispatched.load();
    CallEvent probe;
    probe.event_type = "calling.call.state";
    probe.call_id = "c-race";
    probe.call_state = "ended";
    call.dispatch_event(probe);
    ASSERT_EQ(dispatched.load() - before, 500);
    return true;
}

// CPP-1 regression, shared-state leg: the registered copy and the caller's
// copy must share SharedState, so resolving via the registry resolves the
// Action the caller still holds — matching execute_action returning the same
// logical Action it registered.
TEST(relay_call_registry_copy_shares_state) {
    Call call("c-share", "n-share");
    Action caller_copy("ctl-share");
    call.register_action("ctl-share", &caller_copy);
    ASSERT_FALSE(caller_copy.completed());

    // Ending the call resolves every registered action; because the registry
    // holds a copy that SHARES SharedState with caller_copy, the caller's
    // Action observes the resolution.
    call.update_state("ended");
    ASSERT_TRUE(caller_copy.completed());
    return true;
}
