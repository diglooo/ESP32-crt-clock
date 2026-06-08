#include "Arduino.h"
#include "video.h"
#include "graphics.h"
#include "framebuffer_gfx.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <time.h>
#include "esp_sntp.h"
#include <U8g2_for_Adafruit_GFX.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/Picopixel.h>
#include <JSON_Decoder.h>
#include <OpenWeather.h>
#include <Radar-rd-03d.h>
#include "secrets.h"
#include "BoschHomeConnect.h"


RadarRD03D radar;
unsigned long lastPersonDetectionMillis = 0;
bool person_detected_filtered = false;
unsigned long personDetectedSince   = 0; // when raw signal last went true
unsigned long personAbsentSince     = 0; // when raw signal last went false
unsigned long lastScreenDrawMillis = 1100;
#define PIN_CRT_PWR_CTRL  4

int16_t frame_cnt = 0;
// NTP server details
const char *ntpServer = "pool.ntp.org";
const char *posixTimeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; // POSIX TZ timezone + DST string https://phpsecu.re/tz/
ESP32Time rtc(0);      

unsigned long lastTimeCheck = 0;
time_t last_local_minutes = 0, actual_local_minutes = 0;
struct tm local_time_tm;// offset in seconds

uint8_t iswifiConnected = 0, iswifiConnectedLast = 0, iswifiConnectedOSR = 0;
unsigned long  lastwificheckmillis = 10000+1, lastWeatherCheckMillis=30*60000+1;
unsigned long  last_HC_SSE_poll_time=0;

//HC variables
int remainingTimeSec = 0;
char *dishwasherStateStr = (char *)malloc(64);

U8G2_FOR_ADAFRUIT_GFX gfx_renderer;
OW_Weather ow; // Weather forecast library instance
uint8_t timeIsSet=0;

int8_t rainyDays[MAX_DAYS+1] = { -1,-1,-1,-1,-1 }; // Array to hold the indices of rainy days, initialized to -1

static const char* dayNames[] = {
  "domenica",
  "lunedi",
  "martedi",
  "mercoledi",
  "giovedi",
  "venerdi",
  "sabato"
};

// Framebuffer GFX instance pointer
Framebuffer_GFX *fbGfx = nullptr;

uint8_t initWiFi()
{
  int cnt = 0;
  while (WiFi.status() != WL_CONNECTED && cnt < 60)
  {
    delay(1000);
    cnt++;
  }
  return WiFi.status() == WL_CONNECTED;
}


void ntp_sync_cb(struct timeval* t) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    rtc.setTimeStruct(timeinfo);
    timeIsSet = 1;
  }
}

void setNTPTime(const char *_posixTimeZone, const char *_ntpServer)
{
  Serial.println("NTP begin");
  setenv("TZ", _posixTimeZone, 1);
  tzset();
  if (!esp_sntp_enabled())
  {
    sntp_set_sync_interval(24 * 60 * 60 * 1000UL);
    sntp_set_time_sync_notification_cb(ntp_sync_cb);
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, _ntpServer);
    esp_sntp_init();
  }
  else
  {
    sntp_restart();
  }
}

// Convert unix timestamp to day of week string
int getDayOfWeek(time_t timestamp)
{

  struct tm* timeinfo = localtime(&timestamp);
  if (timeinfo == NULL)
    return 0;
  return timeinfo->tm_wday;
}

void getForecast( int8_t * _rainyDays)
{
  // Create the structures that hold the retrieved weather
  OW_forecast  *forecast = new OW_forecast;

  Serial.print("\nRequesting weather information from OpenWeather... ");
  ow.getForecast(forecast, OPENWEATHER_API, ow_lat, ow_lon, ow_units, ow_lang);

  for(int i=0; i<MAX_DAYS; i++)
  {
    _rainyDays[i]=-1;
  }
  int day_index = 0;

  if (forecast)
  {
    for (int d = 0; d < MAX_DAYS; d++)
    {
      int rainSlots = 0;
      int forecastDay = -1;
      for (int s = 0; s < 8; s++)
      {
        int i = d * 8 + s;
        forecastDay = getDayOfWeek(forecast->dt[i]);
        Serial.print("day      : "); Serial.println(dayNames[forecastDay]);
        Serial.print("main fore: "); Serial.println(forecast->main[i]);
        if (forecast->main[i] == "Rain")
          rainSlots++;
      }
      Serial.printf("Day %s: %d/8 rainy slots\n", dayNames[forecastDay], rainSlots);
      if (rainSlots > 4)
      {
        _rainyDays[day_index] = forecastDay;
        day_index++;
      }
    }
  }
  // Delete to free up space and prevent fragmentation as strings change in length
  delete forecast;
}

