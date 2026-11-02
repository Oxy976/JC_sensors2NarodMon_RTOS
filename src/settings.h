#ifndef SETTINGS_H
#define SETTINGS_H

// Network settings
// --

// Настройки Wi-Fi с уникальными префиксами
#define CONF_SSID          "AP"
#define CONF_PASSWORD      "ApPass"
#define CONF_HOSTNAME      "jc_meteo"


//  настройки сервера времени
#define ntpServerName "ru.pool.ntp.org"
#define TIMEZONE 3
#define daylightOffset_sec 0


// MQTT (narodmon)
/*
//#define SRV   "narodmon.ru"
//#define SRV   "192.168. "
#define CONF_MQTT_SERVER   "192.168. "
//#define CONF_MQTT_SERVER   "narodmon.ru"
#define CONF_CLIENT_ID     "********" // MAC
#define CONF_AUTH_METHOD   "*******" //USERNAME
#define CONF_TOKEN         "*******"  //PASS
#define TOPIC              "login/****/"
#define CONF_CONN_TOPIC    "login/*****/status"
*/


#endif
