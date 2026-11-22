#pragma once

#include "matching_engine/matching_engine.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace matching_engine {

struct ReplayError {
    std::size_t line_number{};
    std::string message{};
};

struct ReplaySummary {
    std::size_t parsed_events{};
    std::size_t malformed_rows{};
    std::size_t new_events{};
    std::size_t cancel_events{};
    std::size_t accepted_orders{};
    std::size_t rejected_orders{};
    std::size_t cancelled_orders{};
    std::size_t unknown_cancels{};
    std::size_t trade_count{};
};

struct ReplayResult {
    ReplaySummary summary{};
    std::vector<ExecutionReport> reports{};
    std::vector<Trade> trades{};
    std::vector<ReplayError> errors{};
    OrderBookSnapshot final_snapshot{};
};

[[nodiscard]] ReplayResult replay_csv(std::istream& input, MatchingEngine& engine);

}  // namespace matching_engine
