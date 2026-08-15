#include "runtime/version.hpp"

#include "common/hash.hpp"

namespace bastion {

std::string BuildInfo::display() const {
    return name + " " + version + " (" + protocol + ")";
}

std::string BuildInfo::digest() const {
    CanonicalBuilder builder;
    builder.field("name", name);
    builder.field("version", version);
    builder.field("protocol", protocol);
    builder.field("language", language);
    builder.open("features");
    for (const auto& feature : features) {
        builder.field("feature", feature);
    }
    builder.close();
    return stable_hash_hex(builder.str());
}

BuildInfo build_info() {
    BuildInfo info;
    info.name = "BastionDTL";
    info.version = "1.0.0";
    info.protocol = "custody-settlement-v1";
    info.language = "C++20";
    info.features = {
        "segregated-accounts",
        "operator-rotation",
        "signed-receipts",
        "receipt-manifests",
        "batch-settlement",
        "ledger-reconciliation",
        "liquidity-stress",
        "weighted-change-control",
    };
    return info;
}

void write_build_info(JsonWriter& json, const BuildInfo& info) {
    json.begin_object();
    json.field("name", info.name);
    json.field("version", info.version);
    json.field("protocol", info.protocol);
    json.field("language", info.language);
    json.field("digest", info.digest());
    json.key("features");
    json.begin_array();
    for (const auto& feature : info.features) {
        json.string(feature);
    }
    json.end_array();
    json.end_object();
}

} // namespace bastion

