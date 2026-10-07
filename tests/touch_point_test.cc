#include "../main/boards/m5stack/stackchan-k151/touch_point.h"
#include <algorithm>
#include <cassert>
int main() {
    TouchPoint point;
    const unsigned char down[6] = {1, 0, 30, 0, 40, 0};
    assert(point.Read([&](unsigned char* p) {
        std::copy(down, down + 6, p);
        return true;
    }));
    assert(point.num == 1 && point.x == 30 && point.y == 40);
    assert(!point.Read([](unsigned char*) { return false; }));
    assert(point.num == 0 && point.x == -1 && point.y == -1);
    const unsigned char invalid[6] = {15, 0, 30, 0, 40, 0};
    assert(!point.Read([&](unsigned char* p) {
        std::copy(invalid, invalid + 6, p);
        return true;
    }));
}
