#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <Time.h>           // Use Time.h instead of TimeLib.h
#include <Timezone.h>    // https://github.com/JChristensen/Timezone
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "secrets.h"

const char *ssid     = WIFI_SSID;
const char *password = WIFI_PASSWORD;

#define PIN            2
#define NUMPIXELS      100
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};    // Daylight time = UTC - 6 hours
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};     // Standard time = UTC - 7 hours
Timezone usMountain(usMDT, usMST);

char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 30000);  // Using UTC time, offset will be handled by Timezone library

// Brightness and sunrise/sunset variables
int normalBrightness = 128;
int dimBrightness = 20;
int currentBrightness = 128;
int sunriseHour = 6;
int sunriseMinute = 0;
int sunsetHour = 18;
int sunsetMinute = 0;

float latitude = 39.7392;  // Default: Denver, CO
float longitude = -104.9903;
bool locationFetched = false;

void fetchLocationFromIP() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient wifiClient;
    HTTPClient http;
    http.begin(wifiClient, "http://ip-api.com/json");
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      int latIndex = payload.indexOf("\"lat\":");
      int lonIndex = payload.indexOf("\"lon\":");
      if (latIndex > 0 && lonIndex > 0) {
        latitude = payload.substring(latIndex + 6, payload.indexOf(',', latIndex)).toFloat();
        longitude = payload.substring(lonIndex + 6, payload.indexOf(',', lonIndex)).toFloat();
        locationFetched = true;
        Serial.print("Fetched location: lat=");
        Serial.print(latitude, 6);
        Serial.print(", lon=");
        Serial.println(longitude, 6);
      }
    }
    http.end();
  }
}

void fetchSunriseSunset(float latitude, float longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient wifiClient;
    HTTPClient http;
    String url = String("http://api.sunrise-sunset.org/json?lat=") + String(latitude, 6) + "&lng=" + String(longitude, 6) + "&formatted=0";
    http.begin(wifiClient, url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      int sunriseIndex = payload.indexOf("sunrise\":\"");
      int sunsetIndex = payload.indexOf("sunset\":\"");
      if (sunriseIndex > 0 && sunsetIndex > 0) {
        String sunriseStr = payload.substring(sunriseIndex + 10, payload.indexOf('"', sunriseIndex + 10));
        String sunsetStr = payload.substring(sunsetIndex + 9, payload.indexOf('"', sunsetIndex + 9));
        Serial.print("Fetched sunrise (UTC): "); Serial.println(sunriseStr);
        Serial.print("Fetched sunset (UTC): "); Serial.println(sunsetStr);
        sunriseHour = sunriseStr.substring(11, 13).toInt();
        sunriseMinute = sunriseStr.substring(14, 16).toInt();
        sunsetHour = sunsetStr.substring(11, 13).toInt();
        sunsetMinute = sunsetStr.substring(14, 16).toInt();
        time_t sunriseUTC = (sunriseHour * 3600) + (sunriseMinute * 60);
        time_t sunsetUTC = (sunsetHour * 3600) + (sunsetMinute * 60);
        time_t now = timeClient.getEpochTime();
        time_t sunriseLocal = usMountain.toLocal(now - (now % 86400) + sunriseUTC);
        time_t sunsetLocal = usMountain.toLocal(now - (now % 86400) + sunsetUTC);
        sunriseHour = hour(sunriseLocal);
        sunriseMinute = minute(sunriseLocal);
        sunsetHour = hour(sunsetLocal);
        sunsetMinute = minute(sunsetLocal);
        Serial.print("Sunrise (local): "); Serial.print(sunriseHour); Serial.print(":"); Serial.println(sunriseMinute);
        Serial.print("Sunset (local): "); Serial.print(sunsetHour); Serial.print(":"); Serial.println(sunsetMinute);
      }
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }
  Serial.println("\nWiFi connected!");
  WiFi.hostname("BigClock");
  timeClient.begin();

  pixels.begin();

  fetchLocationFromIP();
  fetchSunriseSunset(latitude, longitude);
}

unsigned long lastMidnightCheck = 0;
int lastDay = -1;
bool isDimmed = false;

