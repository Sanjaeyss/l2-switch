#include "l2sw/mac_table.hpp"

#include <functional>

namespace l2sw {

std::size_t MacTable::KeyHash::operator()(const Key& k) const noexcept {
    // Pack 48-bit MAC + 12-bit VLAN into one 64-bit integer, then hash it.
    std::uint64_t packed = 0;
    for (auto b : k.mac.bytes) {
        packed = (packed << 8) | b;
    }
    packed |= static_cast<std::uint64_t>(k.vlan) << 48;
    return std::hash<std::uint64_t>{}(packed);
}

MacTable::MacTable(std::chrono::seconds agingTime, std::size_t capacity)
    : agingTime_(agingTime), capacity_(capacity) {}

MacTable::LearnResult MacTable::learn(VlanId vlan, const MacAddress& mac,
                                      PortId port, Clock::time_point now) {
    // A valid source address is always unicast. Learning a multicast source
    // would make the switch send traffic for a whole group to one port.
    if (mac.isMulticast()) {
        return LearnResult::Ignored;
    }

    const Key key{vlan, mac};
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second.lastSeen = now;
        if (it->second.port == port) {
            return LearnResult::Refreshed;
        }
        it->second.port = port;
        return LearnResult::Moved;
    }

    if (entries_.size() >= capacity_) {
        return LearnResult::TableFull;
    }

    entries_.emplace(key, Entry{port, now});
    return LearnResult::Learned;
}

std::optional<PortId> MacTable::lookup(VlanId vlan, const MacAddress& mac) const {
    auto it = entries_.find(Key{vlan, mac});
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second.port;
}

std::size_t MacTable::ageOut(Clock::time_point now) {
    std::size_t removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.lastSeen >= agingTime_) {
            it = entries_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::size_t MacTable::flushPort(PortId port) {
    std::size_t removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.port == port) {
            it = entries_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

const char* toString(MacTable::LearnResult r) {
    switch (r) {
        case MacTable::LearnResult::Learned:   return "Learned";
        case MacTable::LearnResult::Refreshed: return "Refreshed";
        case MacTable::LearnResult::Moved:     return "Moved";
        case MacTable::LearnResult::Ignored:   return "Ignored";
        case MacTable::LearnResult::TableFull: return "TableFull";
    }
    return "Unknown";
}

} // namespace l2sw
