#include <Wire.h>
#include <BME280I2C.h>
#include <ESP8266WiFi.h>
#include "config.h"

ADC_MODE(ADC_VCC);
long vcc = 0;

//// BME
BME280I2C bme;
float temp(NAN), humi(NAN), pres(NAN);
float dp(NAN), hi(NAN);
bool metricUnit = true;
uint8_t pressureUnit = 1; // unit: B000 = Pa, B001 = hPa, B010 = Hg, B011 = atm, B100 = bar, B101 = torr, B110 = N/m^2, B111 = psi
unsigned long nextRead = 1000;
bool isRead = false;

//// Wifi connection
int32_t rssi = 0;
unsigned long nextWifiReport = 0;

//// Wifi client
WiFiClient client;
String postStr = "";
bool isConnected = false;

//// RTC User Data
struct {
  uint32_t timeoutCount;
  uint32_t crc32;
} rtcData;

void setup() {
  Serial.begin(9600);
  Serial.println("Waking up.");

  delay(1000);

  /// Input Voltage
  vcc = ESP.getVcc();
  Serial.print("Vcc:");
  Serial.print(vcc);
  Serial.println("mV");

  /// Read RTC Memory
  ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData));
  if (calcCRC32(((uint8_t*) &rtcData), sizeof(rtcData)-4) != rtcData.crc32){
    Serial.println("RTC data has wrong CRC, discarded.");
    rtcData.timeoutCount = 0;
  } else {
    Serial.println("RTC data:");
    Serial.print("Awake Timeout Count:");
    Serial.println(rtcData.timeoutCount);
  }

  if (vcc <= BATT_CRT){
    Serial.print("Battery critical, Go sleep!");
    goDeepSleep();
  }

  /// Init WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  /// Init BME
  Wire.begin(SDA_PIN, SCL_PIN);
  bme.begin();
}

void loop() {
  if (millis() - AWAKE_TIMEOUT < 0){
    Serial.println("Work is not finished, but it's time to sleep.");
    rtcData.timeoutCount++;
    goDeepSleep();
  }

  if (!isRead && millis() - nextRead < 0){
    Serial.println("Reading...");

    /// BME280
    bme.read(pres, temp, humi, metricUnit, pressureUnit);
    Serial.print("Temp:");
    Serial.print(temp);
    Serial.println((metricUnit ? "C" :"F"));
    Serial.print("Humi:");
    Serial.print(humi);
    Serial.println("%RH");
    Serial.print("Pres:");
    Serial.print(pres);
    Serial.println("hPa");

    // Calculate HeatIndex and DewPoint
    dp = computeDewPoint(temp, humi, metricUnit);
    hi = computeHeatIndex(temp, humi, metricUnit);
    Serial.print("DewPoint:");
    Serial.print(dp);
    Serial.println((metricUnit ? "C" :"F"));
    Serial.print("HeatIndex:");
    Serial.print(hi);
    Serial.println((metricUnit ? "C" :"F"));

    Serial.print("Awake Timeout Count:");
    Serial.println(rtcData.timeoutCount);

    isRead = true;
  }

  if (WiFi.status() != WL_CONNECTED){
    if (millis() - nextWifiReport > 0){
      nextWifiReport = millis() + WIFI_PRINT_INTERVAL;
      Serial.print("Connecting WiFi:");
      Serial.println(WIFI_SSID);
    }
    return;
  }

  if (!rssi){
    /// WiFi RSSI
    rssi = WiFi.RSSI();
    Serial.print("Rssi:");
    Serial.print(rssi);
    Serial.println("dBm");
  }

  if (!isRead){
    return;
  }

  //// We can continue below,
  //// only if both sensor reading and wifi connection are completed.

  if (!isConnected){
    /// TS Data
    postStr = "api_key=";
    postStr += TS_KEY;
    if (!isnan(temp)){
      postStr +="&field1=";
      postStr += String(temp);
    }
    if (!isnan(humi)){
      postStr +="&field2=";
      postStr += String(humi);
    }
    if (!isnan(pres)){
      postStr +="&field3=";
      postStr += String(pres);
    }
    if (!isnan(dp)){
      postStr +="&field4=";
      postStr += String(dp);
    }
    if (!isnan(hi)){
      postStr +="&field5=";
      postStr += String(hi);
    }
    postStr +="&field6=";
    postStr += String(rtcData.timeoutCount);
    postStr +="&field7=";
    postStr += String(rssi);
    postStr +="&field8=";
    postStr += String(vcc);

    Serial.print("Data:");
    Serial.println(postStr);

    Serial.print("Connecting to ");
    Serial.print(TS_HOST);
    Serial.print(":");
    Serial.println(TS_PORT);
    if (client.connect(TS_HOST,TS_PORT)){

      Serial.print("Sending data...");

      client.println("POST /update HTTP/1.1");
      client.print("Host: ");
      client.println(TS_HOST);
      client.println("Connection: close");
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.print("Content-Length: ");
      client.println(postStr.length());
      client.println();
      client.print(postStr);

      isConnected = true;
      Serial.println("Done");
    } else {
      Serial.println("Connection error");
      return;
    }
  }

  if (client.connected()){
    while (client.available()){
      Serial.print(client.read());
    }
    return;
  }

  client.stop();
  Serial.println();
  Serial.println("Disconnected.");

  // Reset
  rtcData.timeoutCount = 0;

  Serial.println("Work is done. Going back to sleep.");
  goDeepSleep();
}

