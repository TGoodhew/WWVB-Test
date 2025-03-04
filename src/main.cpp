
/*
  WWVB Emulator for Adafruit Huzzah32 Featherboard (ESP32)

  There has been construction up the hill from me and this has caused the WWVB signal to be degraded all across my house except for one rear corner.
  Every daylight savings change I need to cycle my atomic clocks through this corner to get them updated. The goal of this emulator is to grab the current
  time via NTP and then create a local signal that my clocks can sync to.

  Change log:

    0.1   Get Blinky running using PlatformIO and the Arduino framework on the ESP32
    0.2   Create a first version using the code generation from https://www.instructables.com/WWVB-radio-time-signal-generator-for-ATTINY45-or-A/
    0.3   Use ESP32 timers to enable tweaking of the modulation to match the proper signal timing
    0.4   Added encoding to create the bit patterns for Years, Days, Hours & Minutes from the system time converted to UTC
    0.5   Added BLE WiFi provisioning using the pattern and IDF from https://github.com/deploythefleet/arduino_ble_provisioning
    0.6   Added NTP call to get UTC time
*/

// Defines
//
// Define WWVBDEBUG if you want to see serial output of the WWVB signal as it is created with each complete signal on one line
//      M for marker bits
//      0 for 0 bits
//      1 for 1 bits
#define WWVBDEBUG
#define LED 13 // Flash the led to show the ESP32 is up and running

// Inlcude files
#include <Arduino.h>
#include "WiFiProv.h"
#include "WiFi.h"
#include "Timezone.h"
#include "apps/sntp/sntp.h"

// Function Prototypes - I wanted to keep this as a single file if people wanted to grab it and drop it into their projects
void encodeYear(uint16_t year, uint8_t *signal);
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal);
void encodeHour(uint8_t hour, uint8_t *signal);
void encodeMinute(uint8_t minute, uint8_t *signal);
void setMarkersAndIndicators(uint8_t *signal);
void setDUT1(uint8_t *signal);
void setLeapYear(uint16_t year, uint8_t *signal);
void setLeapSecond(bool IsLeap, uint8_t *signal);
void setDST(bool IsDST, uint8_t *signal);
uint16_t BitsEncoder(uint16_t n);
void IRAM_ATTR TimerSignalReenable_ISR();
void IRAM_ATTR TimerSecond_ISR();
void SysProvEvent(arduino_event_t *sys_event);
void printESPTime();
bool isLeapYear(int year);
void calculateDSTDays(int year, int *startDay, int *endDay);
bool isDaylightSavingTime(int year, int daysPassed);

// WWVB related
const char *ntpServer = "pool.ntp.org";
uint8_t WWVBArray[60] = {0};
volatile uint8_t slot = 0;

// WiFi Provisioning
bool is_provisioned = false;
bool timer_Enabled = false;

// Bit & Marker timers
hw_timer_t *TimerBit0 = NULL;
hw_timer_t *TimerBit1 = NULL;
hw_timer_t *TimerBitMarker = NULL;

// One Second timer
hw_timer_t *TimerSecond = NULL;

// All the bit/marker timers just reenable the 50%^ duty cycle of the 60KHz signal
void IRAM_ATTR TimerSignalReenable_ISR()
{
    analogWrite(A0, 127);
}

