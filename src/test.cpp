
#include "../include/InterruptHandler.h"
#include <functional>
#include <iostream>
#include <thread>

using namespace std;
using namespace InterruptHandler;

void onInterrupt() {
    std::cout << "***interrupt***" << std::endl;
}

int main() {

    int gpioPin = 8;
    Edge edgeType = Edge::FALLING;
    std::function<void()> cb(&onInterrupt);

    InterruptHandler::init();
    InterruptHandler::attachInterrupt(
        gpioPin,
        edgeType,
        cb);

    while(true) {
        std::this_thread::yield();
    }

    return 0;

}