void setAllPixelsBrightness(int brightness) {
  for (int i = 0; i < NUMPIXELS; i++) {
    uint32_t color = pixels.getPixelColor(i);
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)(color);
    // Scale color by brightness
    r = (r * brightness) / 255;
    g = (g * brightness) / 255;
    b = (b * brightness) / 255;
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

void loop() {
  timeClient.update();
  // Get UTC time and convert to local time with DST handling
  time_t utc = timeClient.getEpochTime();
  time_t local = usMountain.toLocal(utc);
  int hours24 = hour(local); // 24-hour value for logic
  int mins = minute(local);
  int secs = second(local);
  int today = day(local);

  // 12-hour conversion for display
  int hours12 = hours24;
  bool isPM = false;
  if (hours12 == 0) {
    hours12 = 12;
  } else if (hours12 >= 12) {
    if (hours12 > 12) hours12 -= 12;
    isPM = true;
  }

  // At midnight, update sunrise/sunset times for the new day
  if (today != lastDay) {
    lastDay = today;
    fetchSunriseSunset(latitude, longitude);
  }

  // Brightness logic
  bool shouldBeDim = false;
  if (hours24 < sunriseHour || (hours24 == sunriseHour && mins < sunriseMinute)) {
    shouldBeDim = true;
  } else if (hours24 > sunsetHour || (hours24 == sunsetHour && mins >= sunsetMinute)) {
    shouldBeDim = true;
  }
  if (shouldBeDim && !isDimmed) {
    currentBrightness = dimBrightness;
    isDimmed = true;
  } else if (!shouldBeDim && isDimmed) {
    currentBrightness = normalBrightness;
    isDimmed = false;
  }

  // Use hours12 for display
  int displayHours = hours12;
  // Safety: never show 0 for hours on a 12-hour clock
  if (displayHours == 0) {
    displayHours = 12;
  }
  bool leadZero = false;
  if (displayHours < 10) {
    leadZero = true;
  }

  Serial.print(daysOfTheWeek[weekday(local) - 1]);
  Serial.print(", ");
  Serial.print(displayHours);
  Serial.print(":");
  Serial.print(mins);
  Serial.print(":");
  Serial.println(second(local));

  // Print current time, sunrise, sunset, and brightness mode
  Serial.print("Current time (24h): ");
  Serial.print(hours24); Serial.print(":"); Serial.print(mins); Serial.print(":"); Serial.println(secs);
  Serial.print("Current time (12h): ");
  Serial.print(hours12); Serial.print(":"); Serial.print(mins); Serial.print(":"); Serial.print(secs);
  Serial.println(isPM ? " PM" : " AM");
  Serial.print("Today's sunrise: "); Serial.print(sunriseHour); Serial.print(":"); Serial.println(sunriseMinute);
  Serial.print("Today's sunset: "); Serial.print(sunsetHour); Serial.print(":"); Serial.println(sunsetMinute);
  Serial.print("Brightness mode (using 24h time): ");
  if (hours24 < sunriseHour || (hours24 == sunriseHour && mins < sunriseMinute) || hours24 > sunsetHour || (hours24 == sunsetHour && mins >= sunsetMinute)) {
    Serial.println("DIM");
  } else {
    Serial.println("NORMAL");
  }

  // Send Digits to Clock
  if (leadZero == true) {
    Serial.println("leading zero needed");
    Serial.print("hours: ");
    Serial.println(displayHours);
    drawdigit(0, 0, 0, 0, displayHours / 10); //Draw the first digit of the hour
  }
  else {
    Serial.println("leading zero NOT needed");
    drawdigit(0, currentBrightness, 0, 0, displayHours / 10); //Draw the first digit of the hour
  }
  drawdigit(21, currentBrightness, 0, 0, displayHours - ((displayHours / 10) * 10)); //Draw the second digit of the hour
  pixels.setPixelColor(42, pixels.Color(currentBrightness, 0, 0)); //Draw the two middle dots
  pixels.setPixelColor(43, pixels.Color(currentBrightness, 0, 0));
  drawdigit(44, currentBrightness, 0, 0, mins / 10); //Draw the first digit of the minute
  drawdigit(65, currentBrightness, 0, 0, mins - ((mins / 10) * 10)); //Draw the second digit of the minute

  delay(30000);//Request to NIST server should be separated by at least 30 seconds.

}

long utcOffset(const char* timezone) {
  // timezone should look something like: America/Denver
  // a list of timezones can be found by
  // curl "http://worldtimeapi.org/api/timezone/"
  const char* urlBase = "http://worldtimeapi.org/api/timezone/";

  // number of seconds to offset from UTC
  long offset = 0;
  long raw_offset = -25200;
  long dst_offset = 3600;

  return offset + raw_offset + dst_offset;
}

//Get current offset for time based on IP
long getTimeZoneOffset ()
{
  long offset = 0;
  
  return offset;
}

// How to draw each digit
void drawdigit(int offset, int r, int g, int b, int n)
{

  if (n == 2 || n == 3 || n == 4 || n == 5 || n == 6 || n == 8 || n == 9 ) //MIDDLE
  {
    pixels.setPixelColor(0 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(1 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(2 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(0 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(1 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(2 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 1 || n == 2 || n == 3 || n == 4 || n == 7 || n == 8 || n == 9) //TOP RIGHT
  {
    pixels.setPixelColor(3 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(4 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(5 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(3 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(4 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(5 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 3 || n == 5 || n == 6 || n == 7 || n == 8 || n == 9) //TOP
  {
    pixels.setPixelColor(6 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(7 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(8 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(6 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(7 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(8 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 4 || n == 5 || n == 6 || n == 8 || n == 9 ) //TOP LEFT
  {
    pixels.setPixelColor(9 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(10 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(11 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(9 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(10 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(11 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 6 || n == 8) //BOTTOM LEFT
  {
    pixels.setPixelColor(12 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(13 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(14 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(12 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(13 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(14 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 2 || n == 3 || n == 5 || n == 6 || n == 8 || n == 9) //BOTTOM
  {
    pixels.setPixelColor(15 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(16 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(17 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(15 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(16 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(17 + offset, pixels.Color(0, 0, 0));
  }
  if (n == 0 || n == 1 || n == 3 || n == 4 || n == 5 || n == 6 || n == 7 || n == 8 || n == 9) //BOTTOM RIGHT
  {
    pixels.setPixelColor(18 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(19 + offset, pixels.Color(r, g, b));
    pixels.setPixelColor(20 + offset, pixels.Color(r, g, b));
  }
  else
  {
    pixels.setPixelColor(18 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(19 + offset, pixels.Color(0, 0, 0));
    pixels.setPixelColor(20 + offset, pixels.Color(0, 0, 0));
  }

  pixels.show();
}
