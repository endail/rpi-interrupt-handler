
#include "../include/Interrupter.h"
#include <chrono>
#include <climits>
#include <functional>
#include <iostream>
#include <thread>
#include <wiringPi.h>

using namespace std;
using namespace RpiGpioInterrupter;

bool keepRunning = true;
int interruptPin;
int outPin;

void onInterrupt() {
    std::cout << "***interrupt***" << std::endl << std::flush;
}

void pulsePin(const int pin) {
    bool state = digitalRead(pin) == HIGH;
    while(keepRunning) {
        state = !state;
        cout << "Setting pin " << pin << " to " << (state ? "high" : "low") << endl;
        digitalWrite(pin, state ? HIGH : LOW);
        this_thread::sleep_for(chrono::seconds(1));

            //this isn't working because there is no epoll_wait
            //occurring at the time of the cancel event being raised!
            //
            //i think it would be better to have 1 thread which monitors ALL
            //interrupt pins, then invokes a separate thread when
            //an interrupt occurs!
            Interrupter::removeInterrupt(wpiPinToGpio(interruptPin));
    }
}

int main(int argc, char** argv) {

    wiringPiSetup();
    Interrupter::init();

    //both wiringpi num'd pins
    interruptPin = ::stoi(argv[1]);
    outPin = ::stoi(argv[2]);

    pinMode(outPin, OUTPUT);

    thread th = thread(pulsePin, outPin);

    Interrupter::attachInterrupt(
        wpiPinToGpio(interruptPin),
        Edge::FALLING,
        std::function<void()>(&onInterrupt));

    th.join();

    Interrupter::close();

    return 0;

}