// This routine based on sbmull's code for the ATTINY - It generates the correct bit values for each second of the emulated signal
void IRAM_ATTR TimerSecond_ISR()
{
    switch (WWVBArray[slot])
    {
    case 0:
    {
        #ifdef WWVBDEBUG
        Serial.print("0");
        #endif

        // 0 (0.2s reduced power)
        analogWrite(A0, 0);

        // TimerBit0
        timerRestart(TimerBit0);
        timerAlarmEnable(TimerBit0);
    }
    break;
    case 1:
    {
        #ifdef WWVBDEBUG
        Serial.print("1");
        #endif

        // 1 (0.5s reduced power)
        analogWrite(A0, 0);

        // TimerBitMarker
        timerRestart(TimerBit1);
        timerAlarmEnable(TimerBit1);
    }
    break;
    case 2:
    {
        #ifdef WWVBDEBUG
        Serial.print("M");
        #endif

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
        slot = 0; // Reset slot to 0 if at 60 seconds
        #ifdef WWVBDEBUG
        Serial.println();
        #endif
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED, OUTPUT);
    pinMode(A0, OUTPUT);

    // BLE WiFi Enrollment
    WiFi.onEvent(SysProvEvent);
    
    // TODO: change the PIN and device prefix here if desired. The last two parameters are
    // used in the provisioning app.
    WiFiProv.beginProvision(WIFI_PROV_SCHEME_BLE, WIFI_PROV_SCHEME_HANDLER_FREE_BTDM, WIFI_PROV_SECURITY_1, "abcd1234", "PROV_WWVB");

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

    // The WWVB signal is a 60KHz AM moduleted signal (there is a phase modulated option but this emulator is AM only)
    analogWriteFrequency(60000);

    // Start 60KHz signal
    analogWrite(A0, 127);
}

void loop()
{
    time_t rawtime;
    struct tm *utcTime;

    // Wait for the device to complete WiFi provisioning
    if (is_provisioned)
    {
        time(&rawtime);

        utcTime = gmtime(&rawtime);

        // Using the current UTC time fill in the WWVBArray
        encodeYear(utcTime->tm_year + 1900, WWVBArray);
        encodeDayOfYear(utcTime->tm_yday + 1, WWVBArray);
        encodeHour(utcTime->tm_hour, WWVBArray);
        encodeMinute(utcTime->tm_min, WWVBArray);
        setMarkersAndIndicators(WWVBArray);
        setDUT1(WWVBArray); // We're ignoring DUT1 as it has been deprecated and not used in this scenario
        setLeapYear(utcTime->tm_year + 1900, WWVBArray);
        setLeapSecond(false, WWVBArray); // Ignore leap seconds in this scenario
        setDST(isDaylightSavingTime(utcTime->tm_year + 1900, utcTime->tm_yday + 1), WWVBArray);

        // Enable the seconds timer to start sending the WWVB signal after NTP has synced
        if (!timer_Enabled)
        {
            configTime(0, 0, ntpServer);

            // This only works due to the specific implementation of getLocalTime() - https://github.com/espressif/arduino-esp32/blob/7485c653bb949fd182d1eaa46f53f7947b348149/cores/esp32/esp32-hal-time.c#L114
            // someone added a check on the year value for some reason (must be greater than 2016) and as the RTC is initialize to epoch (1900) the call will fail until NTP syncs.
            // A better way to do this would be to use the IDF call to wait for the sync message but this works for me and was way simpler.
            while (!getLocalTime(utcTime))
            {
                delay(100);
            }

            timerAlarmEnable(TimerSecond);
            timer_Enabled = true;
        }

        // Flash the led to show the ESP32 is up and running
        digitalWrite(LED, HIGH);
        delay(1000);
        digitalWrite(LED, LOW);
        delay(1000);
    }
    else
    {
        // Hold the LED on until provisioned
        digitalWrite(LED, HIGH);
        Serial.println("Waiting for Wi-Fi credentials. Open app to get started.");
        delay(5000);
    }
}

// This rotuine takes the input value and then breaks it out in the individual BCD pattern that the WWVB format expects
uint16_t BitsEncoder(uint16_t n)
{
    uint16_t result = 0;

    const uint8_t div1 = n / 100;
    const uint8_t div2 = (n - (div1 * 100)) / 10;
    const uint8_t mod = n % 10;

    result = (div1 & 0xF) << 8;
    result |= (div2 & 0xF) << 4;
    result |= (mod & 0xF);

    return result;
}

// WWVB Expects year to be in 8 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeYear(uint16_t year, uint8_t *signal)
{
    int yearBCD = year % 100;
    uint16_t bitsResult = BitsEncoder(yearBCD);

    signal[45] = (bitsResult & 0x80) >> 7;
    signal[46] = (bitsResult & 0x40) >> 6;
    signal[47] = (bitsResult & 0x20) >> 5;
    signal[48] = (bitsResult & 0x10) >> 4;
    signal[50] = (bitsResult & 0x08) >> 3;
    signal[51] = (bitsResult & 0x04) >> 2;
    signal[52] = (bitsResult & 0x02) >> 1;
    signal[53] = (bitsResult & 0x01);
}

// WWVB Expects the day of the year to be in 10 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal)
{
    uint16_t bitsResult = BitsEncoder(dayOfYear);

    signal[22] = (bitsResult & 0x0200) >> 9;
    signal[23] = (bitsResult & 0x0100) >> 8;
    signal[25] = (bitsResult & 0x0080) >> 7;
    signal[26] = (bitsResult & 0x0040) >> 6;
    signal[27] = (bitsResult & 0x0020) >> 5;
    signal[28] = (bitsResult & 0x0010) >> 4;
    signal[30] = (bitsResult & 0x0008) >> 3;
    signal[31] = (bitsResult & 0x0004) >> 2;
    signal[32] = (bitsResult & 0x0002) >> 1;
    signal[33] = (bitsResult & 0x0001);
}

// WWVB Expects the hour to be in 6 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeHour(uint8_t hour, uint8_t *signal)
{
    uint16_t bitsResult = BitsEncoder(hour);

    signal[12] = (bitsResult & 0x20) >> 5;
    signal[13] = (bitsResult & 0x10) >> 4;
    signal[15] = (bitsResult & 0x08) >> 3;
    signal[16] = (bitsResult & 0x04) >> 2;
    signal[17] = (bitsResult & 0x02) >> 1;
    signal[18] = (bitsResult & 0x01);
}

// WWVB Expects minutes to be in 7 bit BCD - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void encodeMinute(uint8_t minute, uint8_t *signal)
{
    uint16_t bitsResult = BitsEncoder(minute);

    signal[1] = (bitsResult & 0x40) >> 6;
    signal[2] = (bitsResult & 0x20) >> 5;
    signal[3] = (bitsResult & 0x10) >> 4;
    signal[5] = (bitsResult & 0x08) >> 3;
    signal[6] = (bitsResult & 0x04) >> 2;
    signal[7] = (bitsResult & 0x02) >> 1;
    signal[8] = (bitsResult & 0x01);
}

// The WWVB signal has certain marker and bits that are always set to either a marker bit or a zero
void setMarkersAndIndicators(uint8_t *signal)
{
    signal[0] = 2;  // Position marker
    signal[9] = 2;  // Position marker
    signal[19] = 2; // Position marker
    signal[29] = 2; // Position marker
    signal[39] = 2; // Position marker
    signal[49] = 2; // Position marker
    signal[59] = 2; // Position marker

    signal[4] = 0;  // Always 0
    signal[10] = 0; // Always 0
    signal[11] = 0; // Always 0
    signal[14] = 0; // Always 0
    signal[20] = 0; // Always 0
    signal[21] = 0; // Always 0
    signal[24] = 0; // Always 0
    signal[34] = 0; // Always 0
    signal[35] = 0; // Always 0
    signal[44] = 0; // Always 0
    signal[54] = 0; // Always 0
}

// WWVB once supported celestial navigation uses but as it was deprecated and this scenario doesn't need it then just set those bits to 0 - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setDUT1(uint8_t *signal)
{
    // DUT1 is obselete, it was used for celestial navigation
    signal[36] = 0;
    signal[37] = 0;
    signal[38] = 0;
    signal[40] = 0;
    signal[41] = 0;
    signal[42] = 0;
    signal[43] = 0;
}

// If you use the current year and mktime to set a date it will tell you if it is a leap year or not
// I can't find where I got this code from so apologies for not crediting it to the appropriate person
void setLeapYear(uint16_t year, uint8_t *signal)
{
    struct tm time_in = {0};
    time_in.tm_year = year - 1900;
    time_in.tm_mon = 2;  // March (0-based: January is 0)
    time_in.tm_mday = 0; // Zero day of March will roll back to the last day of February

    mktime(&time_in);

    // If mktime leaves the day as 29 then it is a leap year
    if (time_in.tm_mday == 29)
        signal[55] = 1;
    else
        signal[55] = 0;
}

// WWVB Expects this bit for a leap second, I don't believe it is useful in this scenario but setting it just in case - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setLeapSecond(bool IsLeap, uint8_t *signal)
{
    if (IsLeap)
        signal[56] = 1;
    else
        signal[56] = 0;
}

// WWVB Expects to have a DST bit set - It allows for warning of DST but we're ignoring that in this scenario - https://en.wikipedia.org/wiki/WWVB#Amplitude-modulated_time_code
void setDST(bool IsDST, uint8_t *signal)
{
    if (IsDST)
    {
        signal[57] = 1;
        signal[58] = 1;
    }
    else
    {
        signal[57] = 0;
        signal[58] = 0;
    }
}

// This code is to react to the WiFi provisioning using the ESP app. It is only called once per device unless you explicitly clear out the WiFi credentials on the ESP
// It is from this repo - https://github.com/deploythefleet/arduino_ble_provisioning
void SysProvEvent(arduino_event_t *sys_event)
{
    switch (sys_event->event_id)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("\nConnected IP address : ");
        Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
        is_provisioned = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("\nDisconnected. Connecting to the AP again... ");
        break;
    case ARDUINO_EVENT_PROV_START:
        Serial.println("\nProvisioning started\nGive Credentials of your access point using the Espressif BLE Provisioning App");
        break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
    {
        Serial.println("\nReceived Wi-Fi credentials");
        Serial.print("\tSSID : ");
        Serial.println((const char *)sys_event->event_info.prov_cred_recv.ssid);
        Serial.print("\tPassword : ");
        Serial.println((char const *)sys_event->event_info.prov_cred_recv.password);
        break;
    }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
    {
        Serial.println("\nProvisioning failed!\nPlease reset to factory and retry provisioning\n");
        if (sys_event->event_info.prov_fail_reason == WIFI_PROV_STA_AUTH_ERROR)
            Serial.println("\nWi-Fi AP password incorrect");
        else
            Serial.println("\nWi-Fi AP not found....Add API \" nvs_flash_erase() \" before beginProvision()");
        break;
    }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
        Serial.println("\nProvisioning Successful");
        break;
    case ARDUINO_EVENT_PROV_END:
        Serial.println("\nProvisioning Ends");
        break;
    default:
        break;
    }
}

// The following is AI generated code
// This should be checked to see if it is actually works as expected
// I just needed something quick to fill this DST calc

// Function to determine if a year is a leap year
bool isLeapYear(int year)
{
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        return true;
    }
    return false;
}

// Function to calculate the start and end days for DST in a given year
void calculateDSTDays(int year, int *startDay, int *endDay)
{
    bool leap = isLeapYear(year);
    // Calculate the second Sunday in March
    int daysInFeb = leap ? 29 : 28;
    int daysUntilMarch = 31 + daysInFeb;
    *startDay = daysUntilMarch + (14 - ((year + year / 4 - year / 100 + year / 400 + daysUntilMarch) % 7));

    // Calculate the first Sunday in November
    int daysUntilNov = 31 + daysInFeb + 31 + 30 + 31 + 30 + 31 + 31 + 30;
    *endDay = daysUntilNov + (7 - ((year + year / 4 - year / 100 + year / 400 + daysUntilNov) % 7));
}

// Function to check if the current day is within DST period
bool isDaylightSavingTime(int year, int daysPassed)
{
    int startDay, endDay;
    calculateDSTDays(year, &startDay, &endDay);
    return (daysPassed >= startDay && daysPassed < endDay);
}
