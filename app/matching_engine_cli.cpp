#include "matching_engine/replay.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

constexpr std::size_t kDefaultCapacity = 1'024;

std::string_view execution_status_name(const matching_engine::ExecutionStatus status) {
    using matching_engine::ExecutionStatus;
    switch (status) {
    case ExecutionStatus::Accepted:
        return "ACCEPTED";
    case ExecutionStatus::RejectedInvalidRequest:
        return "REJECTED_INVALID_REQUEST";
    case ExecutionStatus::RejectedDuplicateOrderId:
        return "REJECTED_DUPLICATE_ORDER_ID";
    case ExecutionStatus::RejectedPoolExhausted:
        return "REJECTED_POOL_EXHAUSTED";
    case ExecutionStatus::RejectedLevelQuantityOverflow:
        return "REJECTED_LEVEL_QUANTITY_OVERFLOW";
    case ExecutionStatus::RejectedLevelOrderCountOverflow:
        return "REJECTED_LEVEL_ORDER_COUNT_OVERFLOW";
    }
    return "UNKNOWN";
}

bool parse_capacity(const std::string_view text, std::size_t& capacity) {
    std::size_t parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return false;
    }
    capacity = parsed;
    return true;
}

void print_snapshot(const matching_engine::OrderBookSnapshot& snapshot) {
    const auto print_side = [](const std::string_view name, const auto& levels) {
        std::cout << name << '\n';
        if (levels.empty()) {
            std::cout << "  <empty>\n";
            return;
        }
        for (const auto& level : levels) {
            std::cout << "  price_ticks=" << level.price << " quantity=" << level.total_quantity << " order_ids=";
            for (std::size_t index = 0; index < level.order_ids.size(); ++index) {
                if (index != 0U) {
                    std::cout << ',';
                }
                std::cout << level.order_ids[index];
            }
            std::cout << '\n';
        }
    };

    std::cout << "Final order book\n";
    print_side("BIDS", snapshot.bids);
    print_side("ASKS", snapshot.asks);
}

}  // namespace

int main(const int argc, char* argv[]) {
    if (argc < 3 || argc > 5 || std::string_view{argv[1]} != "replay") {
        std::cerr << "Usage: " << argv[0] << " replay <csv-path> [--capacity <count>]\n";
        return 2;
    }

    std::size_t capacity = kDefaultCapacity;
    if (argc == 5 && (std::string_view{argv[3]} != "--capacity" || !parse_capacity(argv[4], capacity))) {
        std::cerr << "--capacity must be a non-negative integer\n";
        return 2;
    }
    if (argc == 4) {
        std::cerr << "Usage: " << argv[0] << " replay <csv-path> [--capacity <count>]\n";
        return 2;
    }

    std::ifstream input{argv[2]};
    if (!input) {
        std::cerr << "Unable to open CSV file: " << argv[2] << '\n';
        return 1;
    }

    matching_engine::MatchingEngine engine{capacity};
    const matching_engine::ReplayResult result = matching_engine::replay_csv(input, engine);

    std::cout << "Replay summary\n";
    std::cout << "  parsed_events=" << result.summary.parsed_events << '\n';
    std::cout << "  malformed_rows=" << result.summary.malformed_rows << '\n';
    std::cout << "  new_events=" << result.summary.new_events << '\n';
    std::cout << "  cancel_events=" << result.summary.cancel_events << '\n';
    std::cout << "  accepted_orders=" << result.summary.accepted_orders << '\n';
    std::cout << "  rejected_orders=" << result.summary.rejected_orders << '\n';
    std::cout << "  cancelled_orders=" << result.summary.cancelled_orders << '\n';
    std::cout << "  unknown_cancels=" << result.summary.unknown_cancels << '\n';
    std::cout << "  trades=" << result.summary.trade_count << '\n';
    for (const auto& error : result.errors) {
        std::cout << "Malformed row " << error.line_number << ": " << error.message << '\n';
    }
    for (const auto& report : result.reports) {
        std::cout << "Order " << report.order_id << ' ' << execution_status_name(report.status)
                  << " executed=" << report.executed_quantity << " remaining=" << report.remaining_quantity
                  << " rested=" << (report.rested ? "true" : "false") << '\n';
    }
    for (const auto& trade : result.trades) {
        std::cout << "Trade maker=" << trade.maker_order_id << " taker=" << trade.taker_order_id
                  << " price_ticks=" << trade.price << " quantity=" << trade.quantity << '\n';
    }
    print_snapshot(result.final_snapshot);

    return result.errors.empty() ? 0 : 1;
}
