#include "sensors.h"
#include <Wire.h>
#include <esp_task_wdt.h>

extern TwoWire WireSensors;
extern void logToWeb(String text);
#define PIN_DS18B20 26

// Глобальные динамические указатели (без изменений)
OneWire *oneWire = nullptr;
DallasTemperature *sDS = nullptr;
DeviceAddress sensorAddress;
boolean bDS = false;

ClimateGuard_RadSens1v2 *sRadSens = nullptr;
boolean bRAD = false;
int vNumPulse = 0;
unsigned long RADstartTime = 0;

//BME280I2C *sBME_i = nullptr;
//BME280I2C *sBME_e = nullptr;
// Заменяем старые указатели на новые типы Adafruit
Adafruit_BME280 *sBME_i = nullptr;
Adafruit_BME280 *sBME_e = nullptr;

boolean bBME_i = false;
boolean bBME_e = false;

Si7021 *sHTU_e = nullptr;
boolean bHTU_e = false;

#define SHT31_ADDRESS 0x44
SHT31 *sSHT_e = nullptr;
boolean bSHT_e = false;

enum HeatState { HEAT_IDLE, HEAT_WARMING, HEAT_COOLING };
static HeatState currentHeatState = HEAT_IDLE;
static unsigned long heatTimer = 0;
static unsigned long lastHeatTimestamp = 0;

SCD30 *sSCD30_i = nullptr;
boolean bSCD30_i = false;

String sC = "C";
String GRAD = "\u00B0" + sC;

float fDS_Tfix = -0.3;
float fBME_e_Tfix = 0.0;
float fHTU_e_Tfix = -0.3;
float fSHT_e_Tfix = -0.7;

bool pingI2CAddress(uint8_t address) {
        WireSensors.beginTransmission(address);
        return (WireSensors.endTransmission() == 0);
}

// === АВТОНОМНЫЙ СКАНЕР МЕТЕО-ШИНЫ I2C ===
void scanI2CBus() {
    Serial.println("\n[I2C SCAN] >>> Старт сканирования шины датчиков (GPIO17/GPIO18)...");
    byte error, address;
    int nDevices = 0;
    
    for (address = 1; address < 127; address++) {
        WireSensors.beginTransmission(address);
        error = WireSensors.endTransmission();
        
        if (error == 0) {
            Serial.printf("[I2C SCAN] Найдено устройство по адресу: 0x%02X\n", address);
            nDevices++;
        } else if (error == 4) {
            Serial.printf("[I2C SCAN] Неизвестная ошибка (КЗ или наводка) на адресе: 0x%02X\n", address);
        }
    }
    
    if (nDevices == 0) {
        Serial.println("[I2C SCAN] !!! На шине WireSensors пустует. Датчики не отвечают.");
        Serial.println("[I2C SCAN] Проверьте: 1) Питание датчиков 3.3V, 2) Подтяжку резисторами 4.7к к 3.3V, 3) Пины SDA/SCL.");
    } else {
        Serial.printf("[I2C SCAN] <<< Сканирование завершено. Всего устройств на метео-шине: %d\n\n", nDevices);
    }
}

