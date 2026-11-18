#include "game/calendar_queue.h"

#include <limits.h>

#include "game/queue.h"
#include "game/scripts.h"

namespace fallout {

uint32_t GameCalendarQueue::gameTime() const
{
    int time = game_time();
    return time < 0 ? 0 : static_cast<uint32_t>(time);
}

bool GameCalendarQueue::timeValid() const
{
    return game_time() >= 0;
}

bool GameCalendarQueue::trySetGameTime(uint32_t time)
{
    if (time == 0 || time > static_cast<uint32_t>(INT_MAX)) {
        return false;
    }

    set_game_time(static_cast<int>(time));
    return true;
}

void GameCalendarQueue::setGameTime(uint32_t time)
{
    trySetGameTime(time);
}

bool GameCalendarQueue::nextEventTime(uint32_t* time) const
{
    int next;
    if (!queue_peek_time(&next)) {
        return false;
    }

    *time = next < 0 ? 0 : static_cast<uint32_t>(next);
    return true;
}

int GameCalendarQueue::processNext()
{
    int result = 0;
    queue_process_one(game_time(), &result);
    return result;
}

} // namespace fallout