void goDeepSleep(){
  rtcData.crc32 = calcCRC32(((uint8_t*) &rtcData), sizeof(rtcData)-4);
  ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

  Serial.print("Deep Sleep (us):");
  long sleepTime = (INTERVAL_NRM - millis()) * 1000;
  if (vcc <= BATT_LOW){
    sleepTime = (INTERVAL_LOW - millis()) * 1000;
  }
  if (sleepTime < 1){
    sleepTime = 1;
  }
  Serial.println(sleepTime);
  ESP.deepSleep(sleepTime);
}

uint32_t calcCRC32(const uint8_t *data, size_t length){
  uint32_t crc = 0xffffffff;
  while (length--){
    uint8_t c = *data++;
    for (uint32_t i=0x80; i > 0; i>>=1){
      bool bit = crc & 0x80000000;
      if (c & i){
        bit = !bit;
      }
      crc <<= 1;
      if (bit){
        crc ^= 0x04c11db7;
      }
    }
  }
  return crc;
}

float computeDewPoint(float temp, float humi, bool metricUnit) {
  // Equations courtesy of Brian McNoldy
  // http://andrew.rsmas.miami.edu;

  float dp = NAN;

  if (!metricUnit){
    temp = convertFtoC(temp);
  }

  dp = 243.04 * (log(humi/100.0) + ((17.625 * temp)/(243.04 + temp)))
      /(17.625 - log(humi/100.0) - ((17.625 * temp)/(243.04 + temp)));

  return metricUnit ? dp : convertCtoF(dp) ;
}

float computeHeatIndex(float temp, float humi, bool metricUnit) {
  // Using both Rothfusz and Steadman's equations
  // http://www.wpc.ncep.noaa.gov/html/heatindex_equation.shtml

  float hi;

  if (metricUnit){
    temp = convertCtoF(temp);
  }

  hi = 0.5 * (temp + 61.0 + ((temp - 68.0) * 1.2) + (humi * 0.094));

  if (hi > 79) {
    hi = -42.379 +
             2.04901523 * temp +
            10.14333127 * humi +
            -0.22475541 * temp*humi +
            -0.00683783 * pow(temp, 2) +
            -0.05481717 * pow(humi, 2) +
             0.00122874 * pow(temp, 2) * humi +
             0.00085282 * temp*pow(humi, 2) +
            -0.00000199 * pow(temp, 2) * pow(humi, 2);

    if ((humi < 13) && (temp >= 80.0) && (temp <= 112.0)){
      hi -= ((13.0 - humi) * 0.25) * sqrt((17.0 - abs(temp - 95.0)) * 0.05882);
    } else if ((humi > 85.0) && (temp >= 80.0) && (temp <= 87.0)){
      hi += ((humi - 85.0) * 0.1) * ((87.0 - temp) * 0.2);
    }
  }

  return metricUnit ? convertFtoC(hi) : hi ;
}

float convertCtoF(float c) {
  return c * 1.8 + 32;
}

float convertFtoC(float f) {
  return (f - 32) * 0.55555;
}