void startSens(stSens *vSensVal) {
        static const char *TAG = "sensors_init";
        ESP_LOGD(TAG, "Start init sensors");

        // 1. ЗАПУСКАЕМ НАШ СКАНЕР ДО ВСЕХ ИНИЦИАЛИЗАЦИЙ
        scanI2CBus();

        // Заполнение структуры данных vSensVal (сохраняем оригинальную структуру)
        vSensVal[0].name = "extDS_Temp"; vSensVal[1].name = "Rad_dyn"; vSensVal[2].name = "Rad_stat"; vSensVal[3].name = "Rad_pulses";
        vSensVal[4].name = "extBME_Temp"; vSensVal[5].name = "extBME_Hum"; vSensVal[6].name = "extBME_Press"; vSensVal[7].name = "extHTU_Temp";
        vSensVal[8].name = "extHTU_Hum"; vSensVal[9].name = "extSHT_Temp"; vSensVal[10].name = "extSHT_Hum"; vSensVal[11].name = "intBME_Temp";
        vSensVal[12].name = "intBME_Hum"; vSensVal[13].name = "intBME_Press"; vSensVal[14].name = "intSCD30_Temp"; vSensVal[15].name = "intSCD30_Hum";
        vSensVal[16].name = "intSCD30_CO2";

        for (int i = 0; i < SensUnit; i++) vSensVal[i].actual = false;

        vSensVal[0].id_name = "temp_ext_ds"; vSensVal[1].id_name = "rad_dyn"; vSensVal[2].id_name = "rad_stat"; vSensVal[3].id_name = "rad_pulses";
        vSensVal[4].id_name = "temp_ext_bme"; vSensVal[5].id_name = "hum_ext_bme"; vSensVal[6].id_name = "press_ext_bme"; vSensVal[7].id_name = "temp_ext_htu";
        vSensVal[8].id_name = "hum_ext_htu"; vSensVal[9].id_name = "temp_ext_sht"; vSensVal[10].id_name = "hum_ext_sht"; vSensVal[11].id_name = "temp_int_bme";
        vSensVal[12].id_name = "hum_int_bme"; vSensVal[13].id_name = "press_int_bme"; vSensVal[14].id_name = "temp_int_scd30"; vSensVal[15].id_name = "hum_int_scd30";
        vSensVal[16].id_name = "co2_int_scd30";

        vSensVal[0].mqttId = "T0"; vSensVal[2].mqttId = "R0"; vSensVal[9].mqttId = "T1"; vSensVal[10].mqttId = "H1";
        vSensVal[11].mqttId = "T2"; vSensVal[12].mqttId = "H2"; vSensVal[13].mqttId = "P2"; vSensVal[16].mqttId = "CO2";

        logToWeb("--- Dallas DS18B20: Пропущен для теста I2C");
        bDS = false;

        // *** 2. Geiger (RadSens) ***
        if (pingI2CAddress(RS_DEFAULT_I2C_ADDRESS)) {
                sRadSens = new ClimateGuard_RadSens1v2(RS_DEFAULT_I2C_ADDRESS);
                if (sRadSens->radSens_init()) {
                        logToWeb("+++ RadSens sensor found"); bRAD = true;
                        sRadSens->setSensitivity(105); sRadSens->setHVGeneratorState(true);
                        RADstartTime = millis(); vSensVal[1].unit = "mRg/h"; vSensVal[1].unit = "mRg/h"; vSensVal[1].unit = "#";
                }
        } else bRAD = false;

        // *** 3. Уличный BME280 (0x77) с новым драйвером ***
        if (pingI2CAddress(0x77)) {
                sBME_e = new Adafruit_BME280();
                // Передаем адрес И ссылку на нашу шину WireSensors!
                if (sBME_e->begin(0x77, &WireSensors)) { 
                        logToWeb("+++ BME280 ext found (0x77)"); 
                        bBME_e = true; 
                        vSensVal[4].unit = GRAD; vSensVal[5].unit = "%"; vSensVal[6].unit = "mmHg"; 
                } else {
                        logToWeb("--- BME280 ext: ошибка begin");
                        bBME_e = false;
                }
        } else bBME_e = false;


        // *** 4. HTU21D/Si7021 ***
        if (pingI2CAddress(0x40)) {
                sHTU_e = new Si7021();
                if (sHTU_e->begin()) { logToWeb("+++ HTU21D/Si7021 ext found"); bHTU_e = true; vSensVal[7].unit = GRAD; vSensVal[8].unit = "%"; }
        } else bHTU_e = false;

        // *** 5. SHT31 ***
        if (pingI2CAddress(SHT31_ADDRESS)) {
                sSHT_e = new SHT31(SHT31_ADDRESS);
                if (sSHT_e->begin()) { logToWeb("+++ SHT31 ext found"); bSHT_e = true; if (sSHT_e->isHeaterOn()) sSHT_e->heatOff(); vSensVal[9].unit = GRAD; vSensVal[10].unit = "%"; }
        } else bSHT_e = false;

             // *** 6. Комнатный BME280 (0x76) с новым драйвером ***
        if (pingI2CAddress(0x76)) {
                sBME_i = new Adafruit_BME280();
                // Передаем адрес И ссылку на нашу шину WireSensors!
                if (sBME_i->begin(0x76, &WireSensors)) { 
                        logToWeb("+++ BME280 int found (0x76)"); 
                        bBME_i = true; 
                        vSensVal[11].unit = GRAD; vSensVal[12].unit = "%"; vSensVal[13].unit = "mmHg"; 
                        bBME_e = true; // Дублируем для внешнего вывода, если датчик один
                } else {
                        logToWeb("--- BME280 int: ошибка begin");
                        bBME_i = false;
                }
        } else bBME_i = false;

        // *** 7. SCD30 ***
        if (pingI2CAddress(0x61)) {
                sSCD30_i = new SCD30();
                if (sSCD30_i->begin(Wire, false)) { logToWeb("+++ SCD30 int found"); bSCD30_i = true; vSensVal[14].unit = GRAD; vSensVal[15].unit = "%"; vSensVal[16].unit = "ppm"; }
        } else bSCD30_i = false;
        
        logToWeb("==== Все датчики проверены ====");
}


