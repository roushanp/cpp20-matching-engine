# C++20 Matching Engine

An educational, single-threaded limit order book and matching-engine simulator.
The project emphasizes deterministic price-time priority, explicit ownership,
fixed-capacity memory management, CMake/CTest verification, and portable C++20.

## Current progress

Completed: domain model, fixed-capacity `OrderPool`, `OrderBook`, and single-threaded `MatchingEngine`.

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
- Incoming orders execute against the best opposite price, then FIFO makers.
- Trades execute at the resting maker price; partial fills update remaining quantity.
- Unfilled limit orders rest; unfilled market quantity is discarded.

The engine does not implement networking or concurrency.

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
priority, cancellation, empty-level removal, duplicate IDs, pool reuse,
partial fills, multiple levels, maker pricing, market-order discard, and capacity rejection.
The system test also runs 1,000 fixed-seed randomized submit/cancel events against
an independent reference model and validates book invariants after every event.

## Replay CLI

The replay CSV format is:

```text
event_type,order_id,side,order_type,price_ticks,quantity
```

`NEW` records require `BUY` or `SELL`, `LIMIT` or `MARKET`, integer ticks, and
a positive quantity. `CANCEL` records use only `event_type` and `order_id`; the
remaining four fields must be empty. The parser intentionally accepts only plain,
unquoted CSV records with exactly six columns.

```sh
./build/matching_engine_cli replay data/sample_orders.csv
```

The optional `--capacity <count>` selects the fixed resting-order capacity.
Replay reports malformed rows without aborting valid later rows. Replaying the
same input produces the same execution reports, trades, summary, and final book.

Example output for the sample file:

```text
Replay summary
  parsed_events=7
  malformed_rows=0
  new_events=6
  cancel_events=1
  accepted_orders=6
  rejected_orders=0
  cancelled_orders=1
  unknown_cancels=0
  trades=3
...
Final order book
BIDS
  price_ticks=98 quantity=12 order_ids=6
ASKS
  <empty>
```

## Verification performed

On macOS with Apple Clang 21.0.0, the Debug and Release configurations built
successfully with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`.
CTest passed the `matching_engine.order_pool`, `matching_engine.order_book`,
`matching_engine.engine`, `matching_engine.system`, and `matching_engine.replay`
tests in both configurations.

## Next milestone

Add GitHub Actions CI for CMake configure, build, and CTest on Ubuntu Linux.
