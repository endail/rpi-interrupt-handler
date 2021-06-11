
#include "../include/Interrupter.h"
#include <chrono>
#include <climits>
#include <functional>
#include <iostream>
#include <thread>

using namespace std;
using namespace RpiGpioInterrupter;

void onInterrupt() {
    std::cout << "***interrupt***" << std::endl;
}

int main(int argc, char** argv) {

    const int gpioPin = ::stoi(argv[1]);

    Interrupter::attachInterrupt(
        gpioPin,
        Edge::BOTH,
        std::function<void()>(&onInterrupt));

    while(true) {
        this_thread::sleep_for(chrono::seconds(UINT_MAX));
    }

    return 0;

}