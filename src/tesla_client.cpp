// -*- C++ -*-
/*
 * Copyright (c) 2020 Sam C. Lin and Chris Howell
 * Author: Sam C. Lin
 */

#include "tesla_client.h"
#include "debug.h"

#define TESLA_USER_AGENT "007"
#define TESLA_CLIENT_ID "81527cff06843c8634fdc09e8ac0abefb46ac849f38fe1e431c2ef2106796384"
#define TESLA_CLIENT_SECRET "c7257eb71a564034f9419ee651c7d0e5f7aa6bfbd18bafb5c5c033b093bb2fa3"

#define TESLA_BASE_URI "https://owner-api.teslamotors.com"
#define TESLA_HOST "owner-api.teslamotors.com"
#define TESLA_PORT 443

#define TESLA_REQ_INTERVAL (30*1000UL)
#define TESLA_REQ_TIMEOUT (10*1000UL)


extern const char *root_ca;

const char *TeslaClient::_userAgent = TESLA_USER_AGENT;
#ifdef USE_WFCS
const char *TeslaClient::_httpHost = TESLA_HOST;
#endif // USE_WFCS
const int TeslaClient::_httpPort = TESLA_PORT;
const char *TeslaClient::_teslaClientId = TESLA_CLIENT_ID;
const char *TeslaClient::_teslaClientSecret = TESLA_CLIENT_SECRET;
const char *TeslaClient::_badAuthStr = "authorization_required_for_txid";

// global instance
TeslaClient teslaClient;

TeslaClient::TeslaClient()
{
  _curVehIdx = -1;
  _lastRequestStart = 0;
  _activeRequest = TAR_NONE;
  _id = NULL;
  _displayName = NULL;
  _accessToken = "";
  _chargeInfo.isValid = false;

#ifdef USE_WFCS
  _responseHandler = NULL;
#ifndef ESP8266
  _client.setCACert(root_ca);
#endif // ESP8266
#endif // USE_WFCS
}

TeslaClient::~TeslaClient()
{
  _cleanVehicles();
}

void TeslaClient::_cleanVehicles()
{
  if (_id) {
    delete [] _id;
    _id = NULL;
  }

  if (_displayName) {
    delete [] _displayName;
    _displayName = NULL;
  }

  _vehicleCnt = 0;
}


String TeslaClient::getVehicleId(int vehidx)
{
  if ((vehidx >= 0) && (vehidx < _vehicleCnt)) {
    return _id[vehidx];
  }
  else return String("");
}

String TeslaClient::getVehicleDisplayName(int vehidx)
{
  if ((vehidx >= 0) && (vehidx < _vehicleCnt)) {
    return _displayName[vehidx];
  }
  else return String("");
}



#ifdef USE_WFCS
void TeslaClient::_handleResponse()
{
  if (_responseHandler) {
    _resp.retCode = WFCS_HTTP_RC_UNKNOWN;
    
    if (_respPhase == 0) {
      if (_client.available()) {
	_respPhase = 1;
      }
      else if (millis()-_waitStart > TESLA_REQ_TIMEOUT) {
	_resp.retCode = WFCS_HTTP_RC_TIMEOUT;
	_client.stop();
      }
      else if (!_client.connected()) {
	_resp.retCode = WFCS_HTTP_RC_DISCONNECTED;
      }
    }
    if (_respPhase == 1) {
      while (_client.available()) {
	vTaskDelay(10);
	_resp.respStr += _client.readString();
      }

      _client.stop();
      _resp.retCode = WFCS_HTTP_RC_OK;
    }
    
    if (_resp.retCode != WFCS_HTTP_RC_UNKNOWN) {
      _responseHandler(&_resp);
      _responseHandler = NULL;
      _activeRequest = TAR_NONE;
    }
  }
}

void TeslaClient::loop()
{
  if (_isBusy()) {
    _handleResponse();
  }
  else { // !busy
    if ((millis()-_lastRequestStart) > TESLA_REQ_INTERVAL) {
      if (_accessToken.length() == 0) {
	requestAccessToken();
      }
      else if (_vehicleCnt == 0) {
	requestVehicles();
      }
      else {
	requestChargeState();
      }
    } 
  } // busy
}

