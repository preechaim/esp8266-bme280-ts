#include <Wire.h>
#include <BME280I2C.h>
#include <ESP8266WiFi.h>
#include "config.h"

ADC_MODE(ADC_VCC);

//// BME
BME280I2C bme;
bool metricUnit = true;
uint8_t pressureUnit = 1; // unit: B000 = Pa, B001 = hPa, B010 = Hg, B011 = atm, B100 = bar, B101 = torr, B110 = N/m^2, B111 = psi
unsigned long nextRead = BME_DISABLE + 2000; // wait for sensor to be stabilized
bool isRead = false;

//// Wifi connection
unsigned long nextWifiReport = 0;

//// Wifi client
WiFiClient client;
String postStr = "";
bool isConnected = false;

//// RTC User Data
struct {
  uint32_t bmeErrorCount;
  uint32_t timeoutCount;
  uint32_t crc32;
} rtcData;

void setup() {
  pinMode(BME_PWR_PIN, OUTPUT);
  digitalWrite(BME_PWR_PIN, HIGH);
  Serial.begin(9600);
  Serial.println("Waking up.");
  delay(BME_DISABLE);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Wire.begin(SDA_PIN, SCL_PIN);
  bme.begin();
  
  ESP.rtcUserMemoryRead(0, (uint32_t*) &rtcData, sizeof(rtcData));
  if (calcCRC32(((uint8_t*) &rtcData), sizeof(rtcData)-4) != rtcData.crc32){
    Serial.println("RTC data has wrong CRC, discarded.");
    rtcData.bmeErrorCount = 0;
    rtcData.timeoutCount = 0;
  } else {
    Serial.println("RTC data:");
    Serial.print("BME Error Count:");
    Serial.println(rtcData.bmeErrorCount);
    Serial.print("Awake Timeout Count:");
    Serial.println(rtcData.timeoutCount);
  }
}

void loop() {
  if (AWAKE_TIMEOUT < millis()){
    Serial.println("Work is not finished, but it's time to sleep.");
    rtcData.timeoutCount++;
    goDeepSleep();
  }
  
  if (!isRead && nextRead < millis()){
    Serial.println("Reading...");

    /// BME280
    float temp(NAN), humi(NAN), pres(NAN);
    bme.read(pres, temp, humi, metricUnit, pressureUnit);
    if (isnan(temp) || isnan(pres) || isnan(humi)) {
      Serial.println("BME280 Read Error");
      rtcData.bmeErrorCount++;
      nextRead = millis() + BME_INTERVAL;
      return;
    }

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
    float dp = computeDewPoint(temp, humi, metricUnit);
    float hi = computeHeatIndex(temp, humi, metricUnit);
    Serial.print("DewPoint :");
    Serial.print(dp);
    Serial.println((metricUnit ? "C" :"F"));
    Serial.print("HeatIndex:");
    Serial.print(hi);
    Serial.println((metricUnit ? "C" :"F"));
    
    /// Input Voltage
    long vcc = ESP.getVcc();
    
    Serial.print("Vcc :");
    Serial.print(vcc);
    Serial.println("mV");

    Serial.print("BME Error Count:");
    Serial.println(rtcData.bmeErrorCount);
    Serial.print("Awake Timeout Count:");
    Serial.println(rtcData.timeoutCount);

    /// TS Data
    postStr = "api_key=";
    postStr += TS_KEY;
    postStr +="&field1=";
    postStr += String(temp);
    postStr +="&field2=";
    postStr += String(humi);
    postStr +="&field3=";
    postStr += String(pres);
    postStr +="&field4=";
    postStr += String(dp);
    postStr +="&field5=";
    postStr += String(hi);
    postStr +="&field6=";
    postStr += String(rtcData.bmeErrorCount);
    postStr +="&field7=";
    postStr += String(rtcData.timeoutCount);
    postStr +="&field8=";
    postStr += String(vcc);

    Serial.println(postStr);
    isRead = true;
    digitalWrite(BME_PWR_PIN, LOW); // Power off
  }

  if (WiFi.status() != WL_CONNECTED){
    if (nextWifiReport < millis()){
      nextWifiReport = millis() + WIFI_PRINT_INTERVAL;
      Serial.print("Connecting WiFi:");
      Serial.println(WIFI_SSID);
    }
    return;
  }

  if (!isRead){
    return;
  }

  //// We can continue below,
  //// only if both sensor reading and wifi connection are completed.

  if (!isConnected){
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
  rtcData.bmeErrorCount = 0;
  rtcData.timeoutCount = 0;

  Serial.println("Work is done. Going back to sleep.");
  goDeepSleep();
}

void goDeepSleep(){
  rtcData.crc32 = calcCRC32(((uint8_t*) &rtcData), sizeof(rtcData)-4);
  ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

  Serial.print("Deep Sleep (us):");
  long sleepTime = (TS_INTERVAL - millis()) * 1000;
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

