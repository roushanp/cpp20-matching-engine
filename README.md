# C++20 Matching Engine

An educational, single-threaded limit order book and matching-engine simulator.
The project emphasizes deterministic price-time priority, explicit ownership,
fixed-capacity memory management, CMake/CTest verification, and portable C++20.

## Current progress

Completed: domain model, fixed-capacity `OrderPool`, and a resting `OrderBook`.

- Prices use signed integer ticks (`Price`), never floating point.
- Order IDs and quantities use fixed-width integer types.
- Order requests reject zero IDs, zero quantities, non-positive limit prices,
  and market orders with a price.
- `OrderPool` allocates all `OrderNode` slots during construction and reuses
  released slots through an O(1) index-based free list.
- Pool exhaustion, invalid indices, and double release are explicit outcomes.
- Nodes use indices rather than owning pointers for future order-queue links.
- Bid and ask price levels use ordered maps; best bid/ask retrieval is O(1).
- Orders at the same price use an intrusive FIFO queue backed by pool indices.
- Order IDs map to pool indices for expected O(1) duplicate detection and cancellation.
- Empty price levels are removed when their final order is cancelled.

The matching engine itself has not yet been implemented; the book does not match
crossing orders in this milestone.

## Build and test

Requirements: CMake, Ninja, and a C++20 compiler.

```sh
cmake -S . -B build/Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
ctest --test-dir build/Debug --output-on-failure
```

For an optimized build, substitute `Release` for `Debug`.

The current tests cover pool allocation, release, slot reuse, exhaustion,
invalid slot handling, double release, invalid requests, best prices, FIFO
priority, cancellation, empty-level removal, duplicate IDs, and pool reuse.

## Verification performed

On macOS with Apple Clang 21.0.0, the Debug and Release configurations built
successfully with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`.
CTest passed the `matching_engine.order_pool` and `matching_engine.order_book`
tests in both configurations.

## Next milestone

Implement the single-threaded price-time matching path, with focused tests for
full fills, partial fills, price improvement, and FIFO priority.
