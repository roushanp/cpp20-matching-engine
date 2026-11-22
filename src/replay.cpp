#include "matching_engine/replay.hpp"

#include <array>
#include <charconv>
#include <istream>
#include <optional>
#include <string_view>

namespace matching_engine {

namespace {

constexpr std::string_view kHeader = "event_type,order_id,side,order_type,price_ticks,quantity";

std::string_view trim(const std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1U);
}

std::optional<std::array<std::string_view, 6>> split_record(const std::string_view line) noexcept {
    std::array<std::string_view, 6> fields{};
    std::size_t field_index = 0;
    std::size_t start = 0;

    while (true) {
        const std::size_t delimiter = line.find(',', start);
        if (field_index == fields.size()) {
            return std::nullopt;
        }
        fields[field_index] = trim(line.substr(start, delimiter - start));
        ++field_index;
        if (delimiter == std::string_view::npos) {
            break;
        }
        start = delimiter + 1U;
    }

    return field_index == fields.size() ? std::optional{fields} : std::nullopt;
}

template <typename Integer>
std::optional<Integer> parse_integer(const std::string_view text) noexcept {
    Integer value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<Side> parse_side(const std::string_view text) noexcept {
    if (text == "BUY") {
        return Side::Buy;
    }
    if (text == "SELL") {
        return Side::Sell;
    }
    return std::nullopt;
}

std::optional<OrderType> parse_order_type(const std::string_view text) noexcept {
    if (text == "LIMIT") {
        return OrderType::Limit;
    }
    if (text == "MARKET") {
        return OrderType::Market;
    }
    return std::nullopt;
}

void add_error(ReplayResult& result, const std::size_t line_number, std::string message) {
    ++result.summary.malformed_rows;
    result.errors.push_back({.line_number = line_number, .message = std::move(message)});
}

}  // namespace

ReplayResult replay_csv(std::istream& input, MatchingEngine& engine) {
    ReplayResult result;
    std::string line;
    std::size_t line_number = 0;

    if (!std::getline(input, line)) {
        add_error(result, 1, "missing CSV header");
        result.final_snapshot = engine.order_book().snapshot();
        return result;
    }
    ++line_number;
    if (trim(line) != kHeader) {
        add_error(result, line_number, "invalid CSV header");
        result.final_snapshot = engine.order_book().snapshot();
        return result;
    }

    while (std::getline(input, line)) {
        ++line_number;
        if (trim(line).empty()) {
            continue;
        }

        const auto fields = split_record(line);
        if (!fields.has_value()) {
            add_error(result, line_number, "expected exactly six comma-separated fields");
            continue;
        }

        const auto id = parse_integer<OrderId>((*fields)[1]);
        if (!id.has_value() || *id == 0U) {
            add_error(result, line_number, "order_id must be a positive unsigned integer");
            continue;
        }

        if ((*fields)[0] == "CANCEL") {
            if (!(*fields)[2].empty() || !(*fields)[3].empty() || !(*fields)[4].empty() || !(*fields)[5].empty()) {
                add_error(result, line_number, "CANCEL requires empty side, order_type, price_ticks, and quantity fields");
                continue;
            }

            ++result.summary.parsed_events;
            ++result.summary.cancel_events;
            if (engine.cancel(*id) == RemoveOrderStatus::Removed) {
                ++result.summary.cancelled_orders;
            } else {
                ++result.summary.unknown_cancels;
            }
            continue;
        }

        if ((*fields)[0] != "NEW") {
            add_error(result, line_number, "event_type must be NEW or CANCEL");
            continue;
        }

        const auto side = parse_side((*fields)[2]);
        const auto order_type = parse_order_type((*fields)[3]);
        const auto price = parse_integer<Price>((*fields)[4]);
        const auto quantity = parse_integer<Quantity>((*fields)[5]);
        if (!side.has_value() || !order_type.has_value() || !price.has_value() || !quantity.has_value()) {
            add_error(result, line_number, "NEW contains an invalid side, order_type, price_ticks, or quantity");
            continue;
        }

        ++result.summary.parsed_events;
        ++result.summary.new_events;
        ExecutionReport report = engine.submit({
            .id = *id,
            .side = *side,
            .type = *order_type,
            .price = *price,
            .quantity = *quantity,
        });
        result.summary.trade_count += report.trades.size();
        if (report.status == ExecutionStatus::Accepted) {
            ++result.summary.accepted_orders;
        } else {
            ++result.summary.rejected_orders;
        }
        result.trades.insert(result.trades.end(), report.trades.begin(), report.trades.end());
        result.reports.push_back(std::move(report));
    }

    result.final_snapshot = engine.order_book().snapshot();
    return result;
}

}  // namespace matching_engine
