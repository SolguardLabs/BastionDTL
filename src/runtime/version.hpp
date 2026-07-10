#pragma once

#include "common/json.hpp"

#include <string>
#include <vector>

namespace bastion {

struct BuildInfo {
    std::string name;
    std::string version;
    std::string protocol;
    std::string language;
    std::vector<std::string> features;

    std::string display() const;
    std::string digest() const;
};

BuildInfo build_info();
void write_build_info(JsonWriter& json, const BuildInfo& info);

} // namespace bastion

