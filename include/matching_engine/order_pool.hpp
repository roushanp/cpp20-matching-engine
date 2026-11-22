#pragma once

#include "matching_engine/domain.hpp"

#include <cstddef>
#include <vector>

namespace matching_engine {

enum class AllocationStatus {
    Allocated,
    Exhausted,
    InvalidRequest,
};

struct AllocationResult {
    AllocationStatus status{AllocationStatus::Exhausted};
    OrderIndex index{kInvalidOrderIndex};

    [[nodiscard]] constexpr bool succeeded() const noexcept {
        return status == AllocationStatus::Allocated;
    }
};

enum class ReleaseStatus {
    Released,
    InvalidIndex,
    DoubleRelease,
};

class OrderPool {
public:
    explicit OrderPool(std::size_t capacity);

    [[nodiscard]] AllocationResult allocate(const OrderRequest& request) noexcept;
    [[nodiscard]] ReleaseStatus release(OrderIndex index) noexcept;

    [[nodiscard]] bool is_allocated(OrderIndex index) const noexcept;
    [[nodiscard]] OrderNode* try_get(OrderIndex index) noexcept;
    [[nodiscard]] const OrderNode* try_get(OrderIndex index) const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    std::vector<OrderNode> slots_;
    OrderIndex free_head_{kInvalidOrderIndex};
    std::size_t active_count_{};
};

}  // namespace matching_engine
