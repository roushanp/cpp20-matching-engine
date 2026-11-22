#include "matching_engine/matching_engine.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

#ifndef MATCHING_ENGINE_BENCHMARK_COMPILER_ID
#define MATCHING_ENGINE_BENCHMARK_COMPILER_ID "unconfigured"
#endif

#ifndef MATCHING_ENGINE_BENCHMARK_COMPILER_VERSION
#define MATCHING_ENGINE_BENCHMARK_COMPILER_VERSION "unconfigured"
#endif

#ifndef MATCHING_ENGINE_BENCHMARK_BUILD_TYPE
#define MATCHING_ENGINE_BENCHMARK_BUILD_TYPE "unconfigured"
#endif

namespace {

using matching_engine::ExecutionReport;
using matching_engine::ExecutionStatus;
using matching_engine::MatchingEngine;
using matching_engine::OrderId;
using matching_engine::OrderRequest;
using matching_engine::OrderType;
using matching_engine::Price;
using matching_engine::Quantity;
using matching_engine::RemoveOrderStatus;
using matching_engine::Side;

constexpr std::uint64_t kDefaultSeed = 0x5EEDC0DEULL;

struct BenchmarkOptions {
    std::size_t event_count{100'000};
    std::size_t warmup_events{10'000};
    std::size_t capacity{1'024};
    std::size_t sample_stride{1'000};
    std::uint64_t seed{kDefaultSeed};
};

enum class EventType { Submit, Cancel };

struct Event {
    EventType type{EventType::Submit};
    OrderRequest request{};
    OrderId cancel_id{};
};

struct OutcomeCounts {
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t matched_orders{};
    std::size_t trades{};
    std::size_t cancelled{};
    std::size_t unknown_cancels{};
};

template <typename Integer>
bool parse_integer(const std::string_view text, Integer& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

OrderRequest limit_order(
    const OrderId id,
    const Side side,
    const Price price,
    const Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Limit, .price = price, .quantity = quantity};
}

OrderRequest market_order(const OrderId id, const Side side, const Quantity quantity) {
    return {.id = id, .side = side, .type = OrderType::Market, .price = 0, .quantity = quantity};
}

std::vector<Event> make_workload(const std::size_t event_count, const std::uint64_t seed) {
    std::vector<Event> events;
    events.reserve(event_count);
    std::mt19937_64 random{seed};
    OrderId next_id = 1;

    while (events.size() < event_count) {
        const Quantity bid_quantity = 1U + (random() % 100U);
        const Quantity ask_quantity = 1U + (random() % 100U);
        const Price bid_price = 99 + static_cast<Price>(random() % 3U);
        const Price ask_price = 101 + static_cast<Price>(random() % 3U);
        const OrderId bid_id = next_id++;
        const OrderId ask_id = next_id++;
        const OrderId sell_market_id = next_id++;
        const OrderId buy_market_id = next_id++;
        const OrderId cancellable_id = next_id++;

        const Event cycle[] = {
            {.type = EventType::Submit, .request = limit_order(bid_id, Side::Buy, bid_price, bid_quantity)},
            {.type = EventType::Submit, .request = limit_order(ask_id, Side::Sell, ask_price, ask_quantity)},
            {.type = EventType::Submit, .request = market_order(sell_market_id, Side::Sell, bid_quantity)},
            {.type = EventType::Submit, .request = market_order(buy_market_id, Side::Buy, ask_quantity)},
            {.type = EventType::Submit, .request = limit_order(cancellable_id, Side::Buy, 95, 1U + (random() % 100U))},
            {.type = EventType::Cancel, .cancel_id = cancellable_id},
        };

        for (const Event& event : cycle) {
            if (events.size() == event_count) {
                break;
            }
            events.push_back(event);
        }
    }

    return events;
}

void execute_event(MatchingEngine& engine, const Event& event, OutcomeCounts& counts) {
    if (event.type == EventType::Cancel) {
        if (engine.cancel(event.cancel_id) == RemoveOrderStatus::Removed) {
            ++counts.cancelled;
        } else {
            ++counts.unknown_cancels;
        }
        return;
    }

    const ExecutionReport report = engine.submit(event.request);
    if (report.status == ExecutionStatus::Accepted) {
        ++counts.accepted;
    } else {
        ++counts.rejected;
    }
    if (report.executed_quantity != 0U) {
        ++counts.matched_orders;
    }
    counts.trades += report.trades.size();
}

std::vector<std::int64_t> sample_latencies(
    const std::vector<Event>& events,
    const BenchmarkOptions& options) {
    MatchingEngine engine{options.capacity};
    OutcomeCounts ignored_counts;
    std::vector<std::int64_t> samples;
    samples.reserve((events.size() + options.sample_stride - 1U) / options.sample_stride);

    for (std::size_t index = 0; index < events.size(); ++index) {
        if (index % options.sample_stride == 0U) {
            const auto start = std::chrono::steady_clock::now();
            execute_event(engine, events[index], ignored_counts);
            const auto finish = std::chrono::steady_clock::now();
            samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
        } else {
            execute_event(engine, events[index], ignored_counts);
        }
    }

    std::sort(samples.begin(), samples.end());
    return samples;
}

std::int64_t percentile(const std::vector<std::int64_t>& sorted_samples, const double percentile_value) {
    if (sorted_samples.empty()) {
        return 0;
    }
    const double rank = percentile_value * static_cast<double>(sorted_samples.size());
    const std::size_t index = static_cast<std::size_t>(std::ceil(rank)) - 1U;
    return sorted_samples[std::min(index, sorted_samples.size() - 1U)];
}

bool parse_options(const int argc, char* argv[], BenchmarkOptions& options) {
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return false;
        }
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == "--events") {
            if (!parse_integer(value, options.event_count) || options.event_count == 0U) {
                return false;
            }
        } else if (option == "--warmup-events") {
            if (!parse_integer(value, options.warmup_events)) {
                return false;
            }
        } else if (option == "--capacity") {
            if (!parse_integer(value, options.capacity)) {
                return false;
            }
        } else if (option == "--sample-stride") {
            if (!parse_integer(value, options.sample_stride) || options.sample_stride == 0U) {
                return false;
            }
        } else if (option == "--seed") {
            if (!parse_integer(value, options.seed)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program
              << " [--events <count>] [--warmup-events <count>] [--capacity <count>]"
                 " [--sample-stride <count>] [--seed <integer>]\n";
}

}  // namespace

int main(const int argc, char* argv[]) {
    BenchmarkOptions options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }

    const std::vector<Event> workload = make_workload(options.event_count, options.seed);
    const std::size_t warmup_count = std::min(options.warmup_events, workload.size());
    MatchingEngine warmup_engine{options.capacity};
    OutcomeCounts warmup_counts;
    for (std::size_t index = 0; index < warmup_count; ++index) {
        execute_event(warmup_engine, workload[index], warmup_counts);
    }

    MatchingEngine throughput_engine{options.capacity};
    OutcomeCounts counts;
    const auto throughput_start = std::chrono::steady_clock::now();
    for (const Event& event : workload) {
        execute_event(throughput_engine, event, counts);
    }
    const auto throughput_finish = std::chrono::steady_clock::now();
    const auto elapsed = throughput_finish - throughput_start;
    const double elapsed_seconds = std::chrono::duration<double>(elapsed).count();
    const double throughput = static_cast<double>(workload.size()) / elapsed_seconds;

    const std::vector<std::int64_t> samples = sample_latencies(workload, options);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "MatchingEngine benchmark\n";
    std::cout << "  compiler=" << MATCHING_ENGINE_BENCHMARK_COMPILER_ID << ' '
              << MATCHING_ENGINE_BENCHMARK_COMPILER_VERSION << '\n';
    std::cout << "  build_type=" << MATCHING_ENGINE_BENCHMARK_BUILD_TYPE << '\n';
    std::cout << "  seed=" << options.seed << '\n';
    std::cout << "  events=" << workload.size() << " warmup_events=" << warmup_count
              << " capacity=" << options.capacity << " sample_stride=" << options.sample_stride << '\n';
    std::cout << "Throughput measurement\n";
    std::cout << "  total_runtime_s=" << elapsed_seconds << '\n';
    std::cout << "  events_per_second=" << throughput << '\n';
    std::cout << "  accepted_orders=" << counts.accepted << " rejected_orders=" << counts.rejected
              << " matched_orders=" << counts.matched_orders << " trades=" << counts.trades << '\n';
    std::cout << "  cancelled_orders=" << counts.cancelled << " unknown_cancels=" << counts.unknown_cancels << '\n';
    std::cout << "Sampled operation latency (separate pass; nanoseconds)\n";
    std::cout << "  samples=" << samples.size() << " p50=" << percentile(samples, 0.50)
              << " p95=" << percentile(samples, 0.95) << " p99=" << percentile(samples, 0.99)
              << " p99.9=" << percentile(samples, 0.999) << '\n';

    return 0;
}
