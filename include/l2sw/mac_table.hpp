// mac_table.hpp - MAC address learning table (a.k.a. FDB, forwarding database).
//
// How a learning switch uses this table:
//   1. A frame arrives on port P with source MAC S in VLAN V.
//      -> learn(V, S, P): "S lives behind port P".
//   2. The switch looks up the destination MAC D in VLAN V.
//      -> found:     forward the frame only out of that port.
//      -> not found: flood it out of every port in V except P.
//   3. Entries that haven't been refreshed within the aging time are
//      removed, so the table tracks hosts that move or disappear.
//
// Entries are keyed by (VLAN, MAC): the same MAC can legitimately appear in
// two VLANs behind different ports, and VLANs must not see each other's
// entries.
#pragma once

#include "l2sw/ethernet.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace l2sw {

using PortId = std::uint16_t;
using VlanId = std::uint16_t;
using Clock  = std::chrono::steady_clock;

class MacTable {
public:
    enum class LearnResult {
        Learned,    // new entry created
        Refreshed,  // existing entry, same port; timestamp updated
        Moved,      // existing entry, different port (host moved)
        Ignored,    // source was multicast/broadcast - never learned
        TableFull,  // at capacity; entry not created
    };

    // 300 s is the IEEE 802.1D default aging time.
    explicit MacTable(std::chrono::seconds agingTime = std::chrono::seconds{300},
                      std::size_t capacity = 4096);

    // `now` is passed in (rather than read from the clock inside) so tests
    // can control time deterministically.
    LearnResult learn(VlanId vlan, const MacAddress& mac, PortId port,
                      Clock::time_point now);

    std::optional<PortId> lookup(VlanId vlan, const MacAddress& mac) const;

    // Removes entries not seen within the aging time. Returns how many were removed.
    std::size_t ageOut(Clock::time_point now);

    // Removes every entry learned on `port` (e.g., when a link goes down).
    std::size_t flushPort(PortId port);

    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return capacity_; }
    std::chrono::seconds agingTime() const { return agingTime_; }

private:
    struct Key {
        VlanId vlan;
        MacAddress mac;
        bool operator==(const Key&) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    struct Entry {
        PortId port;
        Clock::time_point lastSeen;
    };

    std::chrono::seconds agingTime_;
    std::size_t capacity_;
    std::unordered_map<Key, Entry, KeyHash> entries_;
};

const char* toString(MacTable::LearnResult r);

} // namespace l2sw
