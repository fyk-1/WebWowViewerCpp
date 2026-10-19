#ifndef WOWELY_TACT_READ_TIMING_H
#define WOWELY_TACT_READ_TIMING_H

#include <chrono>

/*
robots@Impl: Where one file read spends its time, for the loader's per-second line (RequestProcessor's LoaderTally). The
CASC processor clears it before a read and hands it on after; CDN.cpp adds to it as it goes. open is finding and opening the
archive (exists, file_size, the ifstream), read the seek and the bytes, inflate BLTE::Decode, overrides the check for a
file_edits override. Thread-local, so each loader thread times its own read.
*/
struct TactReadTiming {
    std::chrono::steady_clock::duration overrides{};
    std::chrono::steady_clock::duration open{};
    std::chrono::steady_clock::duration read{};
    std::chrono::steady_clock::duration inflate{};
};

inline thread_local TactReadTiming tactReadTiming;

#endif //WOWELY_TACT_READ_TIMING_H
