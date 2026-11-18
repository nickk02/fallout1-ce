#ifndef FALLOUT_SIM_BUFFERED_ATTACK_H_
#define FALLOUT_SIM_BUFFERED_ATTACK_H_

#include <functional>
#include <optional>

#include "object_handle.h"

namespace fallout {

enum class BufferedAttackKind {
    Ordinary,
    Vats,
};

// Captured intent only. Resource and combat-state checks belong to execution.
struct BufferedAttackRequest {
    ObjectRef attacker;
    ObjectRef target;
    ObjectRef weapon;
    bool unarmed = false;
    int hitMode = 0;
    BufferedAttackKind kind = BufferedAttackKind::Ordinary;
    uint64_t epoch = 0;
    uint32_t capturedAt = 0;
};

// Snapshot the selected mode and allocation identities at request time. The
// pointers are looked up without dereferencing them. Unarmed captures no weapon.
BufferedAttackRequest buffered_attack_capture(const ObjectRegistry& registry,
    const void* attacker, const void* target, const void* weapon, bool unarmed,
    int hitMode, BufferedAttackKind kind, uint64_t epoch, uint32_t now);

enum class BufferedAttackReject {
    None,
    Empty,
    EpochChanged,
    Expired,
    StaleAttacker,
    StaleTarget,
    StaleWeapon,
    WeaponMismatch,
};

struct BufferedAttackResult {
    BufferedAttackReject reason = BufferedAttackReject::Empty;
    // Present only on acceptance. No resolved pointers escape the core.
    std::optional<BufferedAttackRequest> request;
};

// Called only after all required references resolve. Check ownership and that
// capturedHitMode still selects the captured weapon. weapon is null for unarmed.
// Only read object state. Do not destroy objects or change the world lifecycle.
using BufferedAttackWeaponCheck = std::function<bool(const void* attacker, const void* weapon, int capturedHitMode)>;

class BufferedAttack {
public:
    // Replace the single pending request with a value copy.
    void put(const BufferedAttackRequest& request);
    bool pending() const { return request_.has_value(); }
    void cancel();
    void reset() { cancel(); }
    // Exclude a pause from the pending request's age. Addition wraps with the
    // unsigned wall clock. An empty buffer stays empty.
    void delayExpiry(uint32_t delta);

    // Removes the request before validation or callbacks, even on rejection.
    // Unsigned wall-time age preserves the existing TTL: age > 2500 ms expires.
    // An armed request needs a live weapon and a weapon-check callback that
    // agrees. When supplied, the callback also checks an unarmed selection.
    BufferedAttackResult take(const ObjectRegistry& registry, uint64_t epoch,
        uint32_t now, const BufferedAttackWeaponCheck& weaponMatchesMode = {});

private:
    std::optional<BufferedAttackRequest> request_;
};

} // namespace fallout

#endif /* FALLOUT_SIM_BUFFERED_ATTACK_H_ */