void TeslaClient::requestAccessToken()
{
  DEBUG.println("requestAccessToken()");

  _lastRequestStart = millis();

  if ((_teslaUser == 0) || (_teslaPass == 0)) {
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_ACC_TOKEN;

  String s = "{";
  s += "\"grant_type\":\"password\",";
  s += "\"client_id\":\"" + String(_teslaClientId) + "\",";
  s += "\"client_secret\":\"" + String(_teslaClientSecret) + "\",";
  s += "\"email\":\"" + _teslaUser + "\",";
  s += "\"password\":\"" + _teslaPass + "\"";
  s += "}";

  DEBUG.println(s);
  DEBUG.print(_httpHost);DEBUG.println(_httpPort);
  if (!_client.connected() && !_client.connect(_httpHost, _httpPort)) {
    DEBUG.println("connection failed");
    _activeRequest = TAR_NONE;
    return;
  }

  _client.println("POST /oauth/token HTTP/1.1");
  _client.print("Host: ");_client.println(_httpHost);
  _client.print("User-Agent: ");_client.println(_userAgent);
  _client.println("Connection: keep-alive");
  _client.println("Content-Type: application/json; charset=utf-8");
  _client.print("Content-Length: ");_client.println(s.length());
  _client.println();
  _client.println(s);

  _onHttpResponse([&](WfcsHttpResponse *response) {
      DEBUG.print("resp.retCode: ");DEBUG.println(_resp.retCode);
      DEBUG.print("resp.respStr: ");DEBUG.print(_resp.respStr);DEBUG.println("*");
      if (_resp.retCode == WFCS_HTTP_RC_OK) {
	// bad credentials - prevent locking of account
	// by clearing the user/pass so we don't retry w/ them
	if (!strncmp(_resp.respStr.c_str(),"HTTP/1.1 401",12)) {
	  _teslaUser = "";
	  _teslaPass = "";
	  DEBUG.println("bad user/pass");
	}
	else { 
	  int s = _resp.respStr.indexOf('{');;
	  int e = (_resp.respStr.lastIndexOf('}') + 1);
	  if ((s > -1) && (e > -1)) {
	    String json = _resp.respStr.substring(s,e);
	    
	    const size_t capacity = JSON_OBJECT_SIZE(5) + 220;
	    DynamicJsonDocument doc(capacity);
	    deserializeJson(doc, json);
	    const char *token = (const char *)doc["access_token"];;
	    if (token) {
	      _accessToken = "Bearer ";
	      _accessToken += token;
	      DEBUG.print("token: ");DEBUG.println(_accessToken);
	      _lastRequestStart = 0; // allow requestVehicles to happen immediately
	    }
	    else {
	      _accessToken = "";
	      DEBUG.println("token error");
	    }
	  
	    //const char* token_type = doc["token_type"];
	    //long expires_in = doc["expires_in"];
	    //const char* refresh_token = doc["refresh_token"];
	    //long created_at = doc["created_at"]; // 1583079526
	  }
	}
      }
      _activeRequest = TAR_NONE;
    });
}

void TeslaClient::requestVehicles()
{
  DEBUG.println("requestVehicles()");

  _activeRequest = TAR_VEHICLES;
  _lastRequestStart = millis();

  if (!_client.connected() && !_client.connect(_httpHost, _httpPort)) {
    DEBUG.println("connection failed");
    _activeRequest = TAR_NONE;
    return;
  }
  _client.println("GET /api/1/vehicles/ HTTP/1.1");
  _client.print("Host: ");_client.println(_httpHost);
  _client.print("User-Agent: ");_client.println(_userAgent);
  _client.println("Accept: */*");
  _client.print("Authorization: ");_client.println(_accessToken);
  _client.println();

  _onHttpResponse([&](WfcsHttpResponse *response) {
      DEBUG.print("resp.retCode: ");DEBUG.println(_resp.retCode);
      DEBUG.print("resp.respStr: ");DEBUG.print(_resp.respStr);DEBUG.println("*");
      if (_resp.retCode == WFCS_HTTP_RC_OK) {
	int s = _resp.respStr.indexOf('{');;
	int e = (_resp.respStr.lastIndexOf('}') + 1);
	if ((s > -1) && (e > -1)) {
	  String json = _resp.respStr.substring(s,e);
	  if (strstr(json.c_str(),_badAuthStr)) {
	    _accessToken = "";
	    return;
	  }
	  
	  char *sc = strstr(json.c_str(),"\"count\":");
	  if (sc) {
	    _cleanVehicles();
	    sc += 8;
	    sscanf(sc,"%d",&_vehicleCnt);
	    DEBUG.print("vcnt: ");DEBUG.println(_vehicleCnt);

	    _id = new String[_vehicleCnt];
	    _displayName = new String[_vehicleCnt];

	    // ArduinoJson has a bug, and parses the id as a float.
	    // use this ugly workaround.
	    const char *sj = json.c_str();
	    for (int v=0;v < _vehicleCnt;v++) {
	      const char *sid = strstr(sj,"\"id\":");
	      if (sid) {
		sid += 5;
		_id[v] = "";
		while (*sid != ',') _id[v] += *(sid++);
		sj = sid;
	      }
	    }
	    
	    const size_t capacity = _vehicleCnt*JSON_ARRAY_SIZE(2) + JSON_ARRAY_SIZE(_vehicleCnt) + JSON_OBJECT_SIZE(2) + _vehicleCnt*JSON_OBJECT_SIZE(14) + _vehicleCnt*700;
	    DynamicJsonDocument doc(capacity);
	    deserializeJson(doc, json);
	    JsonArray jresponse = doc["response"];
	    
	    for (int i=0;i < _vehicleCnt;i++) {
	      JsonObject responsei = jresponse[i];
	      // doesn't work.. returns converted float _id[i] = responsei["id"].as<String>();
	      _displayName[i] = responsei["display_name"].as<String>();
	      DEBUG.print("id: ");DEBUG.print(_id[i]);
	      DEBUG.print(" name: ");DEBUG.println(_displayName[i]);
	    }

	    _lastRequestStart = 0; // allow requestChargeState to happen immediately
	  }
	}
      }
      _activeRequest = TAR_NONE;
    });
}

void TeslaClient::requestChargeState()
{
  _chargeInfo.isValid = false;
  DEBUG.print("getChargeState() vehidx=");DEBUG.println(_curVehIdx);

  if ((_vehicleCnt <= 0) ||
      (_curVehIdx < 0) || (_curVehIdx > (_vehicleCnt-1))) {
    DEBUG.println("vehicle idx out of range");
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_CHG_STATE;
  _lastRequestStart = millis();

  if (!_client.connected() && !_client.connect(_httpHost, _httpPort)) {
    DEBUG.println("connection failed");
    _activeRequest = TAR_NONE;
    return;
  }

  String sget = "GET /api/1/vehicles/" + String(_id[_curVehIdx]) + "/data_request/charge_state HTTP/1.1";
  _client.println(sget);
  _client.print("Host: ");_client.println(_httpHost);
  _client.print("User-Agent: ");_client.println(_userAgent);
  _client.println("Accept: */*");
  _client.print("Authorization: ");_client.println(_accessToken);
  _client.println();

  _onHttpResponse([&](WfcsHttpResponse *response) {
      DEBUG.print("resp.retCode: ");DEBUG.println(_resp.retCode);
      DEBUG.print("resp.respStr: ");DEBUG.print(_resp.respStr);DEBUG.println("*");
      if (_resp.retCode == WFCS_HTTP_RC_OK) {
	int s = _resp.respStr.indexOf('{');;
	int e = (_resp.respStr.lastIndexOf('}') + 1);
	if ((s > -1) && (e > -1)) {
	  String json = _resp.respStr.substring(s,e);
	  if (strstr(json.c_str(),_badAuthStr)) {
	    _accessToken = "";
	    _activeRequest = TAR_NONE;
	    return;
	  }

	  const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(43) + 1500;
	  DynamicJsonDocument doc(capacity);
	  deserializeJson(doc, json);

	  JsonObject jresponse = doc["response"];

	  _chargeInfo.batteryRange = jresponse["battery_range"];
	  _chargeInfo.chargeEnergyAdded = jresponse["charge_energy_added"];
	  _chargeInfo.chargeMilesAddedRated = jresponse["charge_miles_added_rated"];
	  _chargeInfo.batteryLevel = jresponse["battery_level"];
	  _chargeInfo.chargeLimitSOC = jresponse["charge_limit_soc"];
	  _chargeInfo.timeToFullCharge = jresponse["time_to_full_charge"];
	  _chargeInfo.chargerVoltage = jresponse["charger_voltage"];
	  //	  char str[200];
	  //	  sprintf(str,"chg info: %f %f %f %d %d %d %d",_chargeInfo.batteryRange,
	  //		  _chargeInfo.chargeEnergyAdded,_chargeInfo.chargeMilesAddedRated,
	  //		  _chargeInfo.batteryLevel,_chargeInfo.chargeLimitSOC,_chargeInfo.timeToFullCharge,_chargeInfo.chargerVoltage);
	  //	  DEBUG.println(str);
	  _chargeInfo.isValid = true;

#ifdef notyet
	  bool response_battery_heater_on = jresponse["battery_heater_on"]; // false
	  int response_charge_current_request = jresponse["charge_current_request"]; // 24
	  int response_charge_current_request_max = jresponse["charge_current_request_max"]; // 48
	  bool response_charge_enable_request = jresponse["charge_enable_request"]; // true

	  int response_charge_limit_soc_max = jresponse["charge_limit_soc_max"]; // 100
	  int response_charge_limit_soc_min = jresponse["charge_limit_soc_min"]; // 50
	  int response_charge_limit_soc_std = jresponse["charge_limit_soc_std"]; // 90
	  float response_charge_miles_added_ideal = jresponse["charge_miles_added_ideal"]; // 205.5
	  bool response_charge_port_door_open = jresponse["charge_port_door_open"]; // true
	  const char* response_charge_port_latch = jresponse["charge_port_latch"]; // "Engaged"
	  int response_charge_rate = jresponse["charge_rate"]; // 0
	  bool response_charge_to_max_range = jresponse["charge_to_max_range"]; // false
	  int response_charger_actual_current = jresponse["charger_actual_current"]; // 0
	  int response_charger_pilot_current = jresponse["charger_pilot_current"]; // 48
	  int response_charger_power = jresponse["charger_power"]; // 0

	  const char* response_charging_state = jresponse["charging_state"]; // "Complete"
	  const char* response_conn_charge_cable = jresponse["conn_charge_cable"]; // "IEC"
	  float response_est_battery_range = jresponse["est_battery_range"]; // 187.27
	  const char* response_fast_charger_brand = jresponse["fast_charger_brand"]; // ""
	  bool response_fast_charger_present = jresponse["fast_charger_present"]; // false
	  const char* response_fast_charger_type = jresponse["fast_charger_type"]; // ""
	  float response_ideal_battery_range = jresponse["ideal_battery_range"]; // 230.21
	  bool response_managed_charging_active = jresponse["managed_charging_active"]; // false
	  bool response_managed_charging_user_canceled = jresponse["managed_charging_user_canceled"]; // false
	  int response_max_range_charge_counter = jresponse["max_range_charge_counter"]; // 0
	  int response_minutes_to_full_charge = jresponse["minutes_to_full_charge"]; // 0
	  bool response_not_enough_power_to_heat = jresponse["not_enough_power_to_heat"]; // false
	  bool response_scheduled_charging_pending = jresponse["scheduled_charging_pending"]; // false

	  long response_timestamp = jresponse["timestamp"]; // 1583079530271
	  bool response_trip_charging = jresponse["trip_charging"]; // false
	  int response_usable_battery_level = jresponse["usable_battery_level"]; // 83
#endif // notyet
	}
      }
      _activeRequest = TAR_NONE;
    });
}
#else // use mongoose
void TeslaClient::loop()
{
  if ((_activeRequest != TAR_NONE) &&
      ((millis()-_lastRequestStart) > TESLA_REQ_TIMEOUT)) {
    _activeRequest = TAR_NONE;
  }

  if (!_isBusy()) {
    if ((millis()-_lastRequestStart) > TESLA_REQ_INTERVAL) {
      if (_accessToken.length() == 0) {
	requestAccessToken();
      }
      else if (_vehicleCnt == 0) {
	requestVehicles();
      }
      else {
	requestChargeState();
      }
    } 
  } // !busy
}


void printResponse(MongooseHttpClientResponse *response)
{
  DEBUG.printf("%d %.*s\n", response->respCode(), response->respStatusMsg().length(), (const char *)response->respStatusMsg());
  int headers = response->headers();
  int i;
  for(i=0; i<headers; i++) {
    DEBUG.printf("_HEADER[%.*s]: %.*s\n", 
      response->headerNames(i).length(), (const char *)response->headerNames(i), 
      response->headerValues(i).length(), (const char *)response->headerValues(i));
  }

  DEBUG.printf("\n%.*s\n", response->body().length(), (const char *)response->body());
}

void TeslaClient::requestAccessToken()
{
  DEBUG.println("requestAccessToken()");
  _lastRequestStart = millis();

  if ((_teslaUser == 0) || (_teslaPass == 0)) {
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_ACC_TOKEN;

  String s = "{";
  s += "\"grant_type\":\"password\",";
  s += "\"client_id\":\"" + String(_teslaClientId) + "\",";
  s += "\"client_secret\":\"" + String(_teslaClientSecret) + "\",";
  s += "\"email\":\"" + _teslaUser + "\",";
  s += "\"password\":\"" + _teslaPass + "\"";
  s += "}";

  String uri = TESLA_BASE_URI;
  uri += "/oauth/token";

  DEBUG.println(s);
  DEBUG.println(uri);
    
  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_POST);
  String extraheaders = "Connection: keep-alive\r\n";
  extraheaders += "User-Agent: ";
  extraheaders += _userAgent;
  extraheaders += "\r\n";
  extraheaders += "Content-Type: application/json; charset=utf-8\r\n";
  req->addExtraHeaders(extraheaders.c_str());
  req->setContent((const uint8_t*)s.c_str(),s.length());
  req->onResponse([&](MongooseHttpClientResponse *response) {
      DEBUG.println("resp");
      printResponse(response);

      // bad credentials - prevent locking of account
      // by clearing the user/pass so we don't retry w/ them
      if (response->respCode() == 401) {
	_teslaUser = "";
	_teslaPass = "";
	DEBUG.println("bad user/pass");
      }
      else if (response->respCode() == 200) { 
	const char *cjson = (const char *)response->body();
	if (cjson) {
	  String json = cjson;
      const size_t capacity = JSON_OBJECT_SIZE(5) + 220;
      DynamicJsonDocument doc(capacity);
      deserializeJson(doc, json);
	  const char *token = (const char *)doc["access_token"];;
	  if (token) {
      _accessToken = "Bearer ";
	    _accessToken += token;
	    DEBUG.print("token: ");DEBUG.println(_accessToken);
	    _lastRequestStart = 0; // allow requestVehicles to happen immediately
	  }
	  else {
	    _accessToken = "";
	    DEBUG.println("token error");
	  }

      //const char* token_type = doc["token_type"];
      //long expires_in = doc["expires_in"];
      //const char* refresh_token = doc["refresh_token"];
      //long created_at = doc["created_at"]; // 1583079526
	}
      }
      _activeRequest = TAR_NONE;
    });
  _client.send(req);
}


void TeslaClient::requestVehicles()
{
  DEBUG.println("requestVehicles()");

  _activeRequest = TAR_VEHICLES;
  _lastRequestStart = millis();

  String uri = TESLA_BASE_URI;
  uri += "/api/1/vehicles/";
  _accessToken = "Bearer f5b1e0239feb331db1fa5eb4969fc9c53a292124d797a740485dc375fc3dab50";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  /* broken
  req->addHeader("User-Agent",_userAgent);
  req->addHeader("Authorization", _accessToken);
  */
  String extraheaders = "Connection: keep-alive\r\n";
  extraheaders += "User-Agent: ";
  extraheaders += _userAgent;
  extraheaders += "\r\n";
  extraheaders += "Accept: */*\r\n";
  extraheaders += "Authorization: " + _accessToken + "\r\n";
  req->addExtraHeaders(extraheaders.c_str());
  req->onResponse([&](MongooseHttpClientResponse *response) {
      DEBUG.println("resp");
      printResponse(response);
      
      if (response->respCode() == 401) {
	  _accessToken = "";
	  _activeRequest = TAR_NONE;
	  return;
      }
      else if (response->respCode() == 200) {
      const char *json = (const char *)response->body();

	if (strstr(json,_badAuthStr)) {
	  _accessToken = "";
	  _activeRequest = TAR_NONE;
	  return;
	}

	
	char *sc = strstr(json,"\"count\":");
	if (sc) {
	  _cleanVehicles();
	  sc += 8;
	  sscanf(sc,"%d",&_vehicleCnt);
	  DEBUG.print("vcnt: ");DEBUG.println(_vehicleCnt);

	_id = new String[_vehicleCnt];
	_displayName = new String[_vehicleCnt];
	  
	  // ArduinoJson has a bug, and parses the id as a float.
	  // use this ugly workaround.
	  const char *sj = json;
	  for (int v=0;v < _vehicleCnt;v++) {
	    const char *sid = strstr(sj,"\"id\":");
	    if (sid) {
	      sid += 5;
	      _id[v] = "";
	      while (*sid != ',') _id[v] += *(sid++);
	      sj = sid;
	}
      }
	}
      }
      _activeRequest = TAR_NONE;
    });
  _client.send(req);
}


void TeslaClient::requestChargeState()
{
  _chargeInfo.isValid = false;
  DEBUG.print("getChargeState() vehidx=");DEBUG.println(_curVehIdx);
  _lastRequestStart = millis();

  if ((_vehicleCnt <= 0) ||
      (_curVehIdx < 0) || (_curVehIdx > (_vehicleCnt-1))) {
    DEBUG.println("vehicle idx out of range");
    _activeRequest = TAR_NONE;
    return;
  }

  _activeRequest = TAR_CHG_STATE;


  String uri = TESLA_BASE_URI;
  uri += "/api/1/vehicles/" + String(_id[_curVehIdx]) + "/data_request/charge_state";

  MongooseHttpClientRequest *req = _client.beginRequest(uri.c_str());
  req->setMethod(HTTP_GET);
  /*
  req->addHeader("User-Agent",_userAgent);
  req->addHeader("Authorization", _accessToken);
  */
  String extraheaders = "Connection: keep-alive\r\n";
  extraheaders += "User-Agent: ";
  extraheaders += _userAgent;
  extraheaders += "\r\n";
  extraheaders += "Accept: */*\r\n";
  extraheaders += "Authorization: " + _accessToken + "\r\n";
  req->addExtraHeaders(extraheaders.c_str());
  req->onResponse([&](MongooseHttpClientResponse *response) {
      DEBUG.println("resp");
      printResponse(response);

      if (response->respCode() == 200) {
	const char *json = (const char *)response->body();
      const size_t capacity = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(43) + 1500;
      DynamicJsonDocument doc(capacity);
      deserializeJson(doc, json);

      JsonObject jresponse = doc["response"];
      
	_chargeInfo.batteryRange = jresponse["battery_range"];
	_chargeInfo.chargeEnergyAdded = jresponse["charge_energy_added"];
	_chargeInfo.chargeMilesAddedRated = jresponse["charge_miles_added_rated"];
	_chargeInfo.batteryLevel = jresponse["battery_level"];
	_chargeInfo.chargeLimitSOC = jresponse["charge_limit_soc"];
	_chargeInfo.timeToFullCharge = jresponse["time_to_full_charge"];
	_chargeInfo.chargerVoltage = jresponse["charger_voltage"];
	//	  char str[200];
	//	  sprintf(str,"chg info: %f %f %f %d %d %d %d",_chargeInfo.batteryRange,
	//		  _chargeInfo.chargeEnergyAdded,_chargeInfo.chargeMilesAddedRated,
	//		  _chargeInfo.batteryLevel,_chargeInfo.chargeLimitSOC,_chargeInfo.timeToFullCharge,_chargeInfo.chargerVoltage);
	//	  DEBUG.println(str);
	_chargeInfo.isValid = true;
      
#ifdef notyet
      bool response_battery_heater_on = jresponse["battery_heater_on"]; // false
      int response_charge_current_request = jresponse["charge_current_request"]; // 24
      int response_charge_current_request_max = jresponse["charge_current_request_max"]; // 48
      bool response_charge_enable_request = jresponse["charge_enable_request"]; // true
      
      int response_charge_limit_soc_max = jresponse["charge_limit_soc_max"]; // 100
      int response_charge_limit_soc_min = jresponse["charge_limit_soc_min"]; // 50
      int response_charge_limit_soc_std = jresponse["charge_limit_soc_std"]; // 90
      float response_charge_miles_added_ideal = jresponse["charge_miles_added_ideal"]; // 205.5
      bool response_charge_port_door_open = jresponse["charge_port_door_open"]; // true
      const char* response_charge_port_latch = jresponse["charge_port_latch"]; // "Engaged"
      int response_charge_rate = jresponse["charge_rate"]; // 0
      bool response_charge_to_max_range = jresponse["charge_to_max_range"]; // false
      int response_charger_actual_current = jresponse["charger_actual_current"]; // 0
      int response_charger_pilot_current = jresponse["charger_pilot_current"]; // 48
      int response_charger_power = jresponse["charger_power"]; // 0
      
      const char* response_charging_state = jresponse["charging_state"]; // "Complete"
      const char* response_conn_charge_cable = jresponse["conn_charge_cable"]; // "IEC"
      float response_est_battery_range = jresponse["est_battery_range"]; // 187.27
      const char* response_fast_charger_brand = jresponse["fast_charger_brand"]; // ""
      bool response_fast_charger_present = jresponse["fast_charger_present"]; // false
      const char* response_fast_charger_type = jresponse["fast_charger_type"]; // ""
      float response_ideal_battery_range = jresponse["ideal_battery_range"]; // 230.21
      bool response_managed_charging_active = jresponse["managed_charging_active"]; // false
      bool response_managed_charging_user_canceled = jresponse["managed_charging_user_canceled"]; // false
      int response_max_range_charge_counter = jresponse["max_range_charge_counter"]; // 0
      int response_minutes_to_full_charge = jresponse["minutes_to_full_charge"]; // 0
      bool response_not_enough_power_to_heat = jresponse["not_enough_power_to_heat"]; // false
      bool response_scheduled_charging_pending = jresponse["scheduled_charging_pending"]; // false
      
      long response_timestamp = jresponse["timestamp"]; // 1583079530271
      bool response_trip_charging = jresponse["trip_charging"]; // false
      int response_usable_battery_level = jresponse["usable_battery_level"]; // 83
#endif // notyet
      }
      _activeRequest = TAR_NONE;
    });
  _client.send(req);
}

#endif // USE_WFCS

void TeslaClient::getChargeInfoJson(String &sjson)
{
  DynamicJsonDocument doc(JSON_OBJECT_SIZE(7));

  if (_chargeInfo.isValid) {
    doc["tesla/batteryRange"] = _chargeInfo.batteryRange;
    doc["tesla/chargeEnergyAdded"] = _chargeInfo.chargeEnergyAdded;
    doc["tesla/chargeMilesAddedRated"] = _chargeInfo.chargeMilesAddedRated;
    doc["tesla/batteryLevel"] = _chargeInfo.batteryLevel;
    doc["tesla/chargeLimitSOC"] = _chargeInfo.chargeLimitSOC;
    doc["tesla/timeToFullCharge"] = _chargeInfo.timeToFullCharge;
    doc["tesla/chargerVoltage"] = _chargeInfo.chargerVoltage;
    serializeJson(doc,sjson);
  }
  else {
    sjson = "";
  }
}

#ifdef TESLAERRORS
503 Service Unavailable
_HEADER[Server]: BigIP
_HEADER[Connection]: Keep-Alive
_HEADER[Content-Length]: 95

<html><head></head><body><p>Our servers are waiting for a supercharger spot..</p></body></html>
token error

#endif
