#include "sensors.h"
#include <Wire.h>
#include <MicroDS18B20.h> // GyverLibs v3.10
#include <SHT2x.h>        // RobTillaart v0.5.5
#include <SHT4x.h>
#include <SparkFunBME280.h>                 // SparkFun BME280 v2.0.9
#include "SparkFun_SCD30_Arduino_Library.h" // SparkFun SCD30
#include <SparkFun_TMP117.h>                // SparkFun TMP117 v1.2.5
#include <sps30.h>                          // Sensirion SPS30 v1.2.0 (C API)
#include <esp_log.h>

static const char *TAG = "SENSORS";

#define PIN_SENS_SDA 17
#define PIN_SENS_SCL 18
#define PIN_TOUCH_SDA 4
#define PIN_TOUCH_SCL 8
#define PIN_DS18B20 6

// Настоящие статические объекты (как и объявлено в начале файла)
static MicroDS18B20<PIN_DS18B20> sDS;

SemaphoreHandle_t xLcdMutex = NULL;
SemaphoreHandle_t xSensorsMutex = NULL;

static BME280 sBME_e;
static BME280 sBME_i;
static SHT2x sSHT21_e;
static SHT4x sSHT40_e(0x44, &Wire);
static SCD30 sSCD30_i;
static TMP117 sTMP117;

// Флаги физического присутствия датчиков
boolean bDS = false;
boolean bBME_e = false;
boolean bBME_i = false;
boolean bHTU_e = false;
boolean bSHT_e = false;
boolean bSCD30_i = false;
boolean bTMP117 = false;
boolean bSPS30 = false;
boolean bRAD = false;

// Переменные автомата состояний прогрева
#define HEATTIME 6000
#define COOLTIME 20000
enum HeatState
{
    HEAT_IDLE,
    HEAT_WARMING,
    HEAT_COOLING
};
static HeatState currentHeatState = HEAT_IDLE;
static unsigned long heatTimer = 0;
static unsigned long lastHeatTimestamp = 0;

static String sC = "C";
static String GRAD = "\u00B0" + sC;

// Значения коррекции показаний
float fDS_Tfix = -0.3;
float fBME_e_Tfix = 0.0;
float fHTU_e_Tfix = -0.3;
float fSHT_e_Tfix = -0.7;
float fTMP_Tfix = 0.0;

__attribute__((weak)) void logToWeb(String text)
{
    ESP_LOGD("WEB_LOG", "%s", text.c_str());
}

void switchToMeteoBus()
{
    // Берем мьютекс экрана, блокируя программный тачскрин от попыток занять общую шину
    if (xLcdMutex != NULL)
    {
        xSemaphoreTake(xLcdMutex, portMAX_DELAY);
    }
    // Даем микропаузу для стабилизации линий связи
    vTaskDelay(pdMS_TO_TICKS(5));
}

