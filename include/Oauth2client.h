#include <HTTPClient.h>
#include <ArduinoJson.h>

class Oauth2Client
{
    private:
    JsonDocument deviceAuthorizationResponse;
    JsonDocument deviceAccesTokenRespose;
    String accessToken;
    String refreshToken;
    String authorization_url;
    String token_generator_url;
    String client_id, client_secret;
   
    public:
    /// @brief Set access token and refresh token, you may retrieve them from flash or other storage
    /// @param _accessToken 
    /// @param _refreshToken 
    void set_tokens(String _accessToken, String _refreshToken)
    {
        accessToken=String(_accessToken);
        refreshToken=String(_refreshToken);
    }

    /// @brief Set urls for device autorization and token generation
    /// @param _authorization_url 
    /// @param _token_generator_url 
    void set_server_urls(String _authorization_url, String _token_generator_url)
    {
        authorization_url=String(_authorization_url);
        token_generator_url=String(_token_generator_url);
    }
    
    void set_secrets(String _client_id, String _client_secret)
    {
        client_id=String(_client_id);
        client_secret=String(_client_secret);
    }

    String get_access_token()
    {
        return accessToken;
    }

    String get_refresh_token()
    {
        return refreshToken;
    }

    JsonDocument get_device_auth_response()
    {
        return deviceAuthorizationResponse;
    }

    int req_device_authorization(String _scope)
    {
        HTTPClient http;
        http.begin(authorization_url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String postData =
            String("client_id=") + client_id +
            String("&scope=") + _scope;

        int httpCode = http.POST(postData);
        String postResponse = http.getString();
        http.end();
        
        DeserializationError error = deserializeJson(deviceAuthorizationResponse, postResponse);
        if (error)
        {
            return 0;
        }

        if (httpCode == HTTP_CODE_OK)
        {
            //deviceAuthorizationResponse contents are valid
            return 1;
        }
        else
        {
            return httpCode;
        }
    }

    int req_access_token()
    {
        HTTPClient http;
        http.begin(token_generator_url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String postData =
            String("grant_type=device_code") +
            String("&device_code=") + deviceAuthorizationResponse["device_code"].as<String>() +
            String("&client_id=") + client_id +
            String("&client_secret=") + client_secret;

        int httpCode = http.POST(postData);
        String postResponse = http.getString();
        http.end();

        DeserializationError error = deserializeJson(deviceAccesTokenRespose, postResponse);
        if (error)
        {
            return 0;
        }

        if (httpCode == HTTP_CODE_OK)
        {
            accessToken = deviceAccesTokenRespose["access_token"].as<String>();
            refreshToken = deviceAccesTokenRespose["refresh_token"].as<String>();
        }
        else
        {
            //todo
        }
        return httpCode;   
    }

    int refresh_access_token(String _scope)
    {
        HTTPClient http;
        http.begin(token_generator_url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String postData =
            String("grant_type=refresh_token") +
            String("&refresh_token=") + refreshToken +
            String("&client_id=") + client_id +
            String("&client_secret=") + client_secret + 
            String("&scope=") + _scope;

        int httpCode = http.POST(postData);
        String postResponse = http.getString();
        http.end();

        if (httpCode == HTTP_CODE_OK)
        {
            DeserializationError error = deserializeJson(deviceAccesTokenRespose, postResponse);
            if (error)
            {
                return 0;
            }
            else
            {
                accessToken = String(deviceAccesTokenRespose["access_token"].as<String>());
                refreshToken = String(deviceAccesTokenRespose["refresh_token"].as<String>());
            }
        }
        return httpCode;
    }
};