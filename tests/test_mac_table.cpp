#include "l2sw/mac_table.hpp"

#include <gtest/gtest.h>

using namespace l2sw;
using namespace std::chrono_literals;
using LR = MacTable::LearnResult;

namespace {

MacAddress mac(const char* s) { return *MacAddress::fromString(s); }

// A fixed starting time; tests move forward from here explicitly.
const Clock::time_point t0{};

const MacAddress hostA = mac("00:00:00:00:00:0a");
const MacAddress hostB = mac("00:00:00:00:00:0b");

} // namespace

TEST(MacTable, LearnsAndLooksUpAddress) {
    MacTable table;
    EXPECT_EQ(table.learn(10, hostA, 1, t0), LR::Learned);
    EXPECT_EQ(table.lookup(10, hostA), PortId{1});
    EXPECT_EQ(table.size(), 1u);
}

TEST(MacTable, UnknownAddressReturnsNullopt) {
    MacTable table;
    EXPECT_FALSE(table.lookup(10, hostA).has_value());
}

TEST(MacTable, VlansAreIsolated) {
    MacTable table;
    table.learn(10, hostA, 1, t0);
    table.learn(20, hostA, 2, t0);  // same MAC, different VLAN

    EXPECT_EQ(table.lookup(10, hostA), PortId{1});
    EXPECT_EQ(table.lookup(20, hostA), PortId{2});
    EXPECT_FALSE(table.lookup(30, hostA).has_value());
    EXPECT_EQ(table.size(), 2u);
}

TEST(MacTable, RelearnOnSamePortRefreshes) {
    MacTable table;
    table.learn(10, hostA, 1, t0);
    EXPECT_EQ(table.learn(10, hostA, 1, t0 + 5s), LR::Refreshed);
    EXPECT_EQ(table.size(), 1u);
}

TEST(MacTable, HostMovingToNewPortUpdatesEntry) {
    MacTable table;
    table.learn(10, hostA, 1, t0);
    EXPECT_EQ(table.learn(10, hostA, 4, t0 + 1s), LR::Moved);
    EXPECT_EQ(table.lookup(10, hostA), PortId{4});
}

TEST(MacTable, NeverLearnsMulticastOrBroadcastSource) {
    MacTable table;
    EXPECT_EQ(table.learn(10, MacAddress::broadcast(), 1, t0), LR::Ignored);
    EXPECT_EQ(table.learn(10, mac("01:00:5e:00:00:01"), 1, t0), LR::Ignored);
    EXPECT_EQ(table.size(), 0u);
}

TEST(MacTable, AgesOutStaleEntries) {
    MacTable table(300s);
    table.learn(10, hostA, 1, t0);
    table.learn(10, hostB, 2, t0 + 200s);

    // At t0+299s nothing is old enough yet.
    EXPECT_EQ(table.ageOut(t0 + 299s), 0u);
    EXPECT_EQ(table.size(), 2u);

    // At t0+300s hostA (last seen t0) expires; hostB (t0+200s) stays.
    EXPECT_EQ(table.ageOut(t0 + 300s), 1u);
    EXPECT_FALSE(table.lookup(10, hostA).has_value());
    EXPECT_EQ(table.lookup(10, hostB), PortId{2});
}

TEST(MacTable, RefreshPreventsAging) {
    MacTable table(300s);
    table.learn(10, hostA, 1, t0);
    table.learn(10, hostA, 1, t0 + 250s);  // host still talking

    EXPECT_EQ(table.ageOut(t0 + 400s), 0u);
    EXPECT_EQ(table.lookup(10, hostA), PortId{1});
}

TEST(MacTable, RejectsNewEntriesWhenFull) {
    MacTable table(300s, /*capacity=*/1);
    EXPECT_EQ(table.learn(10, hostA, 1, t0), LR::Learned);
    EXPECT_EQ(table.learn(10, hostB, 2, t0), LR::TableFull);
    EXPECT_FALSE(table.lookup(10, hostB).has_value());

    // Existing entries can still be refreshed at capacity.
    EXPECT_EQ(table.learn(10, hostA, 1, t0 + 1s), LR::Refreshed);
}

TEST(MacTable, FlushPortRemovesOnlyThatPort) {
    MacTable table;
    table.learn(10, hostA, 1, t0);
    table.learn(20, hostB, 1, t0);
    table.learn(10, hostB, 2, t0);

    EXPECT_EQ(table.flushPort(1), 2u);
    EXPECT_EQ(table.size(), 1u);
    EXPECT_EQ(table.lookup(10, hostB), PortId{2});
}
