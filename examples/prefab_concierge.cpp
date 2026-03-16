// Copyright (c) 2025 SignalWire — MIT License
// Concierge prefab: venue information assistant.

#include <signalwire/prefabs/prefabs.hpp>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    prefabs::ConciergeAgent agent("hotel-concierge", "/concierge");

    agent.set_venue_name("Grand Hotel");
    agent.set_amenities({
        {{"name", "Pool"}, {"description", "Rooftop infinity pool with cabanas"}, {"hours", "6AM-10PM"}},
        {{"name", "Spa"}, {"description", "Full-service spa with sauna and steam room"}, {"hours", "8AM-8PM"}},
        {{"name", "Restaurant"}, {"description", "Fine dining with panoramic views"}, {"hours", "7AM-11PM"}},
        {{"name", "Fitness Center"}, {"description", "State-of-the-art gym"}, {"hours", "24/7"}}
    });
    agent.set_hours({
        {"check_in", "3:00 PM"}, {"check_out", "11:00 AM"},
        {"front_desk", "24/7"}, {"valet", "24/7"}
    });

    std::cout << "Concierge at http://0.0.0.0:3000/concierge\n";
    agent.run();
}
