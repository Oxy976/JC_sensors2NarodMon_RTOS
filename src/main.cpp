#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "sensors.h"
#include "OutToScr.h"

// Конфигурация дисплея платы JC3248W535
#define TFT_BL 1
#define TFT_CS 45
#define TFT_SCK 47
#define TFT_D0 21
#define TFT_D1 48
#define TFT_D2 40
#define TFT_D3 39

#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320

#define TOUCH_SDA 4
#define TOUCH_SCL 8
#define TOUCH_INT 3
#define TOUCH_RST 12
#define TOUCH_ADDR 0x3B

// Глобальный массив структур для хранения показаний метеостанции
stSens vSensVal[SensUnit];

volatile bool bSensorsInitDone = false; // Флаг завершения инициализации датчиков

// плвное затухание
#define LCD_TIMEOUT_MS 30000 // 30 секунд
static uint32_t last_touch_time = 0;
static uint8_t lcd_brightness = 255;
static bool is_dimming = false;

void init_backlight_pwm()
{
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 255);
    last_touch_time = millis();
}

// --- Программный Bit-Bang I2C драйвер тачскрина ---
void sw_i2c_delay() { delayMicroseconds(5); }
void sw_i2c_start()
{
    pinMode(TOUCH_SDA, OUTPUT);
    digitalWrite(TOUCH_SDA, HIGH);
    digitalWrite(TOUCH_SCL, HIGH);
    sw_i2c_delay();
    digitalWrite(TOUCH_SDA, LOW);
    sw_i2c_delay();
    digitalWrite(TOUCH_SCL, LOW);
}
void sw_i2c_stop()
{
    pinMode(TOUCH_SDA, OUTPUT);
    digitalWrite(TOUCH_SDA, LOW);
    digitalWrite(TOUCH_SCL, HIGH);
    sw_i2c_delay();
    digitalWrite(TOUCH_SDA, HIGH);
    sw_i2c_delay();
}
bool sw_i2c_write(uint8_t data)
{
    pinMode(TOUCH_SDA, OUTPUT);
    for (uint8_t i = 0; i < 8; i++)
    {
        digitalWrite(TOUCH_SDA, (data & 0x80) ? HIGH : LOW);
        data <<= 1;
        sw_i2c_delay();
        digitalWrite(TOUCH_SCL, HIGH);
        sw_i2c_delay();
        digitalWrite(TOUCH_SCL, LOW);
    }
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    digitalWrite(TOUCH_SCL, HIGH);
    sw_i2c_delay();
    bool ack = (digitalRead(TOUCH_SDA) == LOW);
    digitalWrite(TOUCH_SCL, LOW);
    return ack;
}
uint8_t sw_i2c_read(bool ack)
{
    uint8_t data = 0;
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    for (uint8_t i = 0; i < 8; i++)
    {
        data <<= 1;
        digitalWrite(TOUCH_SCL, HIGH);
        sw_i2c_delay();
        if (digitalRead(TOUCH_SDA))
            data |= 1;
        digitalWrite(TOUCH_SCL, LOW);
        sw_i2c_delay();
    }
    pinMode(TOUCH_SDA, OUTPUT);
    digitalWrite(TOUCH_SDA, ack ? LOW : HIGH);
    digitalWrite(TOUCH_SCL, HIGH);
    sw_i2c_delay();
    digitalWrite(TOUCH_SCL, LOW);
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    return data;
}

