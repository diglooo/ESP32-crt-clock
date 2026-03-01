#include "Arduino.h"
#include "video.h"
#include "graphics.h"
#include "framebuffer_gfx.h"
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
#include "secrets.h"

int16_t frame_cnt = 0;
// NTP server details
const char *ntpServer = "pool.ntp.org";
const char *posixTimeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; // POSIX TZ timezone + DST string https://phpsecu.re/tz/
ESP32Time rtc(0);      

unsigned long lastTimeCheck = 0;
time_t last_local_minutes = 0, actual_local_minutes = 0;
struct tm local_time_tm;// offset in seconds

uint8_t iswifiConnected = 0, iswifiConnectedLast = 0, iswifiConnectedOSR = 0;
uint32_t lastwificheckmillis = 10000+1, lastWeatherCheckMillis=30*60000+1;

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
  // make NTP request
  Serial.println("NTP begin");
  sntp_set_sync_interval(24 * 60 * 60 * 1000UL);
  //sntp_set_sync_interval(30 * 1000UL); 
  sntp_set_time_sync_notification_cb(ntp_sync_cb);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, _ntpServer);
  esp_sntp_init();
  setenv("TZ", _posixTimeZone, 1);
  tzset(); 
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
  int last_day=-1;

  if (forecast)
  {
    for (int i = 0; i < (MAX_DAYS * 24/MAX_HOURS); i++)
    {
      Serial.print("dow  : "); Serial.println(dayNames[getDayOfWeek(forecast->dt[i])]);
      Serial.print("main : "); Serial.println((forecast->main[i]));
      Serial.println();

      //gat day of current forecast
      int forecastDay=getDayOfWeek(forecast->dt[i]);

      //if forecast is rain
      if(forecast->main[i] == "Rain")
      {
        //if last set rainy day is different than current day
        if(last_day != forecastDay)
        {
          //set next rainy day
          _rainyDays[day_index]=forecastDay;
          last_day=forecastDay;
          day_index++;
        }
      }
    }
  }
  // Delete to free up space and prevent fragmentation as strings change in length
  delete forecast;
}

void network_task()
{
  unsigned long currentMillis = millis();
  if(currentMillis-lastwificheckmillis >10000)
  {
    iswifiConnected=WiFi.status() == WL_CONNECTED;
    lastwificheckmillis=currentMillis;
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
  }

  if (millis() - lastTimeCheck > 1000)
  {
      lastTimeCheck = millis();
      getLocalTime(&local_time_tm);
  }
}


unsigned long lastScreenDrawMillis = 1100;

void rendering_task()
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

  }
  else
  {
    //game_of_life_step(frame_buffer, width, height); // Run one step of Game of Life on the frame buffer for a cool screensaver effect when not updating time/weather
  }
  
}

// FreeRTOS task wrapper that repeatedly calls network_loop()
// It has a larger stack so that network_loop and any secure client
// objects do not overflow the task stack.
void guiTask(void *pvParameters)
{
    (void) pvParameters;
    for (;;)
    {
        rendering_task();
        // small delay to avoid hogging CPU
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup()
{
  Serial.begin(115200);
  video_init(320, 240, FB_FORMAT_GREY_8BPP, VIDEO_MODE_PAL, false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

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

  // start network_loop in its own task pinned to core 0
    // increased stack size to handle WiFiClientSecure allocations
    xTaskCreatePinnedToCore(
      guiTask,      // task function
      "NetworkTask",   // name
      4096,             // stack size in bytes
      NULL,             // parameters
      1,                // priority
      NULL,             // handle
      1);               // run on core 0
}

void loop()
{
 network_task();
}

