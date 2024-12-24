/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/vs-code-platformio-ide-esp32-esp8266-arduino/
*********/

#include <Arduino.h>

#define LED 13
// Hours in UTC time
#define hour_tens 2
#define hour_ones 2

#define minute_tens 2
// Minute ones default to zero

volatile uint8_t slot = 0;
volatile uint8_t wwvbSignal = 0;

volatile uint8_t minute_ones;

hw_timer_t *TimerSecond = NULL;
bool bState = false;
String messageTimer = "Timer count is: ";
String messageSignal = "Signal count is: ";
String messageSlot = "Slot count is: ";

// BitMarker timer
hw_timer_t *TimerBit0 = NULL;
hw_timer_t *TimerBit1 = NULL;
hw_timer_t *TimerBitMarker = NULL;

void IRAM_ATTR TimerSignalReenable_ISR()
{
  analogWrite(A0, 127);
  //Serial.println("Signal Enabled");
}

void IRAM_ATTR TimerSecond_ISR()
{

   	switch (slot) {
		
		case 0 : { wwvbSignal = 2;break;} 
		
		case 1 : { wwvbSignal = ((minute_tens >> 2) & 1);break;} // min 40
		case 2 : { wwvbSignal = ((minute_tens >> 1) & 1);break;} // min 20
		case 3 : { wwvbSignal = ((minute_tens >> 0) & 1);break;} // min 10
		
		case 5 : { wwvbSignal = ((minute_ones >> 4) & 1);break;} // min 8
		case 6 : { wwvbSignal = ((minute_ones >> 2) & 1);break;} // min 4
		case 7 : { wwvbSignal = ((minute_ones >> 1) & 1);break;} // min 2
		case 8 : { wwvbSignal = (minute_ones & 1);break;}		 // min 1
		
		case 9 : { wwvbSignal = 2;break;}
		
		case 12 : { wwvbSignal = ((hour_tens >> 1) & 1);break;} // hour 20
		case 13 : { wwvbSignal = ((hour_tens >> 0) & 1);break;} // hour 10
		
		case 15 : { wwvbSignal = ((hour_ones >> 4) & 1);break;} // hour 8
		case 16 : { wwvbSignal = ((hour_ones >> 2) & 1);break;} // hour 4
		case 17 : { wwvbSignal = ((hour_ones >> 1) & 1);break;} // hour 2
		case 18 : { wwvbSignal = (hour_ones & 1);break;}		// hour 1
		
		case 19: { wwvbSignal = 2;break;}
		
		case 26: { wwvbSignal = 1;break;}  //
		case 27: { wwvbSignal = 1;break;}  // Day of year 60
		case 29: { wwvbSignal = 2;break;}  //
		
		case 31: { wwvbSignal = 1;break;}  //
		case 32: { wwvbSignal = 1;break;}  // Day of year 6
		case 37: { wwvbSignal = 1;break;}  //
		case 39: { wwvbSignal = 2;break;}
		
		case 42: { wwvbSignal = 1;break;}  //
		case 43: { wwvbSignal = 1;break;}  // DUT1 = 0.3
		case 49: { wwvbSignal = 2;break;}
		
		case 50: { wwvbSignal = 1;break;}  // Year = 08
		case 55: { wwvbSignal = 1;break;}  // Leap year = True
		case 59: { wwvbSignal = 2;break;}
		
		default: { wwvbSignal = 0;break;}	
	}
	
	switch (wwvbSignal) {
		case 0: {
        //Serial.println("Signal Disabled 0");
		// 0 (0.2s reduced power)
		analogWrite(A0, 0);

    // TimerBit0
    timerRestart(TimerBit0);
    timerAlarmEnable(TimerBit0);
    }
		 break;
		case 1: {
        //Serial.println("Signal Disabled 1");
		// 1 (0.5s reduced power)
		analogWrite(A0, 0);

    // TimerBitMarker
    timerRestart(TimerBit1);
    timerAlarmEnable(TimerBit1);
    }
		 break;
		case 2: {
        //Serial.println("Signal Disabled Marker");

		// Marker (0.8s reduced power)
		analogWrite(A0, 0);

    // TimerBitMarker
    timerRestart(TimerBitMarker);
    timerAlarmEnable(TimerBitMarker);
    }
		 break;
}
	
		slot++;			// Advance data slot in minute data packet
		if (slot == 60) {
			slot = 0; // Reset slot to 0 if at 60 seconds
			minute_ones++; // Advance minute count
		}
	
  
  //Serial.println(messageSignal + wwvbSignal);
  //Serial.println(messageSlot + slot);
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  pinMode(A0, OUTPUT);

  TimerSecond = timerBegin(0, 80, true);
  TimerBitMarker = timerBegin(1, 80, true);
  TimerBit0 = timerBegin(2, 80, true);
  TimerBit1 = timerBegin(3, 80, true);
  
  timerAttachInterrupt(TimerSecond, &TimerSecond_ISR, true);
  timerAttachInterrupt(TimerBit0, &TimerSignalReenable_ISR, true);
  timerAttachInterrupt(TimerBit1, &TimerSignalReenable_ISR, true);
  timerAttachInterrupt(TimerBitMarker, &TimerSignalReenable_ISR, true);

  // Set timer alarm to trigger every second (1,000,000 µs)
  timerAlarmWrite(TimerSecond, 1000000, true);  // true = repeat
  timerAlarmWrite(TimerBit0, 200000, false);  // true = repeat
  timerAlarmWrite(TimerBit1, 500000, false);  // true = repeat
  timerAlarmWrite(TimerBitMarker, 800000, false);  // true = repeat

  timerAlarmEnable(TimerSecond);

  analogWriteFrequency(60000);

  // Start 60KHz signal
  analogWrite (A0,127);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(LED, HIGH);
  //Serial.println("LED is on");
  delay(1000);
  digitalWrite(LED, LOW);
  //Serial.println("LED is off");
  delay(1000);
}

// https://lastminuteengineers.com/esp32-pwm-tutorial/

/*
#include <Arduino.h>

hw_timer_t *timer = NULL;
volatile bool flag = false;

void IRAM_ATTR onTimer() {
  flag = true;  // Set the flag to true when the timer triggers
}

void setup() {
  Serial.begin(115200);
  
  // Initialize the timer
  timer = timerBegin(0, 80, true);  // Use timer 0, prescaler 80 (1 µs tick), count up
  
  // Attach onTimer function to timer
  timerAttachInterrupt(timer, &onTimer, true);
  
  // Set timer alarm to trigger every second (1,000,000 µs)
  timerAlarmWrite(timer, 1000000, true);  // true = repeat
  
  // Enable the alarm
  timerAlarmEnable(timer);
}

void loop() {
  if (flag) {
    Serial.println("Timer triggered!");
    flag = false;  // Reset the flag
  }
  // Other code can go here and will not be blocked by the timer
}

*/