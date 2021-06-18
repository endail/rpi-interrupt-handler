
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
int pulseCount = 0;
chrono::high_resolution_clock::time_point whenPulsed;
CALLBACK_ID int1;
CALLBACK_ID int2;

void onInterrupt() {
    auto now = chrono::high_resolution_clock::now();
    auto diff = chrono::duration_cast<chrono::microseconds>(now - whenPulsed);
    cout << "***interrupt, took " << diff.count() << " microseconds" << endl;
}

void pulsePin(const int pin) {
    bool state = digitalRead(pin) == HIGH;
    while(keepRunning) {
        state = !state;
        //cout << "Setting pin " << pin << " to " << (state ? "high" : "low") << endl;
        whenPulsed = chrono::high_resolution_clock::now();
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

    int1 = Interrupter::attach( 
        wpiPinToGpio(interruptPin),
        Edge::BOTH,
        onInterrupt);

    th.join();

    Interrupter::close();

    return 0;

}