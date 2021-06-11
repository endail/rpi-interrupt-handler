
#include "../include/Interrupter.h"
#include <chrono>
#include <climits>
#include <functional>
#include <iostream>
#include <thread>
#include <wiringPi.h>

using namespace std;
using namespace RpiGpioInterrupter;

int numInterrupts = 0;
bool keepRunning = true;

void onInterrupt() {
    
    std::cout << "***interrupt***" << std::endl;
    numInterrupts++;

    if(numInterrupts >= 10) {
        keepRunning = false;
    }

}

void pulsePin(const int pin) {
    //each second, toggle the output state of the pin
    bool state = digitalRead(pin) == HIGH;
    while(keepRunning) {
        state = !state;
        cout << "Setting pin " << pin << " to " << (state ? "high" : "low") << endl;
        digitalWrite(pin, state ? HIGH : LOW);
        this_thread::sleep_for(chrono::seconds(1));
    }
}

int main(int argc, char** argv) {

    wiringPiSetup();
    Interrupter::init();

    //both wiringpi num'd pins
    const int intPin = ::stoi(argv[1]);
    const int outPin = ::stoi(argv[2]);

    thread th = thread(pulsePin, outPin);

    Interrupter::attachInterrupt(
        wpiPinToGpio(intPin),
        Edge::BOTH,
        std::function<void()>(&onInterrupt));

    th.join();

    return 0;

}