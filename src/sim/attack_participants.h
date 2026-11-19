#ifndef FALLOUT_SIM_ATTACK_PARTICIPANTS_H_
#define FALLOUT_SIM_ATTACK_PARTICIPANTS_H_

#include "game/combat_defs.h"
#include "object_handle.h"
#include <array>
#include <functional>
#include <memory>

namespace fallout {

// Fixed-size identities for participant continuations, independent of animation
// ownership. A null participant is optional; a nonnull one must remain alive.
// Weapon lifetime is separate: thrown/projectile weapons may be consumed by
// sequencing, and captured ammo has its own identity guard.
class AttackParticipants {
public:
    AttackParticipants() = default;
    AttackParticipants(const ObjectRegistry& registry, const Attack& attack, uint64_t epoch, Object* player);
    bool refresh(const ObjectRegistry& registry, uint64_t epoch, Object* player, Attack& attack) const;
    // Weapon is required before sequencing. A consumed throw may disappear;
    // retain it only while the original allocation still resolves.
    bool refreshForAttack(const ObjectRegistry& registry, uint64_t epoch, Object* player,
        Attack& attack, const ObjectRef& weapon, bool consumedThrow = false) const;
    bool clearForAttack(const ObjectRegistry& registry, const std::function<uint64_t()>& epoch,
        const std::function<Object*()>& player, Attack& attack, const ObjectRef& weapon,
        const std::function<int(Object*)>& clear) const;
    // Validate both sides of a script/gameplay boundary. Committed effects are
    // not rolled back when the callback invalidates a required identity.
    bool run(const ObjectRegistry& registry, const std::function<uint64_t()>& epoch,
        const std::function<Object*()>& player, Attack& attack,
        const std::function<void()>& callback) const;

private:
    static constexpr int kCount = 4 + EXPLOSION_TARGET_COUNT;
    std::array<ObjectRef, kCount> refs_ {};
    std::array<bool, kCount> required_ {};
    uint64_t epoch_ = 0;
    int extrasLength_ = 0;
    bool captured_ = false;
};

// Captured death-visual type remains usable after a disposable weapon is gone.
// A negative value retains the legacy live-weapon lookup for other callers.
int attack_visual_damage_type(int capturedType, Object* weapon, const std::function<int(Object*)>& lookup);

// Reports from action_explode/action_dmg have a disposable visual attacker.
// Capture the gameplay participants before animation registration, without making
// the visual's intended deletion a gameplay cancellation.
class DeferredAttackReport {
public:
    DeferredAttackReport(const ObjectRegistry& registry, const Attack& original,
        uint64_t epoch, Object* player, Object* source);
    bool refresh(const ObjectRegistry& registry, uint64_t epoch, Object* player);
    Attack attack;
    AttackParticipants participants;
    Object* source = nullptr;

private:
    ObjectRef sourceRef_;
    bool hadSource_;
};

// Keeps ownership during registration, including synchronous delivery. A forced
// callback consumes the same token as normal delivery. Failed registration drops
// an undelivered token, since anim_cleanup does not execute gameplay callbacks.
class AttackReportRegistration {
public:
    explicit AttackReportRegistration(std::shared_ptr<DeferredAttackReport> report);
    ~AttackReportRegistration();
    AttackReportRegistration(const AttackReportRegistration&) = delete;
    AttackReportRegistration& operator=(const AttackReportRegistration&) = delete;
    void* token() const { return token_; }
    void commit() { committed_ = true; }
    static std::shared_ptr<DeferredAttackReport> take(void* token);

private:
    struct State {
        bool delivered = false;
    };
    struct Token {
        std::shared_ptr<DeferredAttackReport> report;
        std::shared_ptr<State> state;
    };
    std::shared_ptr<State> state_;
    Token* token_;
    bool committed_ = false;
};

} // namespace fallout
#endif
