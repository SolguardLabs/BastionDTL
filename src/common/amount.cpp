#include "common/amount.hpp"

#include <limits>
#include <sstream>

namespace bastion {

namespace {

constexpr std::int64_t kMaxUnits = std::numeric_limits<std::int64_t>::max() / 4;
constexpr std::uint32_t kMaxBps = 10000;

} // namespace

Amount::Amount(std::int64_t units) : units_(units) {
    if (units < 0) {
        fail("amount cannot be negative");
    }
    if (units > kMaxUnits) {
        fail("amount exceeds protocol range");
    }
}

Amount Amount::zero() {
    return Amount(0);
}

Amount Amount::from_units(std::int64_t units) {
    return Amount(units);
}

std::int64_t Amount::units() const {
    return units_;
}

bool Amount::is_zero() const {
    return units_ == 0;
}

bool Amount::positive() const {
    return units_ > 0;
}

std::string Amount::str() const {
    return std::to_string(units_);
}

Amount Amount::checked_add(Amount other) const {
    if (other.units_ > kMaxUnits - units_) {
        fail("amount addition overflow");
    }
    return Amount(units_ + other.units_);
}

Amount Amount::checked_sub(Amount other) const {
    if (other.units_ > units_) {
        fail("insufficient amount");
    }
    return Amount(units_ - other.units_);
}

Amount Amount::saturating_sub(Amount other) const {
    if (other.units_ >= units_) {
        return Amount::zero();
    }
    return Amount(units_ - other.units_);
}

Amount Amount::checked_mul_bps(std::uint32_t bps) const {
    if (bps > kMaxBps) {
        fail("basis points exceed 10000");
    }
    const auto value = static_cast<long double>(units_) * static_cast<long double>(bps);
    if (value > static_cast<long double>(kMaxUnits) * static_cast<long double>(kMaxBps)) {
        fail("amount bps multiplication overflow");
    }
    return Amount(static_cast<std::int64_t>((units_ * static_cast<std::int64_t>(bps)) / kMaxBps));
}

Amount Amount::min(Amount other) const {
    return units_ <= other.units_ ? *this : other;
}

Amount Amount::max(Amount other) const {
    return units_ >= other.units_ ? *this : other;
}

bool operator<(Amount left, Amount right) {
    return left.units_ < right.units_;
}

bool operator<=(Amount left, Amount right) {
    return left.units_ <= right.units_;
}

bool operator>(Amount left, Amount right) {
    return left.units_ > right.units_;
}

bool operator>=(Amount left, Amount right) {
    return left.units_ >= right.units_;
}

BasisPoints::BasisPoints(std::uint32_t value) : value_(value) {
    if (value > kMaxBps) {
        fail("basis points exceed 10000");
    }
}

BasisPoints BasisPoints::zero() {
    return BasisPoints(0);
}

BasisPoints BasisPoints::percent(std::uint32_t percent) {
    if (percent > 100) {
        fail("percent exceeds 100");
    }
    return BasisPoints(percent * 100);
}

std::uint32_t BasisPoints::value() const {
    return value_;
}

Amount BasisPoints::apply(Amount amount) const {
    return amount.checked_mul_bps(value_);
}

std::string BasisPoints::str() const {
    return std::to_string(value_);
}

bool operator<(BasisPoints left, BasisPoints right) {
    return left.value_ < right.value_;
}

bool operator<=(BasisPoints left, BasisPoints right) {
    return left.value_ <= right.value_;
}

bool operator>(BasisPoints left, BasisPoints right) {
    return left.value_ > right.value_;
}

bool operator>=(BasisPoints left, BasisPoints right) {
    return left.value_ >= right.value_;
}

Amount Balance::total() const {
    return available.checked_add(reserved).checked_add(locked);
}

bool Balance::empty() const {
    return available.is_zero() && reserved.is_zero() && locked.is_zero();
}

std::string Balance::describe() const {
    std::ostringstream out;
    out << "available=" << available.str();
    out << ",reserved=" << reserved.str();
    out << ",locked=" << locked.str();
    out << ",total=" << total().str();
    return out.str();
}

} // namespace bastion

