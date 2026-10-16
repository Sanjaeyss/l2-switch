#include "l2sw/ethernet.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace l2sw;

namespace {

MacAddress mac(const char* s) { return *MacAddress::fromString(s); }

// Builds a raw frame byte-by-byte, the way it would appear on the wire.
std::vector<std::uint8_t> buildFrame(const MacAddress& dst, const MacAddress& src,
                                     std::uint16_t etherType,
                                     const std::vector<std::uint8_t>& payload,
                                     std::optional<std::uint16_t> vlanTci = std::nullopt) {
    std::vector<std::uint8_t> f;
    f.insert(f.end(), dst.bytes.begin(), dst.bytes.end());
    f.insert(f.end(), src.bytes.begin(), src.bytes.end());
    if (vlanTci) {
        f.push_back(0x81);
        f.push_back(0x00);
        f.push_back(static_cast<std::uint8_t>(*vlanTci >> 8));
        f.push_back(static_cast<std::uint8_t>(*vlanTci & 0xFF));
    }
    f.push_back(static_cast<std::uint8_t>(etherType >> 8));
    f.push_back(static_cast<std::uint8_t>(etherType & 0xFF));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// MacAddress
// ---------------------------------------------------------------------------

TEST(MacAddress, ParsesAndFormatsRoundTrip) {
    auto m = MacAddress::fromString("00:1A:2b:3C:4d:5E");
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->bytes[0], 0x00);
    EXPECT_EQ(m->bytes[5], 0x5E);
    EXPECT_EQ(m->toString(), "00:1a:2b:3c:4d:5e");
}

TEST(MacAddress, RejectsMalformedStrings) {
    EXPECT_FALSE(MacAddress::fromString("").has_value());
    EXPECT_FALSE(MacAddress::fromString("00:11:22:33:44").has_value());
    EXPECT_FALSE(MacAddress::fromString("00-11-22-33-44-55").has_value());
    EXPECT_FALSE(MacAddress::fromString("zz:11:22:33:44:55").has_value());
    EXPECT_FALSE(MacAddress::fromString("00:11:22:33:44:555").has_value());
}

TEST(MacAddress, ClassifiesBroadcastAndMulticast) {
    EXPECT_TRUE(MacAddress::broadcast().isBroadcast());
    EXPECT_TRUE(MacAddress::broadcast().isMulticast());

    auto mcast = mac("01:00:5e:00:00:01"); // IPv4 multicast range
    EXPECT_TRUE(mcast.isMulticast());
    EXPECT_FALSE(mcast.isBroadcast());

    auto ucast = mac("00:11:22:33:44:55");
    EXPECT_FALSE(ucast.isMulticast());
    EXPECT_FALSE(ucast.isBroadcast());
}

// ---------------------------------------------------------------------------
// parseEthernet
// ---------------------------------------------------------------------------

TEST(ParseEthernet, ParsesUntaggedIPv4Frame) {
    const auto dst = mac("aa:aa:aa:aa:aa:aa");
    const auto src = mac("00:11:22:33:44:55");
    const std::vector<std::uint8_t> payload = {0x45, 0x00, 0x00, 0x14};
    const auto raw = buildFrame(dst, src, 0x0800, payload);

    auto r = parseEthernet(raw);
    ASSERT_TRUE(r.ok()) << toString(r.error);
    EXPECT_EQ(r.frame->dst, dst);
    EXPECT_EQ(r.frame->src, src);
    EXPECT_FALSE(r.frame->vlan.has_value());
    EXPECT_EQ(r.frame->etherType, 0x0800);
    ASSERT_EQ(r.frame->payload.size(), payload.size());
    EXPECT_EQ(r.frame->payload[0], 0x45);
}

TEST(ParseEthernet, ParsesVlanTaggedFrame) {
    // TCI = PCP 5 (101), DEI 1, VID 100 (0x064) -> 1011 0000 0110 0100 = 0xB064
    const auto raw = buildFrame(mac("aa:aa:aa:aa:aa:aa"), mac("00:11:22:33:44:55"),
                                0x0806, {0x00, 0x01}, 0xB064);

    auto r = parseEthernet(raw);
    ASSERT_TRUE(r.ok()) << toString(r.error);
    ASSERT_TRUE(r.frame->vlan.has_value());
    EXPECT_EQ(r.frame->vlan->pcp, 5);
    EXPECT_TRUE(r.frame->vlan->dei);
    EXPECT_EQ(r.frame->vlan->vid, 100);
    EXPECT_EQ(r.frame->etherType, 0x0806); // inner EtherType, not 0x8100
    EXPECT_EQ(r.frame->payload.size(), 2u);
}

TEST(ParseEthernet, AcceptsPriorityTaggedFrameWithVid0) {
    const auto raw = buildFrame(mac("aa:aa:aa:aa:aa:aa"), mac("00:11:22:33:44:55"),
                                0x0800, {}, 0x6000); // PCP 3, VID 0
    auto r = parseEthernet(raw);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.frame->vlan->vid, 0);
    EXPECT_EQ(r.frame->vlan->pcp, 3);
}

TEST(ParseEthernet, RejectsReservedVlanId) {
    const auto raw = buildFrame(mac("aa:aa:aa:aa:aa:aa"), mac("00:11:22:33:44:55"),
                                0x0800, {}, 0x0FFF);
    auto r = parseEthernet(raw);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, ParseError::ReservedVlanId);
}

TEST(ParseEthernet, RejectsFrameShorterThanHeader) {
    const std::vector<std::uint8_t> raw(13, 0x00);
    auto r = parseEthernet(raw);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, ParseError::TooShort);
}

TEST(ParseEthernet, RejectsTruncatedVlanTag) {
    // 14-byte header claiming a VLAN tag, but the tag bytes are missing.
    auto raw = buildFrame(mac("aa:aa:aa:aa:aa:aa"), mac("00:11:22:33:44:55"),
                          0x8100, {0x00});
    auto r = parseEthernet(raw);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, ParseError::TooShort);
}

TEST(ParseEthernet, HeaderOnlyFrameHasEmptyPayload) {
    const auto raw = buildFrame(mac("aa:aa:aa:aa:aa:aa"), mac("00:11:22:33:44:55"),
                                0x86DD, {});
    auto r = parseEthernet(raw);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.frame->payload.empty());
}
