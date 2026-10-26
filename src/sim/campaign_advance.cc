#include "sim/campaign_advance.h"

namespace fallout {

CampaignAdvanceResult advanceCampaignTime(CampaignQueue& queue, uint32_t target, const CampaignAdvanceOptions& options)
{
    CampaignAdvanceResult result;

    if (queue.gameTime() >= target) {
        return result;
    }

    while (true) {
        if (options.shouldStop && options.shouldStop()) {
            result.status = CampaignAdvanceStatus::Stopped;
            return result;
        }

        uint32_t now = queue.gameTime();
        uint32_t next;
        bool hasNext = queue.nextEventTime(&next);

        // Due now or overdue: run it where the time is.
        if (hasNext && next <= now) {
            if (result.events >= options.maxEvents) {
                result.status = CampaignAdvanceStatus::NoProgress;
                return result;
            }

            result.events++;
            if (queue.processNext() != 0) {
                result.status = CampaignAdvanceStatus::Interrupted;
                return result;
            }

            continue;
        }

        if (now >= target) {
            return result;
        }

        if (!hasNext || next > target) {
            queue.setGameTime(target);
            return result;
        }

        // now < next <= target. One past the event, limited to the target.
        queue.setGameTime(next < target ? next + 1 : target);
        result.slices++;
    }
}

CampaignAdvanceResult advanceCampaignBy(CampaignQueue& queue, uint32_t delta, const CampaignAdvanceOptions& options)
{
    uint32_t now = queue.gameTime();
    if (delta > UINT32_MAX - now) {
        CampaignAdvanceResult result;
        result.status = CampaignAdvanceStatus::TimeOverflow;
        return result;
    }

    return advanceCampaignTime(queue, now + delta, options);
}

} // namespace fallout
