# L2 Learning Switch in C++ (In Progress)

A software implementation of an Ethernet learning switch in modern C++ (C++20),
built to understand how switching hardware processes frames at Layer 2.

[![CI](https://github.com/Sanjaeyss/l2-switch/actions/workflows/ci.yml/badge.svg)](https://github.com/Sanjaeyss/l2-switch/actions/workflows/ci.yml)

## Status

| Component | Status |
|---|---|
| Ethernet II frame parser | Done |
| 802.1Q VLAN tag parsing (PCP / DEI / VID) | Done |
| MAC learning table: per-VLAN entries, aging, port flush, capacity limit | Done |
| Unit tests (GoogleTest) + CI on Linux and macOS | Done |
| Switch forwarding engine: forward / flood / filter decisions | Next |
| VLAN-aware ports (access / trunk), tag push & pop | Planned |
| IPv4 routing with longest-prefix match (L3) | Planned |

## Roadmap

The project is built in small, tested steps. Each step lands with unit tests
and passes CI before the next one starts.

1. **Forwarding engine** (next). A `Switch` class takes `(ingress port, raw
   frame)` and returns a decision: forward to one port, flood to every port
   except the ingress port, or filter/drop. It filters when the destination is
   on the ingress port or the frame is malformed. Broadcast, multicast and
   unknown-unicast destinations are flooded.
2. **VLAN-aware ports.** Access and trunk port modes, 802.1Q tag push/pop on
   egress, and flooding restricted to ports that are members of the frame's VLAN.
3. **Stretch goals.** IPv4 routing with longest-prefix match (binary trie),
   then a SAI-style hardware abstraction layer, similar to how SONiC programs
   switch ASICs through the Switch Abstraction Interface.

## How a learning switch works

```
          frame arrives on port P
                    |
                    v
         +---------------------+
         | parse Ethernet      |  malformed? -> drop
         | (+ VLAN tag)        |
         +---------------------+
                    |
                    v
         +---------------------+
         | learn: src MAC is   |  MAC table: (VLAN, MAC) -> port
         | reachable via P     |
         +---------------------+
                    |
                    v
         +---------------------+
         | look up dst MAC     |
         +---------------------+
          |         |         |
     found on    found on   not found /
     port Q      port P     broadcast
          |         |         |
          v         v         v
     forward     filter     flood to all
     to Q only   (drop)     ports except P
```

Entries not refreshed within the aging time (default 300 s, per IEEE 802.1D)
are removed so the table follows hosts that move or go offline.

## Design notes

- **Zero-copy parsing.** `parseEthernet` takes a `std::span` over the raw
  bytes and returns a payload view into the same buffer, so no packet data is
  copied. Multi-byte fields are read as big-endian (network byte order).
- **Explicit errors.** Parsing returns a `ParseResult` with an error code
  (`TooShort`, `ReservedVlanId`) instead of throwing, which suits a
  per-packet fast path.
- **Per-VLAN learning.** The MAC table is keyed by `(VLAN, MAC)`, so the same
  host can appear in different VLANs and VLANs never see each other's entries.
  The key is packed into a 64-bit integer for hashing.
- **Testable time.** Functions that depend on time take `now` as a parameter,
  so aging is tested deterministically without sleeping.
- **Source-address safety.** Multicast and broadcast source MACs are never
  learned, since a valid source address is always unicast.

## Project layout

```
include/l2sw/   public headers (ethernet.hpp, mac_table.hpp)
src/            implementation
tests/          GoogleTest unit tests
.github/        CI workflow
```

## Build and test

Requirements: CMake 3.20+, a C++20 compiler (Apple Clang, Clang, or GCC), and git.

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

GoogleTest is downloaded automatically during the first configure.
