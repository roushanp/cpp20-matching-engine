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

## Benchmark

Build and run a Release benchmark:

```sh
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release --target matching_engine_benchmark
./build/Release/matching_engine_benchmark --events 100000
./build/Release/matching_engine_benchmark --events 1000000
```

The fixed-seed synthetic flow mixes passive limit orders, market orders that
consume them, and cancellations. A warm-up run happens before timing. Throughput
uses one total wall-clock interval; sampled latency is measured in a separate
pass every `--sample-stride` operations, so latency clocks do not dominate the
throughput result.

The executable reports compiler/build information, workload configuration,
runtime, events per second, accepted/rejected/matched order counts, trade count,
and sampled p50/p95/p99/p99.9 latency in nanoseconds. It reports only the local
run's measurements.

Limitations: this is a single-process microbenchmark. CPU frequency scaling,
thermal state, background work, allocator behavior, cache state, clock granularity,
and the sampled-latency instrumentation all affect results. The workload is
synthetic and has a deliberately small active book; it is not representative of
production market data or a cross-system performance comparison.

## Verification performed

On macOS with Apple Clang 21.0.0, the Debug and Release configurations built
successfully with `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`.
CTest passed the `matching_engine.order_pool`, `matching_engine.order_book`,
`matching_engine.engine`, `matching_engine.system`, and `matching_engine.replay`
tests in both configurations.

## Continuous integration

GitHub Actions runs on pushes and pull requests targeting `main`, and can also
be started manually from the repository's **Actions** tab. The workflow builds
and tests GCC and Clang in Debug and Release configurations on Ubuntu Linux. A
separate GCC Debug job runs the full test suite with AddressSanitizer and
UndefinedBehaviorSanitizer.

To inspect a failure, open the failed workflow run in the repository's
**Actions** tab, select the failed matrix job, and expand the failed step. CTest
is invoked with `--output-on-failure`, so failing test output appears directly
in that step's log.

To reproduce a GCC sanitizer failure locally on Linux:

```sh
cmake -S . -B build/sanitizers -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sanitizers --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/sanitizers --output-on-failure
```

## Releases

Pushing a tag matching `v*` starts the release workflow. It builds a GCC Release
configuration on Ubuntu, runs the full CTest suite, and publishes a compressed
Linux x86_64 CLI archive containing `matching_engine_cli`, this README, and the
sample CSV. GitHub also supplies the tagged source-code archives on the release
page.

Create an annotated release tag only after the desired commit is on `main`:

```sh
git tag -a v1.0.0 -m "v1.0.0"
git push origin v1.0.0
```

Open the repository's **Actions** tab to follow the release job. Once it passes,
the generated notes, source archives, and
`matching_engine_cli-v1.0.0-linux-x86_64.tar.gz` asset are available from the
repository's **Releases** page. The workflow authenticates with the built-in
`GITHUB_TOKEN`; no personal access token is stored in the repository.

## Next milestone

Add GitHub Actions CI for CMake configure, build, and CTest on Ubuntu Linux.
