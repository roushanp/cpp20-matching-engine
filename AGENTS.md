# C++20 Matching Engine — Engineering Rules

## Project goals

- Use standard C++20 to build a deterministic, single-threaded limit order book and matching-engine simulator.
- Demonstrate low-latency systems engineering, correct price-time priority, a fixed-capacity memory pool, reproducible benchmarking, CMake/CTest, and GitHub Actions CI.
- Support Apple Clang on macOS and Clang/GCC on Ubuntu Linux.

## Scope and change discipline

- Work only within this repository. Inspect relevant existing files before changing them.
- Make small, milestone-scoped changes; do not generate the whole application at once.
- Do not overwrite working code without first explaining the reason and impact.
- Do not commit, push, add external dependencies, networking, or concurrency without explicit approval.
- Never use proprietary employer code or data. Use only synthetic order flow.

## C++ and matching-engine design

- Prefer clear, maintainable C++20 and the standard library over unnecessary abstractions or frameworks.
- Use RAII, explicit ownership, and stable lifetimes. Document non-owning pointers and their validity.
- Represent prices as integer ticks; never use floating point for price matching.
- Preserve deterministic price-time priority: best executable price first, then FIFO within a price level.
- Avoid dynamic allocation in the matching engine's critical path. Use a fixed-capacity pool where allocation is required, and define exhaustion behaviour.
- Do not add concurrency until the single-threaded book is correct and thoroughly tested.
- For each significant design decision, state algorithmic complexity and allocation/memory behaviour.

## Verification and portability

- Build with CMake and run relevant tests through CTest after each implementation milestone.
- Inspect compiler warnings; do not claim a build or test passed unless it was executed and succeeded.
- Never fabricate benchmark results. Record benchmark command, build configuration, workload seed, and environment with reported measurements.
- Keep code portable across macOS and Ubuntu; do not rely on platform-specific APIs unless isolated and justified.

## Required implementation-task report

Before editing, explain the proposed design and list files to modify. Implement only the requested milestone. Afterward, report:

1. Build and relevant test commands and their actual results.
2. Compiler warnings, if any.
3. Significant changes, complexity, and memory behaviour.
4. Edge cases and trade-offs.
5. Concepts to understand for interviews.
6. A concise suggested Git commit message.
