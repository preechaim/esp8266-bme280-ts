#define WIFI_SSID "--WIFI_SSID--"
#define WIFI_PASS "--WIFI_PASS--"
#define WIFI_PRINT_INTERVAL 500

#define TS_HOST "api.thingspeak.com"
#define TS_PORT 80
#define TS_KEY "--THINGSPEAK_KEY--"
#define INTERVAL_NRM  300000 // normal wake up interval
#define INTERVAL_LOW 1200000 // wake up interval when low battery

#define AWAKE_TIMEOUT 20000 // maximum wakeup time

#define SDA_PIN 0
#define SCL_PIN 2
#define BME_INTERVAL 1000

// Setup for 1W mini solar panel + 2xAA NiMH rechargeable batteries
#define BATT_LOW 2300 // low battery voltage -> use INTERVAL_LOW
#define BATT_CRT 2200 // critical battery voltage -> go sleep
