
#include "../include/RpiInterrupter.h"
#include <functional>
#include <iostream>
#include <unistd.h>
#include <climits>

using namespace std;
using namespace endail;

void onInterrupt() {
    std::cout << "***interrupt***" << std::endl;
}

int main() {

    RpiInterrupter::init();
    RpiInterrupter::attachInterrupt(
        8,
        RpiInterrupter::Edge::FALLING,
        std::function<void()>(onInterrupt));

    while(true) {
        ::sleep(UINT_MAX);
    }

    return 0;

}