// --- Инстансы Дисплея ---
Arduino_DataBus *bus = new Arduino_ESP32QSPI(TFT_CS, TFT_SCK, TFT_D0, TFT_D1, TFT_D2, TFT_D3, 40000000);
Arduino_AXS15231B *g = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
Arduino_GFX *gfx = new Arduino_Canvas(320, 480, g, 0, 0, 0);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{

    // Если инициализация датчиков не завершена — полностью игнорируем опрос тача
    if (!bSensorsInitDone)
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    uint8_t read_cmd[] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};
    uint8_t res[8] = {0};

    sw_i2c_start();
    if (!sw_i2c_write(TOUCH_ADDR << 1))
    {
        sw_i2c_stop();
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    for (uint8_t i = 0; i < 8; i++)
        sw_i2c_write(read_cmd[i]);
    sw_i2c_stop();
    sw_i2c_delay();

    sw_i2c_start();
    if (!sw_i2c_write((TOUCH_ADDR << 1) | 1))
    {
        sw_i2c_stop();
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    for (uint8_t i = 0; i < 7; i++)
        res[i] = sw_i2c_read(true);
    res[7] = sw_i2c_read(false);
    sw_i2c_stop();

    int16_t raw_x = ((res[2] & 0x0F) << 8) | res[3];
    int16_t raw_y = ((res[4] & 0x0F) << 8) | res[5];
    uint8_t status = res[1];

    if (status != 0 && raw_x < 320 && raw_y < 480)
    {
        data->point.x = raw_x;
        data->point.y = raw_y;
        data->state = LV_INDEV_STATE_PR;

        // СБРОС ТАЙМЕРА И МГНОВЕННОЕ ВКЛЮЧЕНИЕ ПРИ КАСАНИИ
        last_touch_time = millis();
        if (lcd_brightness < 255)
        {
            lcd_brightness = 255;
            ledcWrite(TFT_BL, lcd_brightness);
            is_dimming = false;
            ESP_LOGI("LIGHT", "Экран проснулся!");
        }

        ESP_LOGI("TOUCH", "Касание: X=%d, Y=%d (Status: 0x%02X)", raw_x, raw_y, status);

        // ВЫЗОВ  ОТЛАДОЧНОГО ЛОГА ДАННЫХ
        logAllSensorsDebug(vSensVal);

        return;
    }
    data->state = LV_INDEV_STATE_REL;
}

// --- Задача FreeRTOS для работы с сенсорами на Core 1 ---
void TaskSensors(void *pvParameters)
{
    ESP_LOGI("RTOS", "Задача опроса датчиков запущена на Core %d", xPortGetCoreID());
    // startSens(vSensVal);

    for (;;)
    {
        getSensData(vSensVal);
        OutToScr(vSensVal);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    // Инициализируем мьютекс ДО старта датчиков
    xLcdMutex = xSemaphoreCreateMutex();
    // === САМЫЙ ПЕРВЫЙ СТАРТ ШИНЫ WIRE ===
    Wire.begin(17, 18, 100000U);

    init_backlight_pwm();

    // Инициализация пинов тачскрина
    pinMode(TOUCH_SCL, OUTPUT);
    pinMode(TOUCH_SDA, OUTPUT);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);

    gfx->begin();
    gfx->fillScreen(0x0000);
    lv_init();

    // Настройка буферов дисплея LVGL...
    uint32_t buf_size = SCREEN_WIDTH * 32 * sizeof(lv_color_t);
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf)
        disp_draw_buf = (lv_color_t *)ps_malloc(buf_size);
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, SCREEN_WIDTH * 32);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.sw_rotate = 1;
    disp_drv.rotated = LV_DISP_ROT_90;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    init_lvgl_ui();

    // СНАЧАЛА ИНИЦИАЛИЗИРУЕМ ДАТЧИКИ, ЗАТЕМ СТАВИМ ТАЧ ===
    bSensorsInitDone = false;
    startSens(vSensVal);
    bSensorsInitDone = true;

    // Запуск циклической задачи опроса датчиков (внутри неё startSens убираем!)
    xTaskCreatePinnedToCore(TaskSensors, "TaskSensors", 4096, NULL, 2, NULL, 1);

    ESP_LOGI("SYSTEM", "Инициализация метеостанции успешно завершена.");
}

void loop()
{
    lv_timer_handler();
    gfx->flush();

    // Неблокирующий автомат плавного затухания экрана
    uint32_t current_time = millis();
    if (current_time - last_touch_time > LCD_TIMEOUT_MS)
    {
        if (lcd_brightness > 0)
        {
            is_dimming = true;
            lcd_brightness = (lcd_brightness > 5) ? (lcd_brightness - 5) : 0;
            ledcWrite(TFT_BL, lcd_brightness);
        }
    }

    delay(5);
}
