#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <deque>

#include "settings.h"
#include "wifi_server.h"
#include "strct.h"
#include "sys_time.h"
#include "OutToScr.h"
#include "sensors.h"
#include <JC3248W535.h>

#include <hal/wdt_hal.h>
#include <soc/timer_group_reg.h>


// Выделяем аппаратный блок I2C_NUM_1 под метео-датчики
TwoWire WireSensors = TwoWire(1);

bool NarodmonTcpPublish();
JC3248W535_Display *display = nullptr;
JC3248W535_Touch *touch = nullptr; // Добавляем объект тачскрина

#define PIN_BACKLIGHT 1
#define PIN_RADAR 3
#define PIN_SENS_SDA 17
#define PIN_SENS_SCL 18
#define PIN_TOUCH_SDA 4
#define PIN_TOUCH_SCL 8

stSens vSensVal[SensUnit];
const size_t MAX_LOG_LINES = 100;
std::deque<String> webLogs;
SemaphoreHandle_t xLogMutex = NULL;
SemaphoreHandle_t xLcdMutex = NULL;
SemaphoreHandle_t xSensorsMutex = NULL;
boolean bConnWiFi = false;
bool bBooting = true;
unsigned long isrRadarTime = 0;

TaskHandle_t httpTaskHandle = NULL;
static TaskHandle_t xRadarHandle = NULL;
TaskHandle_t wifiWatchdogTaskHandle = NULL;

