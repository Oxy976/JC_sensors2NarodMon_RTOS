#ifndef SETTINGS_H
#define SETTINGS_H

// Network settings

// Настройки Wi-Fi с уникальными префиксами
#define CONF_SSID          "private"
#define CONF_PASSWORD      "nhbyflwfnmcbv"
#define CONF_HOSTNAME      "jc_meteo"


// Заменяем настройки сервера времени
//#define ntpServerName "pool.ntp.org"
#define ntpServerName "192.168.111.1"
//#define ntpServerName "192.168.1.1"
#define TIMEZONE 3
#define daylightOffset_sec 0

// MQTT (narodmon)
/*
//#define SRV   "narodmon.ru"
#define SRV   "192.168.111.10"
*/

#define CONF_MQTT_SERVER   "192.168.111.10"
//#define CONF_MQTT_SERVER   "narodmon.ru"
#define CONF_CLIENT_ID     "30AEA469B904" // MAC
#define CONF_AUTH_METHOD   "ghost-weather" //USERNAME
#define CONF_TOKEN         "15334"  //PASS
#define TOPIC              "login/m5-esp32/"
#define CONF_CONN_TOPIC    "login/m5-esp32/status"



#endif