void switchToTouchBus()
{
    // Просто отдаем мьютекс. Программный тачскрин теперь может безопасно работать со своими пинами 4/8
    if (xLcdMutex != NULL)
    {
        xSemaphoreGive(xLcdMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
}

void startSens(stSens *vSensVal)
{
    ESP_LOGI(TAG, "Старт безопасной монопольной инициализации датчиков (19 каналов)...");

    if (xSensorsMutex == NULL)
        xSensorsMutex = xSemaphoreCreateMutex();
    if (xSensorsMutex != NULL)
        xSemaphoreTake(xSensorsMutex, portMAX_DELAY);

    // 1. Заполнение структуры данных vSensVal под новый порядок индексов
    vSensVal[0].name = "ext117_Temp";    vSensVal[0].id_name = "temp_ext_117";      vSensVal[0].mqttId = "T3";
    vSensVal[1].name = "Rad_dyn";         vSensVal[1].id_name = "rad_dyn";           vSensVal[1].mqttId = "";
    vSensVal[2].name = "Rad_stat";        vSensVal[2].id_name = "rad_stat";          vSensVal[2].mqttId = "R0";
    vSensVal[3].name = "Rad_pulses";      vSensVal[3].id_name = "rad_pulses";        vSensVal[3].mqttId = "";
    vSensVal[4].name = "extBME_Temp";      vSensVal[4].id_name = "temp_ext_bme";      vSensVal[4].mqttId = "";
    vSensVal[5].name = "extBME_Hum";       vSensVal[5].id_name = "hum_ext_bme";       vSensVal[5].mqttId = "";
    vSensVal[6].name = "extBME_Press";     vSensVal[6].id_name = "press_ext_bme";     vSensVal[6].mqttId = "";
    vSensVal[7].name = "extHTU_Temp";      vSensVal[7].id_name = "temp_ext_htu";      vSensVal[7].mqttId = "";
    vSensVal[8].name = "extHTU_Hum";       vSensVal[8].id_name = "hum_ext_htu";       vSensVal[8].mqttId = "";
    vSensVal[9].name = "extSHT_Temp";      vSensVal[9].id_name = "temp_ext_sht";      vSensVal[9].mqttId = "T1";
    vSensVal[10].name = "extSHT_Hum";      vSensVal[10].id_name = "hum_ext_sht";      vSensVal[10].mqttId = "H1";
    vSensVal[11].name = "intBME_Temp";     vSensVal[11].id_name = "temp_int_bme";     vSensVal[11].mqttId = "T2";
    vSensVal[12].name = "intBME_Hum";      vSensVal[12].id_name = "hum_int_bme";      vSensVal[12].mqttId = "H2";
    vSensVal[13].name = "intBME_Press";    vSensVal[13].id_name = "press_int_bme";    vSensVal[13].mqttId = "P2";
    vSensVal[14].name = "intSCD30_Temp";   vSensVal[14].id_name = "temp_int_scd30";   vSensVal[14].mqttId = "";
    vSensVal[15].name = "intSCD30_Hum";    vSensVal[15].id_name = "hum_int_scd30";    vSensVal[15].mqttId = "";
    vSensVal[16].name = "intSCD30_CO2";    vSensVal[16].id_name = "co2_int_scd30";    vSensVal[16].mqttId = "CO2";
    vSensVal[17].name = "extSPS30";        vSensVal[17].id_name = "pm_ext_sps30";     vSensVal[17].mqttId = "PM25";
    vSensVal[18].name = "extDS_Temp";      vSensVal[18].id_name = "temp_ext_ds";      vSensVal[18].mqttId = "T0";

    resetActualSensVal(vSensVal);

    // Инициализация единиц измерения (юнитов)
    vSensVal[0].unit = GRAD;      vSensVal[1].unit = "mRg/h";   vSensVal[2].unit = "mRg/h";   vSensVal[3].unit = "#";
    vSensVal[4].unit = GRAD;      vSensVal[5].unit = "%";       vSensVal[6].unit = "mmHg";    vSensVal[7].unit = GRAD;
    vSensVal[8].unit = "%";       vSensVal[9].unit = GRAD;      vSensVal[10].unit = "%";      vSensVal[11].unit = GRAD;
    vSensVal[12].unit = "%";      vSensVal[13].unit = "mmHg";   vSensVal[14].unit = GRAD;     vSensVal[15].unit = "%";
    vSensVal[16].unit = "ppm";    vSensVal[17].unit = "ug/m3";  vSensVal[18].unit = GRAD;

    if (xSensorsMutex != NULL)
        xSemaphoreGive(xSensorsMutex);

    // 2. Сверхнадежная проверка DS18B20 по уникальному адресу устройства
    uint8_t current_ds_address[8] = {0};
    if (sDS.readAddress(current_ds_address)) {
        if (current_ds_address[0] == 0x28) {
            bDS = true;
            ESP_LOGI(TAG, "+++ Датчик Dallas DS18B20 РЕАЛЬНО НАЙДЕН.");
            sDS.requestTemp(); 
        } else {
            bDS = false;
        }
    } else {
        bDS = false;
        ESP_LOGW(TAG, "--- Датчик Dallas DS18B20 ФИЗИЧЕСКИ ОТСУТСТВУЕТ на пине %d", PIN_DS18B20);
    }

    // Входим в режим работы с метео-датчиками
    switchToMeteoBus();

    // 3. Инициализация TMP117
    if (!sTMP117.begin(0x48, Wire)) {
        bTMP117 = false;
        ESP_LOGD(TAG, "--- Датчик TMP117 не найден");
    } else {
        bTMP117 = true;
        ESP_LOGI(TAG, "+++ Датчик TMP117 успешно запущен (Канал 0)");
    }

    // 4. Уличный BME280 [0x77]
    sBME_e.setI2CAddress(0x77);
    if (!sBME_e.beginI2C(Wire)) {
        bBME_e = false;
        ESP_LOGD(TAG, "--- Уличный BME280 [0x77] не найден");
    } else {
        bBME_e = true;
        ESP_LOGI(TAG, "+++ Уличный BME280 [0x77] успешно запущен");
    }

    // 5. Уличный HTU21D/SHT21 [0x40]
    if (!sSHT21_e.begin()) {
        bHTU_e = false;
        ESP_LOGD(TAG, "--- Уличный HTU21D/SHT21 не найден");
    } else {
        bHTU_e = true;
        ESP_LOGI(TAG, "+++ Уличный HTU21D/SHT21 успешно запущен");
    }

    // 6. Уличный SHT40 [0x44]
    if (!sSHT40_e.begin()) {
        bSHT_e = false;
        ESP_LOGD(TAG, "--- Уличный SHT40 [0x44] не найден");
    } else {
        bSHT_e = true;
        ESP_LOGI(TAG, "+++ Уличный SHT40 [0x44] успешно запущен (Каналы 9, 10)");
    }

    // 7. Комнатный BME280 [0x76]
    sBME_i.setI2CAddress(0x76);
    if (!sBME_i.beginI2C(Wire)) {
        bBME_i = false;
        ESP_LOGD(TAG, "--- Комнатный BME280 [0x76] не найден");
    } else {
        bBME_i = true;
        ESP_LOGI(TAG, "+++ Комнатный BME280 [0x76] успешно запущен");
    }

    // 8. Комнатный SCD30 (CO2)
    // Убираем false! Обычный дефолтный begin() без параметров внутри SparkFun библиотеки 
    // идеально ложится на уже запущенный Master-режим шины Wire.
    if (!sSCD30_i.begin()) {
        bSCD30_i = false;
        ESP_LOGD(TAG, "--- Комнатный SCD30 не найден");
    } else {
        bSCD30_i = true;
        sSCD30_i.setMeasurementInterval(2); // Выставляем интервал строго ПОСЛЕ успешного begin()
        ESP_LOGI(TAG, "+++ Комнатный SCD30 успешно запущен");
    }

    // 9. Анализатор пыли Sensirion SPS30
    if (sps30_probe() != 0) {
        bSPS30 = false;
        ESP_LOGD(TAG, "--- Датчик частиц SPS30 не обнаружен на шине I2C");
    } else {
        if (sps30_start_measurement() != 0) {
            bSPS30 = false;
            ESP_LOGD(TAG, "--- SPS30: Ошибка старта лазерных измерений");
        } else {
            bSPS30 = true;
            ESP_LOGI(TAG, "+++ Датчик частиц SPS30 успешно запущен через C API (Канал 17)");
        }
    }

    logToWeb("End initing sensors");
    switchToTouchBus();
}



void getSensData(stSens *vSensVal)
{
    if (xSensorsMutex != NULL)
        xSemaphoreTake(xSensorsMutex, portMAX_DELAY);
    resetActualSensVal(vSensVal);
    if (xSensorsMutex != NULL)
        xSemaphoreGive(xSensorsMutex);

    // 1. Опрос DS18B20 (Канал 18)
    if (bDS)
    {
        if (sDS.readTemp())
        {
            float t_ds = sDS.getTemp();
            // Датчик не может выдать ровно 85.0 при старте, если он исправен
            if (t_ds > -55.0 && t_ds < 85.0 && t_ds != 85.0)
            {
                vSensVal[18].value = t_ds + fDS_Tfix;
                vSensVal[18].actual = true;
            }
            else
            {
                // Если датчик выдал мусор или отключился в процессе работы
                vSensVal[18].actual = false;
                ESP_LOGE(TAG, "[DS18B20] Ошибка чтения данных или сброс питания (прочитано: %.2f)", t_ds);
            }
        }
        sDS.requestTemp();
    }

    switchToMeteoBus();
    bool skipWeather = (currentHeatState != HEAT_IDLE);

    // 2. Опрос TMP117 (Канал 0)
    if (bTMP117)
    {
        float t_tmp = sTMP117.readTempC();
        if (t_tmp > -50.0 && t_tmp < 60.0)
        {
            vSensVal[0].value = t_tmp + fTMP_Tfix;
            vSensVal[0].actual = true;
        }
    }

    // 3. Уличный BME280 (Каналы 4, 5, 6)
    if (bBME_e)
    {
        vSensVal[4].value = sBME_e.readTempC() + fBME_e_Tfix;
        vSensVal[5].value = sBME_e.readFloatHumidity();
        vSensVal[6].value = sBME_e.readFloatPressure() * 0.007500638;
        if (vSensVal[4].value > -50.0 && vSensVal[4].value < 50.0)
            vSensVal[4].actual = true;
        if (vSensVal[5].value > 5.0 && vSensVal[5].value < 101.0)
            vSensVal[5].actual = true;
        if (vSensVal[6].value > 600.0 && vSensVal[6].value < 811.0)
            vSensVal[6].actual = true;
    }
    // 4. Уличный HTU21D / SHT21 (Каналы 7, 8)
    if (bHTU_e && !skipWeather)
    {
        sSHT21_e.read();
        vSensVal[7].value = sSHT21_e.getTemperature() + fHTU_e_Tfix;
        vSensVal[8].value = sSHT21_e.getHumidity();
        if (vSensVal[7].value > -50.0 && vSensVal[7].value < 50.0)
            vSensVal[7].actual = true;
        if (vSensVal[8].value > 5.0 && vSensVal[8].value < 101.0)
            vSensVal[8].actual = true;
    }
    // 5. Уличный SHT40 (Каналы 9, 10)
    if (bSHT_e)
    {
        if (sSHT40_e.read())
        { // Вызов чтения данных SHT4x
            vSensVal[9].value = sSHT40_e.getTemperature() + fSHT_e_Tfix;
            vSensVal[10].value = sSHT40_e.getHumidity();

            if (vSensVal[9].value > -50.0 && vSensVal[9].value < 50.0)
                vSensVal[9].actual = true;
            if (vSensVal[10].value > 5.0 && vSensVal[10].value < 101.0)
                vSensVal[10].actual = true;
        }
        else
        {
            vSensVal[9].actual = false;
            vSensVal[10].actual = false;
        }
    }

    // 6. Комнатный BME280 (Каналы 11, 12, 13)
    if (bBME_i)
    {
        vSensVal[11].value = sBME_i.readTempC();
        vSensVal[12].value = sBME_i.readFloatHumidity();
        vSensVal[13].value = sBME_i.readFloatPressure() * 0.007500638;
        if (vSensVal[11].value > -50.0 && vSensVal[11].value < 50.0)
            vSensVal[11].actual = true;
        if (vSensVal[12].value > 5.0 && vSensVal[12].value < 101.0)
            vSensVal[12].actual = true;
        if (vSensVal[13].value > 600.0 && vSensVal[13].value < 811.0)
            vSensVal[13].actual = true;
    }
    // 7. Комнатный SCD30 (Каналы 14, 15, 16)
    if (bSCD30_i)
    {
        if (sSCD30_i.dataAvailable())
        {
            vSensVal[14].value = sSCD30_i.getTemperature();
            vSensVal[15].value = sSCD30_i.getHumidity();
            vSensVal[16].value = sSCD30_i.getCO2();
            if (vSensVal[14].value > -50.0 && vSensVal[14].value < 50.0)
                vSensVal[14].actual = true;
            if (vSensVal[15].value > 5.0 && vSensVal[15].value < 101.0)
                vSensVal[15].actual = true;
            if (vSensVal[16].value > 0.0)
                vSensVal[16].actual = true;
        }
    }
    // 8. Опрос SPS30 (Канал 17)
    if (bSPS30)
    {
        struct sps30_measurement measurement;
        if (sps30_read_measurement(&measurement) == 0)
        {
            vSensVal[17].value = measurement.mc_2p5;
            vSensVal[17].actual = true;
        }
    }
    switchToTouchBus();
}
void resetActualSensVal(stSens *vSensVal)
{
    for (int i = 0; i < SensUnit; i++)
    {
        vSensVal[i].actual = false;
    }
}
void SCD30Calibration()
{
    if (bSCD30_i)
    {
        switchToMeteoBus();
        sSCD30_i.setForcedRecalibrationFactor(415);
        switchToTouchBus();
        ESP_LOGW(TAG, "Выполнена калибровка SCD30 на 415 ppm.");
    }
}

void triggerHeatCycle()
{
    if (currentHeatState != HEAT_IDLE)
        return;
    if (millis() - lastHeatTimestamp < 900000) // Защита: не чаще раза в 15 минут
        return;

    switchToMeteoBus();

    // 1. Включаем нагрев на HTU21D/SHT21
    if (bHTU_e)
    {
        sSHT21_e.heatOn();
        logToWeb("[HEAT] Нагрев SHT21/HTU включен.");
    }

    // 2. Включаем нагрев на SHT40 через флаг встроенной термозащиты библиотеки
    if (bSHT_e)
    {
        sSHT40_e.setHeatProtection(true);
        logToWeb("[HEAT] Нагрев SHT40 включен.");
    }

    switchToTouchBus();
    heatTimer = millis();
    lastHeatTimestamp = millis();
    currentHeatState = HEAT_WARMING;
}

void processAsyncHeating()
{
    switch (currentHeatState)
    {
    case HEAT_IDLE:
        break;

    case HEAT_WARMING:
        // Ждем HEATTIME (6 секунд) удержания нагрева
        if (millis() - heatTimer >= HEATTIME)
        {
            switchToMeteoBus();

            // Выключаем нагреватель HTU21D
            if (bHTU_e)
                sSHT21_e.heatOff();

            // Отключаем нагреватель SHT40
            if (bSHT_e)
                sSHT40_e.setHeatProtection(false);

            switchToTouchBus();
            logToWeb("[HEAT] Нагрев выключен. Стадия охлаждения кристаллов датчиков...");
            heatTimer = millis();
            currentHeatState = HEAT_COOLING;
        }
        break;

    case HEAT_COOLING:
        // Полное остывание сенсоров до честной температуры воздуха (20 секунд)
        if (millis() - heatTimer >= COOLTIME)
        {
            currentHeatState = HEAT_IDLE;
            logToWeb("[HEAT] Цикл прогрева и охлаждения полностью завершен.");
        }
        break;
    }
}

void logAllSensorsDebug(stSens *vSensVal)
{
    if (xSensorsMutex == NULL)
        return;
    if (xSemaphoreTake(xSensorsMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        ESP_LOGI("SENS_DATA", "================ МЕТЕОСТАНЦИЯ: ТЕКУЩИЕ ДАННЫЕ ================");
        for (int i = 0; i < SensUnit; i++)
        {
            if (vSensVal[i].actual)
            {
                ESP_LOGI("SENS_DATA", " [%d] %-15s (%-15s) = %.2f %s [MQTT: %s]",
                         i,
                         vSensVal[i].name,
                         vSensVal[i].id_name,
                         vSensVal[i].value,
                         vSensVal[i].unit,
                         vSensVal[i].mqttId);
            }
            else
            {
                ESP_LOGD("SENS_DATA", " [%d] %-15s -> Датчик неактивен/отключен", i, vSensVal[i].name);
            }
        }
        ESP_LOGI("SENS_DATA", "==============================================================");
        xSemaphoreGive(xSensorsMutex);
    }
    else
    {
        ESP_LOGW("SENS_DATA", "Лог пропущен: структура датчиков занята другим потоком RTOS!");
    }
}
