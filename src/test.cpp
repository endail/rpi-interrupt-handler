
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

int main(int argc, char** argv) {

    const int gpioPin = ::stoi(argv[1]);

    RpiInterrupter::init();
    RpiInterrupter::attachInterrupt(
        gpioPin,
        RpiInterrupter::Edge::FALLING,
        std::function<void()>(onInterrupt));

    while(true) {
        std::cout << "main thread sleeping" << std::endl;
        ::sleep(UINT_MAX);
    }

    return 0;

}