static unsigned long wifiReconnectMillis = 0;

void network_func()
{
  unsigned long currentMillis = millis();
  if(currentMillis-lastwificheckmillis >1000)
  {
    iswifiConnected=WiFi.status() == WL_CONNECTED;
    lastwificheckmillis=currentMillis;
    if(iswifiConnected)
    {
      wifiReconnectMillis = 0;
      Serial.println("Wi-Fi connected!");
    }
    else
    {
      Serial.println("Wi-Fi not connected");
      // Full disconnect+reconnect with 10s backoff to clear ASSOC_COMEBACK rejections
      if (wifiReconnectMillis == 0 || currentMillis - wifiReconnectMillis > 10000)
      {
        wifiReconnectMillis = currentMillis;
        WiFi.disconnect(false);
        delay(500);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    }
  }

  if(currentMillis - lastWeatherCheckMillis > 30*60000)
  {
    if(iswifiConnected)
    { 
      lastWeatherCheckMillis = currentMillis;
      getForecast(rainyDays);
    }

  }

  iswifiConnectedOSR=iswifiConnected & !iswifiConnectedLast;
  iswifiConnectedLast=iswifiConnected;

  if (iswifiConnectedOSR)
  {
      setNTPTime(posixTimeZone, ntpServer);

        // try to connect to BOSCH API and get haId of the appliance
      if (hc_get_ha_id(OA2Ptr) != HTTP_CODE_OK)
      {
        // if it fails, try to reauth
        if (!oauth_device_flow(OA2Ptr))
        {
          Serial.println("OAuth2 Error");
        }
        else
        {
          Serial.println("OAuth2 OK!");
          SSEStream = hc_setup_server_event(&httpMonitorRequest, OA2Ptr, BSHhaIdDiswasher);
        }
      }
  }

  if (millis() - lastTimeCheck > 1000)
  {
      lastTimeCheck = millis();
      getLocalTime(&local_time_tm);
  }
}

int isProgramActive=0;
//polling is internally done every 1000 ms
void homeconnect_poll_func()
{
  unsigned long currentMillis = millis();

  if(currentMillis - last_HC_SSE_poll_time > 5000)
  {
    last_HC_SSE_poll_time=currentMillis;
    if (hc_poll_server_event(SSEStream, &ssEventContent))
    {
      Serial.println(ssEventContent.eventName);
      Serial.println(ssEventContent.eventData);
      if (ssEventContent.eventName.indexOf("NOTIFY") >= 0)
      {
        // event type: NOTIFY
        DeserializationError error = deserializeJson(responseJsonDoc, ssEventContent.eventData);
        if (!error)
        {
          if (responseJsonDoc["items"][0]["key"].as<String>().indexOf("RemainingProgramTime") >= 0)
          {
            long remainingTimeSec = responseJsonDoc["items"][0]["value"].as<int32_t>();
            isProgramActive = 1;
          }
          if (responseJsonDoc["items"][0]["key"].as<String>().indexOf("ProgramFinished") >= 0)
          {
            isProgramActive = 0;
          }
          if (responseJsonDoc["items"][0]["key"].as<String>().indexOf("ProgramAborted") >= 0)
          {
            isProgramActive = 0;
          }
        }
      }
    }

    if (isProgramActive)
    {
      time_t ETA_timestamp = time(nullptr) + remainingTimeSec;
      struct tm ETA_datetime = *localtime(&ETA_timestamp);
      sprintf(dishwasherStateStr, "Fine progr: %02d:%02d", ETA_datetime.tm_hour, ETA_datetime.tm_min);
      Serial.println(dishwasherStateStr);
    }
    else
    {
      Serial.println("No active program");
      sprintf(dishwasherStateStr, "Nessun programma.");
    }
  }
}

void rendering_func()
{
  char strbuf[16];
  int dc=0;

  video_wait_frame(); // Wait for the end of the current frame

  if (millis() - lastScreenDrawMillis > 1000)
  {
      lastScreenDrawMillis = millis();
      memset(frame_buffer, 0, width * height); // Clear frame buffer  

      fbGfx->setFont(&Picopixel); // Set a large font for time display
      fbGfx->setTextColor(255); // Set text color to white

      if(timeIsSet)
      {
        strftime(strbuf, sizeof(strbuf), "%H:%M", &local_time_tm);
      }
      else
      {
        snprintf(strbuf, sizeof(strbuf), "--:--");
      }
      
      for(int i=0; i<MAX_DAYS; i++)
      {
        if(rainyDays[i] != -1) dc++;
      }

      if(dc>0) fbGfx->setCursor(20,90);
      else fbGfx->setCursor(20,130);
      fbGfx->setTextSize(16); // Scale up the text size
      fbGfx->print(strbuf); // Print the time string to the frame buffer
      
      dc=0;
      for(int i=0; i<MAX_DAYS; i++)
      {
        if(rainyDays[i] != -1)
        {
          fbGfx->setCursor(20, 125 + dc * 24);
          fbGfx->setTextSize(3); // Normal text size for forecast
          fbGfx->print(dayNames[rainyDays[i]]);
          fbGfx->print(" piove");
          dc++;
        }
      }

      if(!iswifiConnected)
      {
        fbGfx->setCursor(215, 160);
        fbGfx->setTextSize(2); // Normal text size for status
        fbGfx->print("No Wi-Fi :(");
      }


      fbGfx->setCursor(20, 220);
      fbGfx->setTextSize(2); // Normal text size for status
      fbGfx->print(dishwasherStateStr);

  }
  else
  {
    //game_of_life_step(frame_buffer, width, height); // Run one step of Game of Life on the frame buffer for a cool screensaver effect when not updating time/weather
  }
}

void guiTask(void *pvParameters)
{
    (void) pvParameters;
    for (;;)
    {
        rendering_func();
        // small delay to avoid hogging CPU
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void radarTask(void *pvParameters)
{
    pinMode(PIN_CRT_PWR_CTRL, OUTPUT);
    digitalWrite(PIN_CRT_PWR_CTRL, HIGH); // power on CRT
    radar.begin(Serial2, 17, 16); // pass any HardwareSerial port and RX/TX pins
    Serial.println("Radar initialized!");
    (void) pvParameters;
    for (;;)
    {
        radar.poll();
        if(millis() - lastPersonDetectionMillis > 100)
        {
          lastPersonDetectionMillis = millis();
          unsigned long now = millis();
          if (radar.person_detected) {
            personAbsentSince = 0;
            if (personDetectedSince == 0) personDetectedSince = now;
            if (!person_detected_filtered && (now - personDetectedSince >= 500))
              person_detected_filtered = true;
          } else {
            personDetectedSince = 0;
            if (personAbsentSince == 0) personAbsentSince = now;
            if (person_detected_filtered && (now - personAbsentSince >= 5000))
              person_detected_filtered = false;
          }
        }
        digitalWrite(PIN_CRT_PWR_CTRL, person_detected_filtered); // power on CRT if person detected, otherwise power off    
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup()
{
  delay(3000);
  Serial.begin(115200);
  video_init(320, 240, FB_FORMAT_GREY_8BPP, VIDEO_MODE_PAL, false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);


  // retrieve tokens from non-volatile storage
  preferences.begin("tokens", false);
  OA2Ptr->set_tokens(preferences.getString("access_token", "-"), preferences.getString("refresh_token", "-"));
  preferences.end();

  // setup OAuth2 object
  OA2Ptr->set_server_urls(authorization_url, token_generator_url);
  OA2Ptr->set_secrets(CLIENT_ID, CLIENT_SECRET);


  frame_buffer = video_get_frame_buffer_address();
  width = video_get_width();
  height = video_get_height();

  // create and attach Adafruit GFX wrapper to the raw frame buffer
  fbGfx = new Framebuffer_GFX(width, height);
  if (fbGfx) {
    fbGfx->setBuffer(frame_buffer);
    gfx_renderer.begin((Adafruit_GFX&)*fbGfx);
    gfx_renderer.setFont(u8g2_font_logisoso92_tn);  // select u8g2 font from here: https://github.com/olikraus/u8g2/wiki/fntlistall
    gfx_renderer.setFontMode(1);                 // use u8g2 transparent mode (this is default)
    gfx_renderer.setFontDirection(0);            // left to right (this is default)
    gfx_renderer.setForegroundColor(255);       // set color for the font (in this case white)
  }

  xTaskCreatePinnedToCore(
      guiTask,      // task function
      "NetworkTask",   // name
      4096,             // stack size in bytes
      NULL,             // parameters
      1,                // priority
      NULL,             // handle
      1);               // run on core 1

  /*
  xTaskCreatePinnedToCore(
      radarTask,      // task function
      "RadarTask",   // name
      4096,             // stack size in bytes
      NULL,             // parameters
      1,                // priority
      NULL,             // handle
      1);               // run on core 1
  */

}

void loop()
{
  network_func();

  if(iswifiConnected)
  {
    homeconnect_poll_func();
  }
}

