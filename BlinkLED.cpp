#include "BlinkLED.h"
#include <Arduino.h>


BlinkLED::BlinkLED(uint8_t led){
  setLED(led);  
  _working      = true;
  _curstep      = 0;
  _steps        = 2;
  _durations[0] = 1000;
  _durations[1] = 1000;
/*
  _steps = 18;
  _durations[ 0] =1750; _durations[ 1] = 250;
  _durations[ 2] = 250; _durations[ 3] = 250;
  _durations[ 4] = 250; _durations[ 5] = 250;
  _durations[ 6] = 250; _durations[ 7] = 750;
  _durations[ 8] = 250; _durations[ 9] = 750;
  _durations[10] = 250; _durations[11] = 750;
  _durations[12] = 250; _durations[13] = 250;
  _durations[14] = 250; _durations[15] = 250;
  _durations[16] = 250; _durations[17] = 250;
*/
}


void BlinkLED::setRatio(uint16_t onduration, uint16_t offduration){
_durations[0] = onduration;
_durations[1] = offduration; 
_steps = 2;
}


void BlinkLED::setDuration(uint32_t duration, uint16_t onratio, uint16_t offratio){
  uint32_t onduration = duration * onratio/(onratio + offratio);
  setRatio(onduration, duration-onduration);
}


void BlinkLED::setDuration(uint32_t duration){
  uint32_t onduration = duration * _durations[0]/(_durations[0] + _durations[1]);
  setRatio(onduration, duration-onduration);
}


void BlinkLED::setLED(uint8_t led){
  _led = led;
  pinMode(_led, OUTPUT);
}


void BlinkLED::loop(){
  if(_working){
    long now = millis();
    if(now >= _nextswitch){
//      Serial.print("LED: "); Serial.println(_curstep);
      digitalWrite(_led, (_curstep & 0x01)?HIGH:LOW);
      _nextswitch = now + (_durations[_curstep]);
      _curstep = (++_curstep) % _steps;
    }
  }
}
