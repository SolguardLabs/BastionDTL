#pragma once

#include "common/amount.hpp"
#include "common/types.hpp"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace bastion {

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out);

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    void key(std::string_view key);
    void string(std::string_view value);
    void number(std::int64_t value);
    void number(std::uint64_t value);
    void boolean(bool value);
    void null();

    void field(std::string_view key, std::string_view value);
    void field(std::string_view key, const std::string& value);
    void field(std::string_view key, std::int64_t value);
    void field(std::string_view key, std::uint64_t value);
    void field(std::string_view key, bool value);
    void field(std::string_view key, Amount value);

    std::string finish_to_string() const;

private:
    struct Frame {
        bool first = true;
        bool object = false;
    };

    void before_value();
    void raw_string(std::string_view value);

    std::ostream& out_;
    std::vector<Frame> stack_;
    bool after_key_ = false;
};

std::string json_escape(std::string_view value);
std::string json_error(std::string_view error);

} // namespace bastion

