#include "common/json.hpp"

#include <iomanip>
#include <sstream>

namespace bastion {

JsonWriter::JsonWriter(std::ostream& out) : out_(out) {}

void JsonWriter::begin_object() {
    before_value();
    out_ << "{";
    stack_.push_back(Frame{true, true});
}

void JsonWriter::end_object() {
    if (stack_.empty() || !stack_.back().object) {
        fail("json object close without object");
    }
    out_ << "}";
    stack_.pop_back();
    after_key_ = false;
}

void JsonWriter::begin_array() {
    before_value();
    out_ << "[";
    stack_.push_back(Frame{true, false});
}

void JsonWriter::end_array() {
    if (stack_.empty() || stack_.back().object) {
        fail("json array close without array");
    }
    out_ << "]";
    stack_.pop_back();
    after_key_ = false;
}

void JsonWriter::key(std::string_view name) {
    if (stack_.empty() || !stack_.back().object) {
        fail("json key outside object");
    }
    auto& frame = stack_.back();
    if (!frame.first) {
        out_ << ",";
    }
    frame.first = false;
    raw_string(name);
    out_ << ":";
    after_key_ = true;
}

void JsonWriter::string(std::string_view value) {
    before_value();
    raw_string(value);
}

void JsonWriter::number(std::int64_t value) {
    before_value();
    out_ << value;
}

void JsonWriter::number(std::uint64_t value) {
    before_value();
    out_ << value;
}

void JsonWriter::boolean(bool value) {
    before_value();
    out_ << (value ? "true" : "false");
}

void JsonWriter::null() {
    before_value();
    out_ << "null";
}

void JsonWriter::field(std::string_view name, std::string_view value) {
    key(name);
    string(value);
}

void JsonWriter::field(std::string_view name, const std::string& value) {
    field(name, std::string_view(value));
}

void JsonWriter::field(std::string_view name, std::int64_t value) {
    key(name);
    number(value);
}

void JsonWriter::field(std::string_view name, std::uint64_t value) {
    key(name);
    number(value);
}

void JsonWriter::field(std::string_view name, bool value) {
    key(name);
    boolean(value);
}

void JsonWriter::field(std::string_view name, Amount value) {
    field(name, value.units());
}

std::string JsonWriter::finish_to_string() const {
    if (!stack_.empty()) {
        fail("json document still has open frames");
    }
    return {};
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!stack_.empty()) {
        auto& frame = stack_.back();
        if (!frame.object) {
            if (!frame.first) {
                out_ << ",";
            }
            frame.first = false;
        }
    }
}

void JsonWriter::raw_string(std::string_view value) {
    out_ << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '"':
            out_ << "\\\"";
            break;
        case '\\':
            out_ << "\\\\";
            break;
        case '\b':
            out_ << "\\b";
            break;
        case '\f':
            out_ << "\\f";
            break;
        case '\n':
            out_ << "\\n";
            break;
        case '\r':
            out_ << "\\r";
            break;
        case '\t':
            out_ << "\\t";
            break;
        default:
            if (c < 0x20) {
                out_ << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                     << static_cast<int>(c) << std::dec;
            } else {
                out_ << static_cast<char>(c);
            }
            break;
        }
    }
    out_ << '"';
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    JsonWriter writer(out);
    writer.string(value);
    return out.str();
}

std::string json_error(std::string_view error) {
    std::ostringstream out;
    JsonWriter writer(out);
    writer.begin_object();
    writer.field("ok", false);
    writer.field("error", error);
    writer.end_object();
    return out.str();
}

} // namespace bastion

