#include "Arduino.h"
#include "video.h"
#include "graphics.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <time.h>
#include "esp_sntp.h"

int16_t frame_cnt = 0;
// NTP server details
const char *ntpServer = "pool.ntp.org";
const char *posixTimeZone = "CET-1CEST,M3.5.0,M10.5.0/3"; // POSIX TZ timezone + DST string https://phpsecu.re/tz/
ESP32Time rtc(0);      

unsigned long lastTimeCheck = 0;
time_t last_local_minutes = 0, actual_local_minutes = 0;
struct tm local_time_tm;// offset in seconds

// Low-pass filter state for cursor_x smoothing
float filtered_cursor_x = 0.0f;
const float FILTER_ALPHA = 0.02f; 

uint8_t initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin("---", "---");
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
  }
  Serial.println("synchronized");
}

void setNTPTime(const char *_posixTimeZone, const char *_ntpServer)
{
  // make NTP request
  Serial.println("NTP begin");
  //sntp_set_sync_interval(24 * 60 * 60 * 1000UL);
  sntp_set_sync_interval(30 * 1000UL); 
  sntp_set_time_sync_notification_cb(ntp_sync_cb);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, _ntpServer);
  esp_sntp_init();
  setenv("TZ", _posixTimeZone, 1);
  tzset(); 
}


void setup()
{
    int wifi_ok = initWiFi();
    if (wifi_ok)
    {
        setNTPTime(posixTimeZone, ntpServer);
    }
      else
    {
        delay(1000);
        ESP.restart();
    }
    video_init(320, 240, FB_FORMAT_GREY_8BPP, VIDEO_MODE_PAL, false);
    frame_buffer = video_get_frame_buffer_address();
    width = video_get_width();
    height = video_get_height();
}

void loop()
{
    frame_cnt++;
    video_wait_frame(); // Wait for the end of the current frame

    if (millis() - lastTimeCheck > 1000)
    {
        lastTimeCheck = millis();
        getLocalTime(&local_time_tm);
    }

    memset(frame_buffer, 0, width * height); // Clear frame buffer

    float char_height = 90;
    int vertical_offset = 0;// sin(frame_cnt * 0.001f) * 20.0f;
    int horizontal_offset = 0;//cos(frame_cnt * 0.0015f) * 5.0f;

    char strbuf[16];
    strftime(strbuf, sizeof(strbuf), "%H %M", &local_time_tm);

    draw_vector_string(horizontal_offset, height/2+char_height/2*1.4+vertical_offset, strbuf, char_height, 1.4, 255); 

    int colon_x=167;
    int colon_y=88;
    draw_circle(colon_x, colon_y, 7, 255);
    draw_circle(colon_x, colon_y, 6, 255);
    draw_circle(colon_x, colon_y, 5, 255);

    draw_circle(colon_x, colon_y+35, 7, 255);
    draw_circle(colon_x, colon_y+35, 6, 255);
    draw_circle(colon_x, colon_y+35, 5, 255);

    draw_thick_line(25, height/2+char_height/2*1.4+vertical_offset+10, 302, height/2+char_height/2*1.4+vertical_offset+10, 3, 255); // bottom line

    // Calculate raw cursor position with sub-second precision for oversampling
    float raw_cursor_x = map(local_time_tm.tm_sec, 0, 59, 25, 302);
    filtered_cursor_x = filtered_cursor_x + FILTER_ALPHA * (raw_cursor_x - filtered_cursor_x);

    draw_circle(filtered_cursor_x, height/2+char_height/2*1.4+vertical_offset+10, 7, 255);
    draw_circle(filtered_cursor_x, height/2+char_height/2*1.4+vertical_offset+10, 6, 255);
    draw_circle(filtered_cursor_x, height/2+char_height/2*1.4+vertical_offset+10, 5, 255);

    
}

