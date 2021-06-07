
#include "../include/RpiInterrupter.h"
#include <functional>
#include <iostream>
#include <thread>

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
        std::function<void()>(&onInterrupt));

    while(true) {
        std::this_thread::yield();
    }

    return 0;

}