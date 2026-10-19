#include <chrono>

/*
robots@Impl: The floor of the two load budgets of a frame (Map::loadBudget): the time it spends on tile load stages, and the
time it spends finishing loaded M2 geometry, WMO roots and WMO groups (both in Map::doPostLoad). Each is half the frame period
the main loop paces to (Map::setFramePeriod), and this floor when it paces to none. A tile costs about 17 ms of stages, each 0
to 20 ms, while a frame paced to 30 fps does about 5 ms of other work, so a smaller budget left the render thread asleep
most of each frame while a jump's eighty new tiles waited seconds to show. Finishing was a quarter of a 60 Hz frame, 4 ms,
which drained a jump's wave of two thousand models at about a hundred a second, so the models faded in for seconds after
the ground; at half a frame the wave takes a hitchy second or two instead. The count caps that replaced were 100000 and
20000, so a tile's models and groups all finished in the frame they were first seen, one hitch the size of the tile. The stage
or item that crosses its budget finishes, since neither can be cut, and a frame runs at least one of each so both queues
always advance.
*/
const std::chrono::microseconds LOAD_BUDGET = std::chrono::microseconds(4000);

/*
robots@Impl: The ceiling on the fast-load window. A map that opens finishes its tiles and geometry without the per-frame
budgets (LOAD_BUDGET) until the tiles around the arrival point are in, then the budgets return for
the rest of the session. The window ends when it settles; the ceiling, counted in wall clock from the open, is for a map that
never does, such as a tile whose files never arrive. The loader is one thread reading from a cold install, so a continent start
takes seconds, not frames: 15 s is long enough for that, and short enough that a map stuck short of settling spends a quarter
of a minute without budgets, not the session.
*/
const std::chrono::seconds FAST_LOAD_CEILING = std::chrono::seconds(15);
