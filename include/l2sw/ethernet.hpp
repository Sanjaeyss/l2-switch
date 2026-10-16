// ethernet.hpp - Ethernet II / 802.1Q frame parsing.
//
// Wire layout of an Ethernet frame as handed to software (the NIC has
// already stripped the preamble and the 4-byte FCS/CRC):
//
//   Untagged:
//   +----------+----------+-----------+------------------+
//   | Dst MAC  | Src MAC  | EtherType | Payload ...      |
//   | 6 bytes  | 6 bytes  | 2 bytes   | 46-1500 bytes    |
//   +----------+----------+-----------+------------------+
//
//   802.1Q VLAN-tagged (a 4-byte tag is inserted after Src MAC):
//   +----------+----------+--------+-----+-----------+------------+
//   | Dst MAC  | Src MAC  | 0x8100 | TCI | EtherType | Payload... |
//   +----------+----------+--------+-----+-----------+------------+
//                         |<-- 802.1Q tag -->|
//
//   TCI (Tag Control Information, 16 bits):
//     PCP (3 bits, priority) | DEI (1 bit, drop eligible) | VID (12 bits)
//
// All multi-byte fields are big-endian ("network byte order").
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace l2sw {

// ---------------------------------------------------------------------------
// MacAddress: a 48-bit hardware address.
// ---------------------------------------------------------------------------
struct MacAddress {
    std::array<std::uint8_t, 6> bytes{};

    // Parses "aa:bb:cc:dd:ee:ff" (case-insensitive). Returns nullopt if malformed.
    static std::optional<MacAddress> fromString(std::string_view text);
    static MacAddress broadcast();

    std::string toString() const;

    // ff:ff:ff:ff:ff:ff - delivered to every host on the segment.
    bool isBroadcast() const;
    // Lowest bit of the first byte set => group (multicast) address.
    // Broadcast is a special case of multicast.
    bool isMulticast() const;

    auto operator<=>(const MacAddress&) const = default;
};

enum class EtherType : std::uint16_t {
    IPv4 = 0x0800,
    ARP  = 0x0806,
    VLAN = 0x8100,  // 802.1Q tag protocol identifier (TPID)
    IPv6 = 0x86DD,
};

struct VlanTag {
    std::uint8_t  pcp = 0;     // Priority Code Point (0-7)
    bool          dei = false; // Drop Eligible Indicator
    std::uint16_t vid = 0;     // VLAN ID (0-4094; 0 = priority-tagged only)
};

struct EthernetFrame {
    MacAddress dst;
    MacAddress src;
    std::optional<VlanTag> vlan;      // present only for 802.1Q-tagged frames
    std::uint16_t etherType = 0;      // inner EtherType (after any VLAN tag)
    std::span<const std::uint8_t> payload; // view into the caller's buffer
};

enum class ParseError {
    None,
    TooShort,          // fewer bytes than the headers require
    ReservedVlanId,    // VID 0xFFF is reserved by IEEE 802.1Q
};

struct ParseResult {
    std::optional<EthernetFrame> frame;
    ParseError error = ParseError::None;

    bool ok() const { return frame.has_value(); }
};

inline constexpr std::size_t kMacHeaderLen  = 14; // dst + src + EtherType
inline constexpr std::size_t kVlanTagLen    = 4;  // TPID + TCI

// Parses a raw frame. Does not copy: the returned payload points into `raw`,
// so `raw` must outlive the result.
ParseResult parseEthernet(std::span<const std::uint8_t> raw);

const char* toString(ParseError err);

} // namespace l2sw
