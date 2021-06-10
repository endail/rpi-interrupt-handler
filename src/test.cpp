
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

    RpiInterrupter::attachInterrupt(
        gpioPin,
        RpiInterrupter::Edge::BOTH,
        std::function<void()>(&onInterrupt));

    while(true) {
        ::sleep(UINT_MAX);
    }

    return 0;

}