#include "core/Types.h"

#include <chrono>

namespace nhn {

TickCount NowTick() {
    return ::GetTickCount64();
}

int64 NowUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace nhn
