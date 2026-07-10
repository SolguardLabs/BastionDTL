#pragma once

#include "common/hash.hpp"
#include "common/types.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bastion {

enum class IdentityRole {
    Owner,
    Operator,
    Auditor,
    Treasurer,
    Beneficiary,
    Service,
};

std::string role_name(IdentityRole role);
IdentityRole parse_role(std::string_view value);

struct Signature {
    IdentityId signer;
    std::string value;
    std::string algorithm = "bdtl-fnv-keyed-v1";

    bool empty() const;
    std::string describe() const;
};

struct Identity {
    IdentityId id;
    IdentityRole role = IdentityRole::Service;
    std::string display_name;
    std::string public_key;
    std::string secret_material;
    bool enabled = true;

    bool can_sign_receipts() const;
    bool can_manage_accounts() const;
    std::string fingerprint() const;
};

class IdentityRegistry {
public:
    void register_identity(Identity identity);
    bool contains(const IdentityId& id) const;
    bool enabled(const IdentityId& id) const;
    const Identity& get(const IdentityId& id) const;
    std::optional<Identity> find(const IdentityId& id) const;
    void disable(const IdentityId& id);
    void enable(const IdentityId& id);

    Signature sign(const IdentityId& signer, std::string_view payload) const;
    bool verify(const Signature& signature, std::string_view payload) const;
    std::string digest_for(const IdentityId& signer, std::string_view payload) const;
    std::vector<Identity> list() const;

private:
    std::map<IdentityId, Identity> identities_;
};

Identity make_identity(std::string_view id, IdentityRole role, std::string_view name);
IdentityRegistry default_identities();

} // namespace bastion

