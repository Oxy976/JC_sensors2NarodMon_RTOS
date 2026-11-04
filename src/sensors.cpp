#include "sensors.h"
#include <esp_task_wdt.h>
#include <esp_log.h>

static const char* TAG = "SENSORS";

// Распиновка JC3248W535
#define PIN_SENS_SDA 17
#define PIN_SENS_SCL 18
#define PIN_TOUCH_SDA 4
#define PIN_TOUCH_SCL 8

SemaphoreHandle_t xLcdMutex = NULL; 
SemaphoreHandle_t xSensorsMutex = NULL; 

static BME280 bmeSettingsExt; 
static BME280 bmeSettingsInt; 

BME280 *sBME_e = &bmeSettingsExt;
BME280 *sBME_i = &bmeSettingsInt;

boolean bBME_e = false;
boolean bBME_i = false;
float fBME_e_Tfix = 0.0;

void switchToMeteoBus() {
    if (xLcdMutex != NULL) {
        xSemaphoreTake(xLcdMutex, portMAX_DELAY);
    }
    
    // Временно приглушаем логи ядра до уровня Warning, чтобы скрыть сообщение "Initializing..."
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_WARN);
    
    Wire.end(); // Корректно завершаем работу шины для смены пинов
    Wire.begin(PIN_SENS_SDA, PIN_SENS_SCL, 100000U);
    
    // Возвращаем глобальный уровень логирования (уровень Debug)
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_DEBUG);
    
    vTaskDelay(pdMS_TO_TICKS(10)); 
}

void switchToTouchBus() {
    // Бесшумно закрываем аппаратную шину перед уходом на программный тач
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_WARN);
    Wire.end();
    esp_log_level_set("esp32-hal-i2c", ESP_LOG_DEBUG);

    // Возвращаем пины 4 и 8 в режим цифрового GPIO для работы Bit-Bang тача
    pinMode(PIN_TOUCH_SDA, INPUT_PULLUP);
    pinMode(PIN_TOUCH_SCL, OUTPUT);
    digitalWrite(PIN_TOUCH_SCL, HIGH);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    if (xLcdMutex != NULL) {
        xSemaphoreGive(xLcdMutex);
    }
}

void startSens(stSens *vSensVal) {
    if (xSensorsMutex == NULL) {
        xSensorsMutex = xSemaphoreCreateMutex();
    }

    if (xSensorsMutex != NULL) xSemaphoreTake(xSensorsMutex, portMAX_DELAY);
    resetActualSensVal(vSensVal);
    if (xSensorsMutex != NULL) xSemaphoreGive(xSensorsMutex);

    vTaskDelay(pdMS_TO_TICKS(100)); 

    // Первая и единственная базовая инициализация Wire
    Wire.begin(PIN_SENS_SDA, PIN_SENS_SCL, 100000U);

    sBME_e->setI2CAddress(0x77);
    sBME_i->setI2CAddress(0x76);

    if (sBME_e->beginI2C(Wire)) { bBME_e = true; ESP_LOGI(TAG, "[ИНИЦИАЛИЗАЦИЯ] Уличный BME280 [0x77] запущен."); }
    if (sBME_i->beginI2C(Wire)) { bBME_i = true; ESP_LOGI(TAG, "[ИНИЦИАЛИЗАЦИЯ] Комнатный BME280 [0x76] запущен."); }

    switchToTouchBus();
}

void getSensData(stSens *vSensVal) {
    if (xSensorsMutex != NULL) xSemaphoreTake(xSensorsMutex, portMAX_DELAY);
    resetActualSensVal(vSensVal);
    if (xSensorsMutex != NULL) xSemaphoreGive(xSensorsMutex);

    switchToMeteoBus();

    float t_ext = NAN, h_ext = NAN, p_ext = NAN;
    float t_int = NAN, h_int = NAN, p_int = NAN;

    if (bBME_e && sBME_e->beginI2C(Wire)) {
        t_ext = sBME_e->readTempC();
        h_ext = sBME_e->readFloatHumidity();
        p_ext = sBME_e->readFloatPressure();
    }

    if (bBME_i && sBME_i->beginI2C(Wire)) {
        t_int = sBME_i->readTempC();
        h_int = sBME_i->readFloatHumidity();
        p_int = sBME_i->readFloatPressure();
    }

    switchToTouchBus();

    if (xSensorsMutex != NULL) xSemaphoreTake(xSensorsMutex, portMAX_DELAY);

    // Индексы 4, 5, 6 — Улица
    if (!isnan(t_ext) && !isnan(h_ext) && !isnan(p_ext)) {
        vSensVal[4].value = t_ext + fBME_e_Tfix; 
        vSensVal[5].value = h_ext; 
        vSensVal[6].value = p_ext * 0.007500638;
        vSensVal[4].actual = true; vSensVal[5].actual = true; vSensVal[6].actual = true;
        
        ESP_LOGI(TAG, "[BME280-УЛИЦА]  T: %.2f C, H: %.2f %%, P: %.2f mmHg", vSensVal[4].value, vSensVal[5].value, vSensVal[6].value);
    } else if (bBME_e) {
        ESP_LOGW(TAG, "[BME280-УЛИЦА] Ошибка чтения данных!");
    }

    // Индексы 11, 12, 13 — Комната
    if (!isnan(t_int) && !isnan(h_int) && !isnan(p_int)) {
        vSensVal[11].value = t_int; 
        vSensVal[12].value = h_int; 
        vSensVal[13].value = p_int * 0.007500638;
        vSensVal[11].actual = true; vSensVal[12].actual = true; vSensVal[13].actual = true;
        
        ESP_LOGI(TAG, "[BME280-КОМНАТА] T: %.2f C, H: %.2f %%, P: %.2f mmHg", vSensVal[11].value, vSensVal[12].value, vSensVal[13].value);
    } else if (bBME_i) {
        ESP_LOGW(TAG, "[BME280-КОМНАТА] Ошибка чтения данных!");
    }

    if (xSensorsMutex != NULL) xSemaphoreGive(xSensorsMutex);
}

void resetActualSensVal(stSens *vSensVal) {
    for (int i = 0; i < SensUnit; i++) vSensVal[i].actual = false;
}

void SCD30Calibration() {
    ESP_LOGW(TAG, "Вызов калибровки SCD30 пропущен (датчик не активен).");
}
