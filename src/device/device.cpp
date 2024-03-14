#include "./device.h"

Device::Device() {
    homed = false;
    limit = 1000;
    currentPosition = 0.0;
    ratio = 1.0;
    accel = 1.0;
}
Device::~Device() {
}

void Device::homeAxis(){
    currentPosition = 0.0;
    homed = true;
}

void Device::setPosition(float newPosition) { 
    if (newPosition < 0.0) {
        currentPosition = 0.0;
    } else if (newPosition <= limit && newPosition >= 0.0 ) {
        currentPosition = newPosition;
    } else if (newPosition > limit) {
        currentPosition = limit;
    }
}

bool Device::isHomed() {return homed;}

float Device::getPosition() {return currentPosition;}

float Device::getLimit() {return limit;}