#include "security/identity.hpp"

#include <sstream>

namespace bastion {

std::string role_name(IdentityRole role) {
    switch (role) {
    case IdentityRole::Owner:
        return "owner";
    case IdentityRole::Operator:
        return "operator";
    case IdentityRole::Auditor:
        return "auditor";
    case IdentityRole::Treasurer:
        return "treasurer";
    case IdentityRole::Beneficiary:
        return "beneficiary";
    case IdentityRole::Service:
        return "service";
    }
    return "service";
}

IdentityRole parse_role(std::string_view value) {
    const auto normalized = normalize_label(value);
    if (normalized == "owner") {
        return IdentityRole::Owner;
    }
    if (normalized == "operator") {
        return IdentityRole::Operator;
    }
    if (normalized == "auditor") {
        return IdentityRole::Auditor;
    }
    if (normalized == "treasurer") {
        return IdentityRole::Treasurer;
    }
    if (normalized == "beneficiary") {
        return IdentityRole::Beneficiary;
    }
    if (normalized == "service") {
        return IdentityRole::Service;
    }
    fail("unknown identity role");
}

bool Signature::empty() const {
    return signer.empty() || value.empty();
}

std::string Signature::describe() const {
    return signer.str() + ":" + value.substr(0, 12);
}

bool Identity::can_sign_receipts() const {
    return enabled && role == IdentityRole::Operator;
}

bool Identity::can_manage_accounts() const {
    return enabled && (role == IdentityRole::Owner || role == IdentityRole::Treasurer);
}

std::string Identity::fingerprint() const {
    CanonicalBuilder builder;
    builder.field("id", id.str());
    builder.field("role", role_name(role));
    builder.field("public", public_key);
    builder.field("enabled", enabled);
    return compact_digest(builder.str());
}

void IdentityRegistry::register_identity(Identity identity) {
    if (identity.id.empty()) {
        fail("identity id is required");
    }
    if (identity.public_key.empty()) {
        identity.public_key = deterministic_id("pub", identity.id.str() + role_name(identity.role));
    }
    if (identity.secret_material.empty()) {
        identity.secret_material = deterministic_id("sec", identity.public_key + identity.id.str());
    }
    const auto inserted = identities_.emplace(identity.id, identity);
    if (!inserted.second) {
        fail("identity already registered");
    }
}

bool IdentityRegistry::contains(const IdentityId& id) const {
    return identities_.find(id) != identities_.end();
}

bool IdentityRegistry::enabled(const IdentityId& id) const {
    auto found = identities_.find(id);
    return found != identities_.end() && found->second.enabled;
}

const Identity& IdentityRegistry::get(const IdentityId& id) const {
    auto found = identities_.find(id);
    if (found == identities_.end()) {
        fail("identity is not registered");
    }
    return found->second;
}

std::optional<Identity> IdentityRegistry::find(const IdentityId& id) const {
    auto found = identities_.find(id);
    if (found == identities_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void IdentityRegistry::disable(const IdentityId& id) {
    auto found = identities_.find(id);
    if (found == identities_.end()) {
        fail("identity is not registered");
    }
    found->second.enabled = false;
}

void IdentityRegistry::enable(const IdentityId& id) {
    auto found = identities_.find(id);
    if (found == identities_.end()) {
        fail("identity is not registered");
    }
    found->second.enabled = true;
}

Signature IdentityRegistry::sign(const IdentityId& signer, std::string_view payload) const {
    const auto& identity = get(signer);
    if (!identity.enabled) {
        fail("disabled identity cannot sign");
    }
    return Signature{signer, digest_for(signer, payload), "bdtl-fnv-keyed-v1"};
}

bool IdentityRegistry::verify(const Signature& signature, std::string_view payload) const {
    if (signature.empty()) {
        return false;
    }
    auto found = identities_.find(signature.signer);
    if (found == identities_.end() || !found->second.enabled) {
        return false;
    }
    return signature.value == digest_for(signature.signer, payload);
}

std::string IdentityRegistry::digest_for(const IdentityId& signer, std::string_view payload) const {
    const auto& identity = get(signer);
    CanonicalBuilder builder;
    builder.field("identity", identity.id.str());
    builder.field("role", role_name(identity.role));
    builder.field("public", identity.public_key);
    builder.field("payload", payload);
    return keyed_digest(identity.secret_material, builder.str());
}

std::vector<Identity> IdentityRegistry::list() const {
    std::vector<Identity> out;
    out.reserve(identities_.size());
    for (const auto& [_, identity] : identities_) {
        out.push_back(identity);
    }
    return out;
}

Identity make_identity(std::string_view id, IdentityRole role, std::string_view name) {
    Identity identity;
    identity.id = IdentityId(id);
    identity.role = role;
    identity.display_name = std::string(name);
    identity.public_key = deterministic_id("pub", std::string(id) + ":" + role_name(role));
    identity.secret_material = deterministic_id("sec", identity.public_key + ":" + std::string(name));
    identity.enabled = true;
    return identity;
}

IdentityRegistry default_identities() {
    IdentityRegistry registry;
    registry.register_identity(make_identity("owner:atlas", IdentityRole::Owner, "Atlas Treasury"));
    registry.register_identity(make_identity("owner:forge", IdentityRole::Owner, "Forge Custody"));
    registry.register_identity(make_identity("treasury:core", IdentityRole::Treasurer, "Core Treasury"));
    registry.register_identity(make_identity("operator:north", IdentityRole::Operator, "North Operations"));
    registry.register_identity(make_identity("operator:south", IdentityRole::Operator, "South Operations"));
    registry.register_identity(make_identity("operator:west", IdentityRole::Operator, "West Operations"));
    registry.register_identity(make_identity("beneficiary:merchant", IdentityRole::Beneficiary, "Merchant Desk"));
    registry.register_identity(make_identity("beneficiary:market", IdentityRole::Beneficiary, "Market Maker"));
    registry.register_identity(make_identity("beneficiary:escrow", IdentityRole::Beneficiary, "Escrow Receiver"));
    registry.register_identity(make_identity("auditor:watch", IdentityRole::Auditor, "Watchtower"));
    return registry;
}

} // namespace bastion

