#define SDA_PIN 0
#define SCL_PIN 2

#include <Wire.h>
#include <BME280.h>
#include <ESP8266WiFi.h>

ADC_MODE(ADC_VCC);

BME280 bme;
bool metricUnit = true;
uint8_t pressureUnit = 1; // unit: B000 = Pa, B001 = hPa, B010 = Hg, B011 = atm, B100 = bar, B101 = torr, B110 = N/m^2, B111 = psi

const int bmeRetryInterval = 2*1000;
uint32_t bmeErrorCount = 0;

WiFiClient client;
const char* wifiSsid = "WIFISSID";
const char* wifiPass = "WIFIPASS";
const char* tsServer = "api.thingspeak.com";
const char* tsApiKey = "THINGSPEAKKEY";
const long tsInterval = 300 * 1000;
unsigned long nextTsUpdate = 0;

const long wifiInterval = 500;
unsigned long lastWifi = 0;
bool isConnected = false;

const long timeout = 30 * 1000;

void doDeepSleep() {
  ESP.rtcUserMemoryWrite(0, &bmeErrorCount, sizeof(bmeErrorCount));
  Serial.print("DeepSleep(us):");
  long sleepTime = (tsInterval - millis()) * 1000;
  if (sleepTime < 1){
    sleepTime = 1;
  }
  Serial.println(sleepTime);
  ESP.deepSleep(sleepTime);
}

void setup() {
  ESP.rtcUserMemoryRead(0, &bmeErrorCount, sizeof(bmeErrorCount));
  Serial.begin(9600);
  Wire.begin(SDA_PIN, SCL_PIN);
  while(!bme.begin()){
    Serial.println("Could not find BME280 sensor!");
    delay(1000);
  }
  WiFi.begin(wifiSsid, wifiPass);
  Serial.println("Start");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (lastWifi + wifiInterval < millis()){
      lastWifi = millis();
      Serial.print("Connecting WiFi:");
      Serial.println(wifiSsid);
    }
    if (timeout < millis()){
      doDeepSleep();
    }
    return;
  }

  if (nextTsUpdate < millis()){
    client.stop();
    isConnected = false;
    Serial.println("Reading...");

    /// BME280
    float temp(NAN), humi(NAN), pres(NAN);
    bme.ReadData(pres, temp, humi, metricUnit, pressureUnit);
    if (isnan(temp) || isnan(pres) || isnan(humi)) {
      Serial.print("BME280 Read Error, counted: ");
      Serial.println(bmeErrorCount);
      bmeErrorCount++;
      nextTsUpdate = millis() + bmeRetryInterval;
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

    /// TS Data
    String postStr = "api_key=";
    postStr += tsApiKey;
    postStr +="&field1=";
    postStr += String(temp);
    postStr +="&field2=";
    postStr += String(humi);
    postStr +="&field3=";
    postStr += String(pres);
    postStr +="&field5=";
    postStr += String(dp);
    postStr +="&field6=";
    postStr += String(hi);
    postStr +="&field7=";
    postStr += String(bmeErrorCount);
    postStr +="&field8=";
    postStr += String(vcc);
    
    bmeErrorCount = 0;
    nextTsUpdate = millis() + tsInterval;
    
    Serial.println(postStr);
      
    Serial.print("Connecting to ");
    Serial.println(tsServer);
    if (client.connect(tsServer,80)) {
      isConnected = true;
      
      Serial.print("Sending data...");
      
      client.println("POST /update HTTP/1.1");
      client.print("Host: ");
      client.println(tsServer);
      client.println("Connection: close");
      client.println("Content-Type: application/x-www-form-urlencoded");
      client.print("Content-Length: ");
      client.println(postStr.length());
      client.println();
      client.print(postStr);
      
      Serial.println("Done");
    } else {
      Serial.println("Connection error");
    }
    return;
  }
  
  if (isConnected){
    if (client.connected()){
      while (client.available()){
        Serial.print(client.read());
      }
    } else {
      client.stop();
      Serial.println();
      Serial.println("Disconnected.");
      isConnected = false;
    }
    return;
  }
  
  if (timeout < millis()){
    doDeepSleep();
  }
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

