/****************************************************************************************************************************
  ESP_RequestHandlersImpl.h - Dead simple web-server.
  For Ethernet shields on ESP8266 and ESP32
  For EthernetWebServer library by Khoi Hoang

  EthernetWebServer is a library for the Ethernet shields to run WebServer

  Based on and modified from ESP8266 https://github.com/esp8266/Arduino/releases
  Built by Khoi Hoang https://github.com/khoih-prog/EthernetWebServer
  Licensed under MIT license

  Original author:
  @file       Esp8266WebServer.h
  @author     Ivan Grokhotkov

  Version: 2.0.0

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.0.0   K Hoang      13/02/2020 Initial coding for Arduino Mega, Teensy, etc to support Ethernetx libraries
  ...
  1.6.0   K Hoang      04/09/2021 Add support to QNEthernet Library for Teensy 4.1
  1.7.0   K Hoang      09/09/2021 Add support to Portenta H7 Ethernet
  1.7.1   K Hoang      04/10/2021 Change option for PIO `lib_compat_mode` from default `soft` to `strict`. Update Packages Patches
  1.8.0   K Hoang      19/12/2021 Reduce usage of Arduino String with std::string
  1.8.1   K Hoang      24/12/2021 Fix bug
  1.8.2   K Hoang      27/12/2021 Fix wrong http status header bug
  1.8.3   K Hoang      28/12/2021 Fix authenticate issue caused by libb64
  1.8.4   K Hoang      11/01/2022 Fix libb64 compile error for ESP8266
  1.8.5   K Hoang      11/01/2022 Restore support to AVR Mega2560 and add megaAVR boards. Fix libb64 fallthrough compile warning
  1.8.6   K Hoang      12/01/2022 Fix bug not supporting boards
  2.0.0   K Hoang      16/01/2022 To coexist with ESP32 WebServer and ESP8266 ESP8266WebServer
 *************************************************************************************************************************************/

#pragma once

#include "RequestHandler.h"
#include "esp_detail/mimetable.h"
#include "FS.h"
#include "WString.h"
#include <MD5Builder.h>
#include <base64.h>


class ethernetFunctionRequestHandler : public ethernetRequestHandler
{
  public:

    ethernetFunctionRequestHandler(EthernetWebServer::THandlerFunction fn, EthernetWebServer::THandlerFunction ufn, const String &uri, const HTTPMethod& method)
      : _fn(fn)
      , _ufn(ufn)
      , _uri(uri)
      , _method(method)
    {
    }

    bool canHandle(const HTTPMethod& requestMethod, const String& requestUri) override
    {
      if (_method != HTTP_ANY && _method != requestMethod)
        return false;

      if (requestUri == _uri)
        return true;

      if (_uri.endsWith("/*"))
      {
        String _uristart = _uri;
        _uristart.replace("/*", "");

        if (requestUri.startsWith(_uristart))
          return true;
      }

      return false;
    }

    bool canUpload(const String& requestUri) override
    {
      if (!_ufn || !canHandle(HTTP_POST, requestUri))
        return false;

      return true;
    }

    bool handle(EthernetWebServer& server, const HTTPMethod& requestMethod, const String& requestUri) override
    {
      ETW_UNUSED(server);
      
      if (!canHandle(requestMethod, requestUri))
        return false;

      _fn();
      return true;
    }

    void upload(EthernetWebServer& server, const String& requestUri, const ethernetHTTPUpload& upload) override
    {
      ETW_UNUSED(server);
      ETW_UNUSED(upload);
      
      if (canUpload(requestUri))
        _ufn();
    }

  protected:
    EthernetWebServer::THandlerFunction _fn;
    EthernetWebServer::THandlerFunction _ufn;
    String _uri;
    HTTPMethod _method;
};

class StaticRequestHandler : public ethernetRequestHandler 
{
    using WebServerType = EthernetWebServer;
    
public:
    StaticRequestHandler(FS& fs, const char* path, const char* uri, const char* cache_header)
    : _fs(fs)
    , _uri(uri)
    , _path(path)
    , _cache_header(cache_header)
    {
        //DEBUGV("StaticRequestHandler: path=%s uri=%s, cache_header=%s\r\n", path, uri, cache_header == __null ? "" : cache_header);
        _isFile = fs.exists(path);
        _baseUriLength = _uri.length();
    }

    bool validMethod(HTTPMethod requestMethod){
        return (requestMethod == HTTP_GET) || (requestMethod == HTTP_HEAD);
    }

    /* Deprecated version. Please use mime::getContentType instead */
    static String getContentType(const String& path) __attribute__((deprecated)) 
    {
        return mime_esp::getContentType(path);
    }

protected:
    FS _fs;
    bool _isFile;
    String _uri;
    String _path;
    String _cache_header;
    size_t _baseUriLength;
};


class StaticFileRequestHandler
    :
public StaticRequestHandler 
{
    using SRH = StaticRequestHandler;
    using WebServerType = EthernetWebServer;

public:
    StaticFileRequestHandler(FS& fs, const char* path, const char* uri, const char* cache_header)
        :
    StaticRequestHandler{fs, path, uri, cache_header}
    {
        File f = SRH::_fs.open(path, "r");
        MD5Builder calcMD5;
        calcMD5.begin();
        calcMD5.addStream(f, f.size());
        calcMD5.calculate();
        calcMD5.getBytes(_ETag_md5);
        f.close();
    }

    bool canHandle(const HTTPMethod& requestMethod, const String& requestUri) override  
    {
        return SRH::validMethod(requestMethod) && requestUri == SRH::_uri;
    }

    bool handle(EthernetWebServer& server, const HTTPMethod& requestMethod, const String& requestUri) 
    {
        if (!canHandle(requestMethod, requestUri))
            return false;

        
        const String etag = "\"" + base64::encode(_ETag_md5, 16) + "\"";

        if(server.header("If-None-Match") == etag){
            server.send(304);
            return true;
        }

        File f = SRH::_fs.open(SRH::_path, "r");

        if (!f)
            return false;

        if (!_isFile) {
            f.close();
            return false;
        }

        if (SRH::_cache_header.length() != 0)
            server.sendHeader("Cache-Control", SRH::_cache_header);

        server.sendHeader("ETag", etag);

        String path_without_gz = SRH::_path;
        path_without_gz.replace(".gz","");
        server.streamFile(f, mime_esp::getContentType(path_without_gz), requestMethod);
        return true;
    }

protected:
    uint8_t _ETag_md5[16];
};

