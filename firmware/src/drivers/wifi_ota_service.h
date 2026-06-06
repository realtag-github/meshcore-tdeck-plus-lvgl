#pragma once

#include <Arduino.h>

void wifi_ota_begin();
void wifi_ota_loop();
void wifi_ota_start();
void wifi_ota_restart();
void wifi_ota_stop();
bool wifi_ota_save_credentials(const String& ssid, const String& password, const String& hostname = String());
bool wifi_ota_save_hostname(const String& hostname);
void wifi_ota_clear_credentials();
String wifi_ota_status();
