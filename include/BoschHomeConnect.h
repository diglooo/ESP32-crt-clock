#include <Preferences.h>
#include <ArduinoJson.h>
#include "Oauth2client.h"
#include "secrets.h"

Oauth2Client OA2Obj;
Oauth2Client *OA2Ptr = &OA2Obj;
String authorization_url = "https://api.home-connect.com/security/oauth/device_authorization";
String token_generator_url = "https://api.home-connect.com/security/oauth/token";
String OA2scope = "IdentifyAppliance%20Dishwasher-Monitor";

JsonDocument responseJsonDoc;
String BSHhaIdDiswasher;
Preferences preferences;

HTTPClient httpMonitorRequest;
WiFiClient *SSEStream;

struct sse_event_t
{
  String eventName;
  String eventData;
};
sse_event_t ssEventContent;

enum appliance_state
{
  UNKNOWN,
  RUNNING,
  STOPPED
};

struct dishwasher_state_t
{
  String programName;
  unsigned long remainingTimeSec;
  appliance_state state;
};
dishwasher_state_t dishwasherState;


int hc_get_ha_id(Oauth2Client *OA2)
{
  HTTPClient http;
  http.begin("https://api.home-connect.com/api/homeappliances");
  http.addHeader("accept", "application/vnd.bsh.sdk.v1+json");
  http.addHeader("authorization", String("Bearer") + OA2->get_access_token());

  int httpResponseCode = http.GET();
  String postResponse = http.getString();
  http.end();

  if (httpResponseCode == HTTP_CODE_OK)
  {
    DeserializationError error = deserializeJson(responseJsonDoc, postResponse);
    if (error)
    {
      return 0;
    }
    else
    {
      // sleect index zero of my appliances (I have only one)
      BSHhaIdDiswasher = responseJsonDoc["data"]["homeappliances"][0]["haId"].as<String>();
      Serial.print("haid = ");
      Serial.println(BSHhaIdDiswasher);
    }
  }
  else
    Serial.println(postResponse);
  return httpResponseCode;
}

int hc_get_active_program(Oauth2Client *OA2)
{
  HTTPClient http;
  http.begin(String("https://api.home-connect.com/api/homeappliances/") + String(BSHhaIdDiswasher) + String("/programs/active"));
  http.addHeader("accept", "application/vnd.bsh.sdk.v1+json");
  http.addHeader("authorization", String("Bearer") + OA2->get_access_token());

  int httpResponseCode = http.GET();
  String postResponse = http.getString();
  http.end();

  if (httpResponseCode == HTTP_CODE_OK)
  {
    DeserializationError error = deserializeJson(responseJsonDoc, postResponse);
    if (error)
    {
      return 0;
    }
  }
  return httpResponseCode;
}

WiFiClient *hc_setup_server_event(HTTPClient *_httpMonitorRequest, Oauth2Client *OA2, String haId)
{
  _httpMonitorRequest->begin(String("https://api.home-connect.com/api/homeappliances/") + haId + String("events"));
  _httpMonitorRequest->addHeader("Content-Type", "text/event-streamn");
  _httpMonitorRequest->addHeader("authorization", String("Bearer") + OA2->get_access_token());

  int httpResponseCode = _httpMonitorRequest->GET();
  if (httpResponseCode == HTTP_CODE_OK)
  {
    if (_httpMonitorRequest->connected())
    {
      Serial.println("SSE open, returning stream pointer");
      return _httpMonitorRequest->getStreamPtr();
    }
    else
    {
      _httpMonitorRequest->end();
      return NULL;
    }
  }
  else
  {
    Serial.println("SSE not ok, response is:");
    while (_httpMonitorRequest->getStreamPtr()->available())
    {
      char c = _httpMonitorRequest->getStreamPtr()->read();
      Serial.print(c);
    }
    _httpMonitorRequest->end();
    return NULL;
  }
}

int hc_poll_server_event(WiFiClient *_eventStream, sse_event_t *ev)
{
  String data_line;
  int seq = 0;
  if (_eventStream != NULL)
  {
    while (_eventStream->available())
    {
      char c = _eventStream->read();
      data_line += c;
    }
    data_line.trim();

    if (data_line.length() > 0)
    {
      int event_sta = data_line.indexOf("event");
      int event_end = data_line.indexOf('\n', event_sta);
      if (event_sta > 0 && event_end > 0)
      {
        ev->eventName = data_line.substring(event_sta + 6, event_end);
        ev->eventName.trim();
      }

      int data_sta = data_line.indexOf("data");
      int data_end = data_line.indexOf('\n', data_sta);
      if (data_sta > 0 && data_end > 0)
      {
        ev->eventData = data_line.substring(data_sta + 5, data_end);
        ev->eventData.trim();
      }
      return 1;
    }
  }
  return 0;
}

int oauth_device_flow(Oauth2Client *OA2)
{
  // if token is invalid, try to refresh
  Serial.println("refresh_access_token()");
  if (OA2->refresh_access_token(OA2scope) == HTTP_CODE_OK)
  {
    // token refresed successfully
    // save tokens in flash
    preferences.begin("tokens", false);
    preferences.putString("access_token", OA2->get_access_token());
    preferences.putString("refresh_token", OA2->get_refresh_token());
    preferences.end();
    return 1;
  }
  else
  {
    // if refresh fails, redo the OAuth2 device flow
    Serial.println("refresh_access_token() failed");
    Serial.println("Begin OAuth2 device flow.");
    if (OA2->req_device_authorization(OA2scope))
    {
      // Wait for user to go to the landing page
      // and input the code shown on the display
      JsonDocument resp = OA2->get_device_auth_response();

      // display a link with the code
      Serial.println("Go to website:");
      Serial.println(resp["verification_uri_complete"].as<String>());
      Serial.println("And input this code:");
      Serial.println(resp["user_code"].as<String>());

      // waiting for user login on other device...
      int response = 0;
      int attempts = 20;
      do
      {
        delay(5000);
        response = OA2->req_access_token();
        attempts--;
      } while (response != HTTP_CODE_OK && attempts > 0);

      if (attempts > 0 && response == HTTP_CODE_OK)
      {
        // save tokens
        preferences.begin("tokens", false);
        preferences.putString("access_token", OA2->get_access_token());
        preferences.putString("refresh_token", OA2->get_refresh_token());
        preferences.end();
        return 1;
      }
      else
      {
        Serial.println("Device flow timeout or failed");
        return 0;
      }
    }
  }
  return 0;
}

void parse_active_program(dishwasher_state_t *dwState)
{
  int activeProgramResp = hc_get_active_program(OA2Ptr);
  if (activeProgramResp == HTTP_CODE_OK)
  {
    String programName = responseJsonDoc["data"]["key"].as<String>();
    dwState->programName = String(programName.substring(programName.lastIndexOf(".") + 1, programName.length()));

    for (int i = 0; i < responseJsonDoc["data"]["options"].size(); i++)
    {
      String key = responseJsonDoc["data"]["options"][i]["key"].as<String>();
      if (key.compareTo("BSH.Common.Option.RemainingProgramTime") == 0)
      {
        dwState->remainingTimeSec = responseJsonDoc["data"]["options"][i]["value"].as<int32_t>();
      }
    }
    dwState->state = RUNNING;
    return;
  }
  else
  {
    dwState->state = STOPPED;
    return;
  }
}