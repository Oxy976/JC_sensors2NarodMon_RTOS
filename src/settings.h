#ifndef SETTINGS_H
#define SETTINGS_H

// Network settings

// Настройки Wi-Fi с уникальными префиксами
#define CONF_SSID          "AP"
#define CONF_PASSWORD      "APPass"
#define CONF_HOSTNAME      "jc_meteo"


// Заменяем настройки сервера времени
#define ntpServerName "pool.ntp.org"
#define TIMEZONE 3
#define daylightOffset_sec 0

// MQTT (narodmon)
/*
//#define SRV   "narodmon.ru"
#define SRV   "192.168. "
*/

#define CONF_MQTT_SERVER   "192.168. "
//#define CONF_MQTT_SERVER   "narodmon.ru"
#define CONF_CLIENT_ID     "xxxxxxxx" // MAC
#define CONF_AUTH_METHOD   "xxxx" //USERNAME
#define CONF_TOKEN         "xxxx"  //PASS
#define TOPIC              "login/xxxx/"
#define CONF_CONN_TOPIC    "login/xxxx/status"



#endif
