#include "matching_engine/replay.hpp"

#include <iostream>
#include <sstream>

namespace {

int failures = 0;

void check(const bool condition, const char* const expression, const char* const test_name) {
    if (!condition) {
        std::cerr << test_name << ": check failed: " << expression << '\n';
        ++failures;
    }
}

#define CHECK(test_name, expression) check((expression), #expression, test_name)

constexpr const char* kValidReplay =
    "event_type,order_id,side,order_type,price_ticks,quantity\n"
    "NEW,1,SELL,LIMIT,100,40\n"
    "NEW,2,SELL,LIMIT,101,50\n"
    "NEW,3,BUY,LIMIT,101,60\n"
    "NEW,4,BUY,MARKET,0,50\n"
    "NEW,5,BUY,LIMIT,99,25\n"
    "CANCEL,5,,,,\n"
    "NEW,6,BUY,LIMIT,98,12\n";

bool same_snapshot(
    const matching_engine::OrderBookSnapshot& left,
    const matching_engine::OrderBookSnapshot& right) {
    return left.bids.size() == right.bids.size() && left.asks.size() == right.asks.size()
        && (left.bids.empty() || (left.bids[0].price == right.bids[0].price
            && left.bids[0].total_quantity == right.bids[0].total_quantity
            && left.bids[0].order_ids == right.bids[0].order_ids));
}

void valid_replay_test() {
    constexpr const char* test_name = "valid replay";
    std::istringstream input{kValidReplay};
    matching_engine::MatchingEngine engine{16};
    const auto result = matching_engine::replay_csv(input, engine);

    CHECK(test_name, result.errors.empty());
    CHECK(test_name, result.summary.parsed_events == 7U);
    CHECK(test_name, result.summary.new_events == 6U);
    CHECK(test_name, result.summary.cancel_events == 1U);
    CHECK(test_name, result.summary.accepted_orders == 6U);
    CHECK(test_name, result.summary.trade_count == 3U);
    CHECK(test_name, result.reports.size() == 6U);
    CHECK(test_name, result.trades.size() == 3U);
    CHECK(test_name, result.final_snapshot.bids.size() == 1U);
    CHECK(test_name, result.final_snapshot.bids[0].price == 98);
    CHECK(test_name, result.final_snapshot.bids[0].total_quantity == 12U);
    CHECK(test_name, result.final_snapshot.bids[0].order_ids[0] == 6U);
    CHECK(test_name, result.final_snapshot.asks.empty());
    CHECK(test_name, engine.order_book().validate_invariants());
}

void malformed_rows_test() {
    constexpr const char* test_name = "malformed rows";
    std::istringstream input{
        "event_type,order_id,side,order_type,price_ticks,quantity\n"
        "NEW,1,BUY,LIMIT,100,10\n"
        "INVALID,2,BUY,LIMIT,100,10\n"
        "NEW,three,BUY,LIMIT,100,10\n"
        "CANCEL,1,BUY,,,\n"
        "NEW,2,SELL,LIMIT,101\n"};
    matching_engine::MatchingEngine engine{4};
    const auto result = matching_engine::replay_csv(input, engine);

    CHECK(test_name, result.summary.parsed_events == 1U);
    CHECK(test_name, result.summary.malformed_rows == 4U);
    CHECK(test_name, result.errors.size() == 4U);
    CHECK(test_name, result.final_snapshot.bids.size() == 1U);
    CHECK(test_name, result.final_snapshot.bids[0].order_ids[0] == 1U);
    CHECK(test_name, engine.order_book().validate_invariants());
}

void deterministic_replay_test() {
    constexpr const char* test_name = "deterministic replay";
    std::istringstream first_input{kValidReplay};
    std::istringstream second_input{kValidReplay};
    matching_engine::MatchingEngine first_engine{16};
    matching_engine::MatchingEngine second_engine{16};
    const auto first = matching_engine::replay_csv(first_input, first_engine);
    const auto second = matching_engine::replay_csv(second_input, second_engine);

    CHECK(test_name, first.summary.parsed_events == second.summary.parsed_events);
    CHECK(test_name, first.summary.trade_count == second.summary.trade_count);
    CHECK(test_name, first.trades.size() == second.trades.size());
    CHECK(test_name, same_snapshot(first.final_snapshot, second.final_snapshot));
    CHECK(test_name, first_engine.order_book().validate_invariants());
    CHECK(test_name, second_engine.order_book().validate_invariants());
}

}  // namespace

int main() {
    valid_replay_test();
    malformed_rows_test();
    deterministic_replay_test();

    if (failures != 0) {
        std::cerr << failures << " test check(s) failed\n";
        return 1;
    }

    std::cout << "All replay tests passed\n";
    return 0;
}
