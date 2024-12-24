
/*
  WWVB Emulator for Adafruit Huzzah32 Featherboard (ESP32)

  There has been construction up the hill from me and this has caused the WWVB signal to be degraded all across my house except for one rear corner.
  Every daylight savings change I need to cycle my atomic clocks through this corner to get them updated. The goal of this emulator is to grab the current
  time via NTP and then create a local signal that my clocks can sync to.

  Change log:

    0.1   Get Blinky running using PlatformIO and the Arduino framework on the ESP32
    0.2   Create a first version using the code generation from https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/
    0.3   Use ESP32 timers to enable tweaking of the modulation to match the proper signal timing
*/

#include <Arduino.h>

#define LED 13 // Flash the led to show the ESP32 is up and running

// Hours in UTC time
#define hour_tens 0
#define hour_ones 7

#define minute_tens 3
// Minute ones default to zero

volatile uint8_t slot = 0;
volatile uint8_t wwvbSignal = 0;
volatile uint8_t minute_ones = 0;
volatile uint8_t ticks = 0;

bool bState = false;
String messageTimer = "Timer count is: ";
String messageSignal = "Signal count is: ";
String messageSlot = "Slot count is: ";

// Bit & Marker timers
hw_timer_t *TimerBit0 = NULL;
hw_timer_t *TimerBit1 = NULL;
hw_timer_t *TimerBitMarker = NULL;

// One Second timer
hw_timer_t *TimerSecond = NULL;

bool bDone = false;

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
  analogWrite(A0, 127);
  // Serial.println("Signal Enabled");
}

// This routine based on sbmull's code for the ATTINY - It generates the correct bit values for each second of the emulated signal
void IRAM_ATTR TimerSecond_ISR()
{
  switch (slot)
  {

  case 0:
  {
    wwvbSignal = 2;
    break;
  }

  case 1:
  {
    wwvbSignal = ((minute_tens >> 2) & 1);
    break;
  } // min 40
  case 2:
  {
    wwvbSignal = ((minute_tens >> 1) & 1);
    break;
  } // min 20
  case 3:
  {
    wwvbSignal = ((minute_tens >> 0) & 1);
    break;
  } // min 10

  case 5:
  {
    wwvbSignal = ((minute_ones >> 4) & 1);
    break;
  } // min 8
  case 6:
  {
    wwvbSignal = ((minute_ones >> 2) & 1);
    break;
  } // min 4
  case 7:
  {
    wwvbSignal = ((minute_ones >> 1) & 1);
    break;
  } // min 2
  case 8:
  {
    wwvbSignal = (minute_ones & 1);
    break;
  } // min 1

  case 9:
  {
    wwvbSignal = 2;
    break;
  }

  case 12:
  {
    wwvbSignal = ((hour_tens >> 1) & 1);
    break;
  } // hour 20
  case 13:
  {
    wwvbSignal = ((hour_tens >> 0) & 1);
    break;
  } // hour 10

  case 15:
  {
    wwvbSignal = ((hour_ones >> 4) & 1);
    break;
  } // hour 8
  case 16:
  {
    wwvbSignal = ((hour_ones >> 2) & 1);
    break;
  } // hour 4
  case 17:
  {
    wwvbSignal = ((hour_ones >> 1) & 1);
    break;
  } // hour 2
  case 18:
  {
    wwvbSignal = (hour_ones & 1);
    break;
  } // hour 1

  case 19:
  {
    wwvbSignal = 2;
    break;
  }

  case 26:
  {
    wwvbSignal = 1;
    break;
  } //
  case 27:
  {
    wwvbSignal = 1;
    break;
  } // Day of year 60
  case 29:
  {
    wwvbSignal = 2;
    break;
  } //

  case 31:
  {
    wwvbSignal = 1;
    break;
  } //
  case 32:
  {
    wwvbSignal = 1;
    break;
  } // Day of year 6
  case 37:
  {
    wwvbSignal = 1;
    break;
  } //
  case 39:
  {
    wwvbSignal = 2;
    break;
  }

  case 42:
  {
    wwvbSignal = 1;
    break;
  } //
  case 43:
  {
    wwvbSignal = 1;
    break;
  } // DUT1 = 0.3
  case 49:
  {
    wwvbSignal = 2;
    break;
  }

  case 50:
  {
    wwvbSignal = 1;
    break;
  } // Year = 08
  case 55:
  {
    wwvbSignal = 1;
    break;
  } // Leap year = True
  case 59:
  {
    wwvbSignal = 2;
    break;
  }

  default:
  {
    wwvbSignal = 0;
    break;
  }
  }

  switch (wwvbSignal)
  {
  case 0:
  {
    Serial.print("0");
    // 0 (0.2s reduced power)
    analogWrite(A0, 0);

    // TimerBit0
    timerRestart(TimerBit0);
    timerAlarmEnable(TimerBit0);
  }
  break;
  case 1:
  {
    Serial.print("1");
    // 1 (0.5s reduced power)
    analogWrite(A0, 0);

    // TimerBitMarker
    timerRestart(TimerBit1);
    timerAlarmEnable(TimerBit1);
  }
  break;
  case 2:
  {
    Serial.print("M");

    // Marker (0.8s reduced power)
    analogWrite(A0, 0);

    // TimerBitMarker
    timerRestart(TimerBitMarker);
    timerAlarmEnable(TimerBitMarker);
  }
  break;
  }
 
  slot++; // Advance data slot in minute data packet
  if (slot == 60)
  {
    slot = 0;      // Reset slot to 0 if at 60 seconds
    minute_ones++; // Advance minute count
    Serial.println();
  }

  // Serial.println(messageSignal + wwvbSignal);
  // Serial.println(messageSlot + slot);
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(A0, OUTPUT);

  // Create the timers for seconds and bits/marker
  TimerSecond = timerBegin(0, 80, true);
  TimerBitMarker = timerBegin(1, 80, true);
  TimerBit0 = timerBegin(2, 80, true);
  TimerBit1 = timerBegin(3, 80, true);

  // Attach the appropriate ISR to each timer
  timerAttachInterrupt(TimerSecond, &TimerSecond_ISR, true);
  timerAttachInterrupt(TimerBit0, &TimerSignalReenable_ISR, true);
  timerAttachInterrupt(TimerBit1, &TimerSignalReenable_ISR, true);
  timerAttachInterrupt(TimerBitMarker, &TimerSignalReenable_ISR, true);

  // Set timer alarm to trigger every second (1,000,000 µs)
  timerAlarmWrite(TimerSecond, 1000000, true);    
  timerAlarmWrite(TimerBit0, 200000, false);      
  timerAlarmWrite(TimerBit1, 500000, false);      
  timerAlarmWrite(TimerBitMarker, 800000, false); 

  Serial.println("Start");

  // The one second timer runs always
  timerAlarmEnable(TimerSecond);

  // The WWVB signal is a 60KHz AM moduleted signal (there is a phase modulated option but this emulator is AM only)
  analogWriteFrequency(60000);

  // Start 60KHz signal
  analogWrite(A0, 127);
}

void loop()
{
  if ((ticks > 60) & (bDone == false))
  {
    timerAlarmDisable(TimerSecond);
    analogWrite(A0, 0);
    bDone = true;
    Serial.println("\nDone");
  }

  // Flash the led to show the ESP32 is up and running
  digitalWrite(LED, HIGH);
  // Serial.println("LED is on");
  delay(1000);
  digitalWrite(LED, LOW);
  // Serial.println("LED is off");
  delay(1000);
}