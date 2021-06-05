
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

    InterruptHandler::init();
    InterruptHandler::attachInterrupt(
        8,
        Edge::FALLING,
        std::function<void()>(&onInterrupt));

    while(true) {
        std::this_thread::yield();
    }

    return 0;

}