void getSensData(stSens *vSensVal)
{
        resetActualSensVal(vSensVal);
        bool skipWeather = (currentHeatState != HEAT_IDLE);

        if (bDS && sDS != nullptr)
        {
                vSensVal[0].value = sDS->getTempC(sensorAddress);
                vSensVal[0].value += fDS_Tfix;
                sDS->requestTemperatures();
                if (vSensVal[0].value > -50 && vSensVal[0].value < 50)
                        vSensVal[0].actual = true;
                vTaskDelay(pdMS_TO_TICKS(5));
        }

        if (bRAD && sRadSens != nullptr)
        {
                if (sRadSens->getData())
                {
                        vSensVal[1].value = sRadSens->getRadIntensyDyanmic();
                        vSensVal[2].value = sRadSens->getRadIntensyStatic();
                        vSensVal[3].value = sRadSens->getNumberOfPulses();
                        vSensVal[1].actual = true;

                        vSensVal[2].actual = true;
                        vSensVal[3].actual = true;
                }
        }
        if (bBME_e && sBME_e != nullptr) {
                vSensVal[4].value = sBME_e->readTemperature();
                vSensVal[5].value = sBME_e->readHumidity();
                vSensVal[6].value = sBME_e->readPressure(); // Возвращает Pa
                vSensVal[6].value = (vSensVal[6].value * 0.007500638); // Переводим в мм рт. ст.
                
                vSensVal[4].value += fBME_e_Tfix;
                if (vSensVal[4].value > -50 && vSensVal[4].value < 50) vSensVal[4].actual = true;
                if (vSensVal[5].value > 5 && vSensVal[5].value < 101) vSensVal[5].actual = true;
                if (vSensVal[6].value > 600 && vSensVal[6].value < 811) vSensVal[6].actual = true;
        }
        if (bHTU_e && sHTU_e != nullptr && !skipWeather)
        {
                if (sHTU_e->read())
                {
                        vSensVal[7].value = sHTU_e->getTemperature();
                        vSensVal[8].value = sHTU_e->getHumidity();
                        vSensVal[7].value += fHTU_e_Tfix;
                        if (vSensVal[7].value > -50 && vSensVal[7].value < 50)
                                vSensVal[7].actual = true;
                        if (vSensVal[8].value > 5 && vSensVal[8].value < 101)
                                vSensVal[8].actual = true;
                }
        }
        if (bSHT_e && sSHT_e != nullptr && !skipWeather)
        {
                if (sSHT_e->read())
                {
                        vSensVal[9].value = sSHT_e->getTemperature();
                        vSensVal[10].value = sSHT_e->getHumidity();
                        vSensVal[9].value += fSHT_e_Tfix;
                        if (vSensVal[9].value > -50 && vSensVal[9].value < 50)
                                vSensVal[9].actual = true;
                        if (vSensVal[10].value > 5 && vSensVal[10].value < 101)
                                vSensVal[10].actual = true;
                        if (vSensVal[10].value >= 100)
                                sSHT_e->reset();
                }
        }
        if (bBME_i && sBME_i != nullptr) {
                vSensVal[11].value = sBME_i->readTemperature();
                vSensVal[12].value = sBME_i->readHumidity();
                vSensVal[13].value = sBME_i->readPressure();
                vSensVal[13].value = (vSensVal[13].value * 0.007500638);
                
                if (vSensVal[11].value > -50 && vSensVal[11].value < 50) vSensVal[11].actual = true;
                if (vSensVal[12].value > 5 && vSensVal[12].value < 101) vSensVal[12].actual = true;
                if (vSensVal[13].value > 600 && vSensVal[13].value < 811) vSensVal[13].actual = true;
        }
        if (bSCD30_i && sSCD30_i != nullptr)
        {
                if (sSCD30_i->dataAvailable())
                {
                        vSensVal[14].value = sSCD30_i->getTemperature();
                        vSensVal[15].value = sSCD30_i->getHumidity();
                        vSensVal[16].value = sSCD30_i->getCO2();
                        if (vSensVal[14].value > -50 && vSensVal[14].value < 50)
                                vSensVal[14].actual = true;
                        if (vSensVal[15].value > 5 && vSensVal[15].value < 101)
                                vSensVal[15].actual = true;
                        vSensVal[16].actual = true;
                }
        }
}
void resetActualSensVal(stSens *vSensVal)
{
        for (int i = 0; i < SensUnit; i++)
                vSensVal[i].actual = false;
}
extern void SCD30Calibration()
{
        if (sSCD30_i != nullptr)
                sSCD30_i->setForcedRecalibrationFactor(415);
}
const unsigned long ASYNC_HEAT_TIME = 6000;
const unsigned long ASYNC_COOL_TIME = 20000;
void triggerHeatCycle()
{
        if (currentHeatState != HEAT_IDLE)
                return;
        if (millis() - lastHeatTimestamp < 900000)
                return;
        if (bHTU_e && sHTU_e != nullptr)
                sHTU_e->heatOn();
        if (bSHT_e && sSHT_e != nullptr)
                sSHT_e->heatOn();
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
                if (millis() - heatTimer >= ASYNC_HEAT_TIME)
                {
                        if (bHTU_e && sHTU_e != nullptr)
                                sHTU_e->heatOff();
                        if (bSHT_e && sSHT_e != nullptr)
                                sSHT_e->heatOff();
                        heatTimer = millis();
                        currentHeatState = HEAT_COOLING;
                }
                break;
        case HEAT_COOLING:
                if (millis() - heatTimer >= ASYNC_COOL_TIME)
                        currentHeatState = HEAT_IDLE;
                break;
        }
}