void logToWeb(String text)
{
    String timeStr = getSystemTimeShortStr();
    if (xLogMutex != NULL && xSemaphoreTake(xLogMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        webLogs.push_back(timeStr + text);
        if (webLogs.size() > MAX_LOG_LINES)
            webLogs.pop_front();
        if (bBooting && display != nullptr)
        {
            if (xLcdMutex != NULL && xSemaphoreTake(xLcdMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                auto gfx = display->getCanvas();
                if (gfx)
                {
                    String lowerText = text;
                    lowerText.toLowerCase();
                    if (lowerText.indexOf("not found") != -1 || lowerText.indexOf("failed") != -1 || lowerText.indexOf("error") != -1)
                        gfx->setTextColor(0xF800);
                    else if (lowerText.indexOf("connecting") != -1 || lowerText.indexOf("wait") != -1)
                        gfx->setTextColor(0xFFE0);
                    else
                        gfx->setTextColor(0xFFFF);
                    gfx->println(text);
                    display->flush();
                }
                xSemaphoreGive(xLcdMutex);
            }
        }
        xSemaphoreGive(xLogMutex);
    }
    Serial.println(timeStr + text);
}

static void IRAM_ATTR vfnRadarISR(void *vpArg)
{
    uint32_t ulGPIONumber = (uint32_t)vpArg;
    if (gpio_get_level((gpio_num_t)ulGPIONumber) == 1)
        xTaskNotifyFromISR(xRadarHandle, 0, eSetBits, NULL);
    portYIELD_FROM_ISR();
}

static void vfnRadarTask(void *vpArg)
{
    uint32_t ulNotifiedValue = 0;
    while (1)
    {
        xTaskNotifyWait(0x00, 0xFFFFFFFF, &ulNotifiedValue, portMAX_DELAY);
        if ((millis() - isrRadarTime > 3000ul) && (millis() > 10000ul))
        {
            logToWeb("[RADAR] Обнаружено присутствие");
            ScreenOn();
            isrRadarTime = millis();
        }
    }
}

static void vfnSensorUpdateTask(void *vpArg)
{
    esp_task_wdt_add(NULL); // Обязательная регистрация таска в WDT Arduino v3
    while (1)
    {
        esp_task_wdt_reset();
        if (xSensorsMutex != NULL && xSemaphoreTake(xSensorsMutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            getSensData(vSensVal);
            xSemaphoreGive(xSensorsMutex);
        }
        // Безопасный сон с периодическим сбросом WDT
        for (int i = 0; i < 60; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
    }
}

static void vfnNetworkSendTask(void *vpArg)
{
    esp_task_wdt_add(NULL);
    while (1)
    {
        // Заменяем длинный Delay на частые итерации со сбросом WDT (10 минут = 600 секунд)
        for (int i = 0; i < 600; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
        if (xSensorsMutex != NULL && xSemaphoreTake(xSensorsMutex, pdMS_TO_TICKS(200)) == pdTRUE)
        {
            getSensData(vSensVal);
            xSemaphoreGive(xSensorsMutex);
        }
        if (bConnWiFi)
        {
            esp_task_wdt_reset();
            NarodmonTcpPublish();
            esp_task_wdt_reset();
        }
        if (vSensVal[8].value > 90.0 || vSensVal[10].value > 90.0)
            triggerHeatCycle();
    }
}

void vWifiWatchdogTask(void *pvParameters)
{
    esp_task_wdt_add(NULL);
    int disconnectCount = 0;
    for (;;)
    {
        esp_task_wdt_reset();
        if (WiFi.status() != WL_CONNECTED)
        {
            bConnWiFi = false;
            disconnectCount++;
            if (disconnectCount >= 3)
            {
                WiFi.disconnect();
                for (int i = 0; i < 60; i++)
                {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_task_wdt_reset();
                }
                WiFi.begin(CONF_SSID, CONF_PASSWORD);
                for (int i = 0; i < 10; i++)
                {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_task_wdt_reset();
                }
                disconnectCount = 0;
            }
        }
        else if (!bConnWiFi)
        {
            bConnWiFi = true;
            disconnectCount = 0;
        }
        for (int i = 0; i < 300; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_task_wdt_reset();
        }
    }
}

void setup_wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(10));
    logToWeb("[WIFI] Подключение к " + String(CONF_SSID));
    WiFi.begin(CONF_SSID, CONF_PASSWORD);
    int wifiCounter = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (++wifiCounter > 20)
        {
            logToWeb("[WIFI] --- Тайм-аут.");
            bConnWiFi = false;
            return;
        }
    }
    bConnWiFi = true;
    logToWeb("[WIFI] Подключено! IP: " + WiFi.localIP().toString());
}

void setup() {
    Serial.begin(115200); delay(800); 

    // ГАРАНТИРОВАННОЕ ОТКЛЮЧЕНИЕ АППАРАТНЫХ ВАТЧДОГОВ (TG0 и TG1) НА ВРЕМЯ SETUP
    wdt_hal_context_t wdt_ctx0 = {.inst = WDT_MWDT0, .mwdt_dev = &TIMERG0};
    wdt_hal_write_protect_disable(&wdt_ctx0);
    wdt_hal_disable(&wdt_ctx0);
    wdt_hal_write_protect_enable(&wdt_ctx0);

    wdt_hal_context_t wdt_ctx1 = {.inst = WDT_MWDT1, .mwdt_dev = &TIMERG1};
    wdt_hal_write_protect_disable(&wdt_ctx1);
    wdt_hal_disable(&wdt_ctx1);
    wdt_hal_write_protect_enable(&wdt_ctx1);

    xSensorsMutex = xSemaphoreCreateMutex(); 
    xLogMutex = xSemaphoreCreateMutex(); 
    xLcdMutex = xSemaphoreCreateMutex();

    Serial.println("[Дисплей] Динамический старт графики...");
    display = new JC3248W535_Display(); // Штатный конструктор дисплея
    if (!display->begin()) { Serial.println("[КРИТ] Ошибка QSPI дисплея!"); while(1) delay(100); }

    touch = new JC3248W535_Touch();
    touch->begin(); // Инициализация тача (автоматически займет bus=0 на пинах 4/8)

    // Запускаем метео-шину строго на блоке №1 (пины 17/18)
    Serial.println("[I2C] Запуск WireSensors (Датчики на IO17/IO18)...");
    WireSensors.begin(PIN_SENS_SDA, PIN_SENS_SCL, 100000); 
    WireSensors.setTimeOut(25);

    auto gfx = display->getCanvas();
    if (gfx) { 
        gfx->setRotation(1); // Задаем ландшафт (ROTATION_90) прямо на холсте
        gfx->fillScreen(0x0000); 
        gfx->setTextSize(1); 
        gfx->setCursor(10, 10); 
        gfx->setTextColor(0xFFFF); 
    }

    logToWeb("==== Инициализация датчиков ====");
    startSens(vSensVal); 
    getSensData(vSensVal);

    setup_wifi();

    if (bConnWiFi) { long gmtOffset_sec = TIMEZONE * 3600; configTime(gmtOffset_sec, daylightOffset_sec, ntpServerName); }

    xTaskCreatePinnedToCore(vfnSensorUpdateTask, "Read Sensors", 5120, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(vfnNetworkSendTask, "Net Send", 4096, NULL, 4, NULL, 0);

    if (xLcdMutex != NULL && xSemaphoreTake(xLcdMutex, pdMS_TO_TICKS(500)) == pdTRUE) { bBooting = false; if (gfx) gfx->fillScreen(0x0000); xSemaphoreGive(xLcdMutex); }

    xTaskCreatePinnedToCore(vHttpServerTask, "Web Server", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(vWifiWatchdogTask, "Wifi WD", 3072, NULL, 1, &wifiWatchdogTaskHandle, 1);

    gpio_config_t xRadarConfig = {0}; xRadarConfig.pin_bit_mask = (1ULL << PIN_RADAR); xRadarConfig.mode = GPIO_MODE_INPUT; xRadarConfig.pull_up_en = GPIO_PULLUP_DISABLE; xRadarConfig.pull_down_en = GPIO_PULLDOWN_DISABLE; xRadarConfig.intr_type = GPIO_INTR_POSEDGE; gpio_config(&xRadarConfig);
    xTaskCreatePinnedToCore(vfnRadarTask, "Radar Task", 2048, NULL, 5, &xRadarHandle, 1);
    gpio_install_isr_service(0); gpio_isr_handler_add((gpio_num_t)PIN_RADAR, vfnRadarISR, (void *)PIN_RADAR);

    // Включаем стандартный Task Watchdog обратно для операционной системы
    esp_task_wdt_config_t wdt_config = { .timeout_ms = 20000, .idle_core_mask = (1 << 0) | (1 << 1), .trigger_panic = true }; 
    esp_task_wdt_reconfigure(&wdt_config);

    logToWeb("==== Система успешно запущена ====");
    if (xLcdMutex != NULL && xSemaphoreTake(xLcdMutex, pdMS_TO_TICKS(100)) == pdTRUE) { ShowTime(); xSemaphoreGive(xLcdMutex); }
}

void loop() { 
    // ОЧИЩЕНО: Ручной сброс ватчдога удален, чтобы не спамить "task not found"
    processAsyncHeating(); 
    processLcdAnimate(); 

    TouchPoint point;
    if (touch != nullptr && touch->read(point)) {
        if (point.touched) {
            ScreenOn(); // Просыпаемся при касании
        }
    }
    vTaskDelay(pdMS_TO_TICKS(30)); 
}

bool NarodmonTcpPublish()
{
    bool hasDataToSend = false;
    for (int i = 0; i < SensUnit; i++)
    {
        if (vSensVal[i].actual && vSensVal[i].mqttId[0] != '\0')
        {
            hasDataToSend = true;
            break;
        }
    }
    if (!hasDataToSend)
        return false;
    String mac = WiFi.macAddress(); // Динамический MAC адрес устройства вместо хардкода
    String buf = "#" + mac + "\n";
    for (int i = 0; i < SensUnit; i++)
    {
        if (vSensVal[i].actual && vSensVal[i].mqttId[0] != '\0')
            buf = buf + "#" + vSensVal[i].mqttId + "#" + String(vSensVal[i].value, 2) + "\n";
    }
    buf = buf + "##\n";
    WiFiClient client;
    client.setTimeout(1000);
    if (!client.connect("narodmon.ru", 8283))
    {
        client.stop();
        return false;
    }
    else
    {
        client.print(buf);
        vTaskDelay(pdMS_TO_TICKS(50));
        client.stop();
        return true;
    }
}