
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
    Interrupter::removePin(wpiPinToGpio(interruptPin));
}

void pulsePin(const int pin) {
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
    interruptPin = ::stoi(argv[1]);
    outPin = ::stoi(argv[2]);

    pinMode(outPin, OUTPUT);

    thread th = thread(pulsePin, outPin);
    th.detach();

    Interrupter::attach(
        wpiPinToGpio(interruptPin),
        Edge::RISING,
        []() { cout << "interrupt one" << endl; });

    Interrupter::attach(
        wpiPinToGpio(interruptPin),
        Edge::RISING,
        []() { cout << "interrupt two" << endl; });

    th.join();

    Interrupter::close();

    return 0;

}