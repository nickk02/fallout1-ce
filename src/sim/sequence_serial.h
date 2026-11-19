#ifndef FALLOUT_SIM_SEQUENCE_SERIAL_H_
#define FALLOUT_SIM_SEQUENCE_SERIAL_H_

#include <cstdint>

namespace fallout {

// Names animation sequences for the player's click routes, and tells whether a
// pose call is the automatic stand that ends or steps one of them. Serial zero
// means none and is never issued.
class SequenceSerials {
public:
    explicit SequenceSerials(uint64_t last = 0)
        : last_(last)
    {
    }

    // False once every serial has been issued. The counter never wraps, so
    // a registration must check this first and fail without side effects.
    bool canIssue() const { return last_ != UINT64_MAX; }
    // The next serial, or zero when none is left.
    uint64_t issue() { return canIssue() ? ++last_ : 0; }
    uint64_t last() const { return last_; }

    // The serial whose automatic stand is running now, or zero. A registration
    // or clear made by a callback of that sequence is not an automatic stand
    // and reads zero here.
    uint64_t standing() const { return standing_; }

    class StandScope {
    public:
        StandScope(SequenceSerials& owner, uint64_t serial)
            : owner_(owner)
            , previous_(owner.standing_)
        {
            owner_.standing_ = serial;
        }

        ~StandScope() { owner_.standing_ = previous_; }

        StandScope(const StandScope&) = delete;
        StandScope& operator=(const StandScope&) = delete;

    private:
        SequenceSerials& owner_;
        uint64_t previous_;
    };

private:
    uint64_t last_;
    uint64_t standing_ = 0;
};

} // namespace fallout

#endif /* FALLOUT_SIM_SEQUENCE_SERIAL_H_ */
