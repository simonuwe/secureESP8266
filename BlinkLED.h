#include <stdint.h>

class BlinkLED{
  public:
    BlinkLED(uint8_t led, uint32_t duration, uint16_t onratio =1, uint16_t offratio = 1);
    BlinkLED(uint8_t led);
    void setDuration(uint32_t duration, uint16_t onratio, uint16_t offratio);
    void setDuration(uint32_t duration);
    void setLED(uint8_t led);
    void setRatio(uint16_t on, uint16_t off);
    void toggleLED();
    void switchOff() {_working = true;};
    void switchOn()  {_working = false;};
    void loop();
  private:
    uint16_t      _durations[2];
    bool          _working     = true;
    uint8_t       _led         = 0;
    unsigned long _nextswitch  = 0;
    uint8_t       _steps       = 2;
    uint8_t       _curstep     = 0;
};
