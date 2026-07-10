#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <string>

namespace bastion {

class Amount {
public:
    Amount() = default;
    explicit Amount(std::int64_t units);

    static Amount zero();
    static Amount from_units(std::int64_t units);

    std::int64_t units() const;
    bool is_zero() const;
    bool positive() const;
    std::string str() const;

    Amount checked_add(Amount other) const;
    Amount checked_sub(Amount other) const;
    Amount saturating_sub(Amount other) const;
    Amount checked_mul_bps(std::uint32_t bps) const;
    Amount min(Amount other) const;
    Amount max(Amount other) const;

    friend bool operator==(Amount left, Amount right) = default;
    friend bool operator<(Amount left, Amount right);
    friend bool operator<=(Amount left, Amount right);
    friend bool operator>(Amount left, Amount right);
    friend bool operator>=(Amount left, Amount right);

private:
    std::int64_t units_ = 0;
};

class BasisPoints {
public:
    BasisPoints() = default;
    explicit BasisPoints(std::uint32_t value);

    static BasisPoints zero();
    static BasisPoints percent(std::uint32_t percent);

    std::uint32_t value() const;
    Amount apply(Amount amount) const;
    std::string str() const;

    friend bool operator==(BasisPoints left, BasisPoints right) = default;
    friend bool operator<(BasisPoints left, BasisPoints right);
    friend bool operator<=(BasisPoints left, BasisPoints right);
    friend bool operator>(BasisPoints left, BasisPoints right);
    friend bool operator>=(BasisPoints left, BasisPoints right);

private:
    std::uint32_t value_ = 0;
};

struct Balance {
    Amount available;
    Amount reserved;
    Amount locked;

    Amount total() const;
    bool empty() const;
    std::string describe() const;
};

} // namespace bastion

