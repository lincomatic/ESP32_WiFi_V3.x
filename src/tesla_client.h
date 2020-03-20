// -*- C++ -*-
/*
 * Copyright (c) 2020 Sam C. Lin and Chris Howell
 * Author: Sam C. Lin
 */

#ifndef _TESLA_CLIENT_H_
#define _TESLA_CLIENT_H_

//#define USE_WFCS // use WiFiClientSecure instead of Mongoose

#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef USE_WFCS
#include <WiFiClientSecure.h>
#else
#include <MongooseString.h>
#include <MongooseHttpClient.h>
#endif // USE_WFCS


#ifdef USE_WFCS
typedef struct whr {
  int retCode;
  String respStr;
} WfcsHttpResponse;

typedef std::function<void(WfcsHttpResponse *rsp)> WfcsHttpResponseHandler;
#endif // USE_WFCS

//TAR = Tesla_Active_Request
#define TAR_NONE      0
#define TAR_ACC_TOKEN 1
#define TAR_VEHICLES  2
#define TAR_CHG_STATE 3

#define WFCS_HTTP_RC_UNKNOWN -1
#define WFCS_HTTP_RC_OK 0
#define WFCS_HTTP_RC_DISCONNECTED 1
#define WFCS_HTTP_RC_TIMEOUT 2

typedef struct telsa_charge_info {
  bool isValid;
  float batteryRange;
  float chargeEnergyAdded;
  float chargeMilesAddedRated;
  int batteryLevel;
  int chargeLimitSOC;
  int timeToFullCharge;
  int chargerVoltage;
} TESLA_CHARGE_INFO;

class TeslaClient {
#ifdef USE_WFCS
  static const char *_httpHost;
#endif // USE_WFCS
  static const int _httpPort;
  static const char *_userAgent;
  static const char *_teslaClientId;
  static const char *_teslaClientSecret;
  static const char *_badAuthStr;

  int _activeRequest; // TAR_xxx
  unsigned long _lastRequestStart;
  String _accessToken;
  String _teslaUser;
  String _teslaPass;

  int _vehicleCnt;
  int _curVehIdx;
  String *_id;
  String *_displayName;

  TESLA_CHARGE_INFO _chargeInfo;

#ifdef USE_WFCS
  WiFiClientSecure _client;
  WfcsHttpResponseHandler _responseHandler;
  WfcsHttpResponse _resp;
  int _respPhase;
  unsigned long _waitStart;

  void _onHttpResponse(WfcsHttpResponseHandler handler) {
    _responseHandler = handler;
    _respPhase = 0;
    _waitStart = millis();
    _resp.retCode = WFCS_HTTP_RC_UNKNOWN;
    _resp.respStr = "";
  }
  void _handleResponse();
#else
  MongooseHttpClient _client;
#endif

  bool _isBusy() { return _activeRequest == TAR_NONE ? false : true; }
  void _cleanVehicles();

 public:
  TeslaClient();
  ~TeslaClient();

  void setUser(const char *user) { _teslaUser = user; }
  void setPass(const char *pass) { _teslaPass = pass; }

  void requestAccessToken();
  void requestVehicles();
  void requestChargeState();

  int getVehicleCnt() { return _vehicleCnt; }
  void setVehicleIdx(int vehidx) { _curVehIdx = vehidx; }
  int getCurVehicleIdx() { return _curVehIdx; }
  String getVehicleId(int vehidx);
  String getVehicleDisplayName(int vehidx);
  const char *getUser() { return _teslaUser.c_str(); }
  const char *getPass() { return _teslaPass.c_str(); }
  void getChargeInfoJson(String &sjson);
  const TESLA_CHARGE_INFO *getChargeInfo() { return &_chargeInfo; }

  void loop();
};

// global instance
extern TeslaClient teslaClient;

#endif // _TESLA_CLIENT_H_
