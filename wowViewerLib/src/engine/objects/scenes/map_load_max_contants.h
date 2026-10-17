#include <chrono>

const int MAX_LOAD_ADT_PER_FRAME = 1;

/*
robots@Impl: The time one frame spends finishing loaded M2 geometry, WMO roots and WMO groups (Map::doPostLoad). The count caps
this replaces were 100000 and 20000, so a tile's models and groups all finished in the frame they were first seen, one hitch
the size of the tile. A quarter of a 60 Hz frame (16.7 ms) is 4 ms: it leaves the rest to update, culling, recording and the
GPU wait, while a tile's worth of work still finishes over some frames. The item that crosses the budget finishes, since an
item cannot be cut; a frame finishes at least one so the queue always advances.
*/
const std::chrono::microseconds POST_LOAD_BUDGET = std::chrono::microseconds(4000);

/*
robots@Impl: The ceiling on the fast-load window. A map that opens finishes its tiles and geometry without the per-frame
budgets (MAX_LOAD_ADT_PER_FRAME, POST_LOAD_BUDGET) until the tiles around the arrival point are in, then the budgets return for
the rest of the session. The window ends when it settles; the ceiling, counted in wall clock from the open, is for a map that
never does, such as a tile whose files never arrive. The loader is one thread reading from a cold install, so a continent start
takes seconds, not frames: 15 s is long enough for that, and short enough that a map stuck short of settling spends a quarter
of a minute without budgets, not the session.
*/
const std::chrono::seconds FAST_LOAD_CEILING = std::chrono::seconds(15);
