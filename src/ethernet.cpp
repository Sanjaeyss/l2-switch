#include "l2sw/ethernet.hpp"

#include <charconv>
#include <cstdio>

namespace l2sw {

namespace {

// Reads a big-endian 16-bit value starting at `p`.
std::uint16_t readU16BE(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

MacAddress readMac(const std::uint8_t* p) {
    MacAddress mac;
    for (std::size_t i = 0; i < mac.bytes.size(); ++i) {
        mac.bytes[i] = p[i];
    }
    return mac;
}

} // namespace

// ---------------------------------------------------------------------------
// MacAddress
// ---------------------------------------------------------------------------

std::optional<MacAddress> MacAddress::fromString(std::string_view text) {
    // Expect exactly "xx:xx:xx:xx:xx:xx" = 17 characters.
    if (text.size() != 17) {
        return std::nullopt;
    }
    MacAddress mac;
    for (std::size_t i = 0; i < 6; ++i) {
        const std::size_t pos = i * 3;
        if (i < 5 && text[pos + 2] != ':') {
            return std::nullopt;
        }
        unsigned value = 0;
        const char* begin = text.data() + pos;
        const char* end = begin + 2;
        auto [ptr, ec] = std::from_chars(begin, end, value, 16);
        if (ec != std::errc{} || ptr != end) {
            return std::nullopt;
        }
        mac.bytes[i] = static_cast<std::uint8_t>(value);
    }
    return mac;
}

MacAddress MacAddress::broadcast() {
    MacAddress mac;
    mac.bytes.fill(0xFF);
    return mac;
}

std::string MacAddress::toString() const {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return buf;
}

bool MacAddress::isBroadcast() const {
    for (auto b : bytes) {
        if (b != 0xFF) return false;
    }
    return true;
}

bool MacAddress::isMulticast() const {
    return (bytes[0] & 0x01) != 0;
}

// ---------------------------------------------------------------------------
// Frame parsing
// ---------------------------------------------------------------------------

ParseResult parseEthernet(std::span<const std::uint8_t> raw) {
    ParseResult result;

    if (raw.size() < kMacHeaderLen) {
        result.error = ParseError::TooShort;
        return result;
    }

    EthernetFrame frame;
    const std::uint8_t* p = raw.data();
    frame.dst = readMac(p);
    frame.src = readMac(p + 6);

    std::size_t offset = 12;
    std::uint16_t type = readU16BE(p + offset);
    offset += 2;

    // 802.1Q: TPID 0x8100 means a 4-byte VLAN tag sits here, followed by
    // the real EtherType.
    if (type == static_cast<std::uint16_t>(EtherType::VLAN)) {
        if (raw.size() < kMacHeaderLen + kVlanTagLen) {
            result.error = ParseError::TooShort;
            return result;
        }
        const std::uint16_t tci = readU16BE(p + offset);
        VlanTag tag;
        tag.pcp = static_cast<std::uint8_t>(tci >> 13);   // top 3 bits
        tag.dei = ((tci >> 12) & 0x1) != 0;               // next bit
        tag.vid = static_cast<std::uint16_t>(tci & 0x0FFF); // low 12 bits
        if (tag.vid == 0x0FFF) {
            result.error = ParseError::ReservedVlanId;
            return result;
        }
        frame.vlan = tag;
        offset += 2;

        type = readU16BE(p + offset);
        offset += 2;
    }

    frame.etherType = type;
    frame.payload = raw.subspan(offset);
    result.frame = frame;
    return result;
}

const char* toString(ParseError err) {
    switch (err) {
        case ParseError::None:           return "None";
        case ParseError::TooShort:       return "TooShort";
        case ParseError::ReservedVlanId: return "ReservedVlanId";
    }
    return "Unknown";
}

} // namespace l2sw
