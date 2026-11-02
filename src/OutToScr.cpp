#include "OutToScr.h"
#include <JC3248W535.h>

extern JC3248W535_Display *display;

// Опеределение цветов RGB565 для нового экрана
#define SCR_BLACK   0x0000
#define SCR_WHITE   0xFFFF
#define SCR_RED     0xF800
#define SCR_GREEN   0x07E0
#define SCR_BLUE    0x001F
#define SCR_CYAN    0x07FF
#define SCR_YELLOW  0xFFE0
#define SCR_MAGENTA 0xF81F
#define SCR_GRAY    0x7BEF

#define LCD_HOLD_TIME 8000 // Держать экран включенным 8 секунд после активности
#define FADE_SPEED_MS 15

static unsigned long lcdTimer = 0;        
static unsigned long fadeTimer = 0;       
static int currentBrightness = 100;       
static int targetBrightness = 100;        
static bool isScreenActive = true;        

// Таблица компактных двухбуквенных дней недели на русском языке для шрифта дисплея
struct RuDay { const char* name; };
static const RuDay ruDays[] = {
    {"BC"}, {"ПH"}, {"BT"}, {"CP"}, {"4T"}, {"ПT"}, {"CБ"}
};

void ScreenOn() {
    currentBrightness = 100;
    targetBrightness = 100;
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH); // Аппаратно зажигаем транзисторный ключ подсветки GPIO 1
    isScreenActive = true;
    lcdTimer = millis(); 
}

void ScreenOff() {
    targetBrightness = 0;
    isScreenActive = false;
    fadeTimer = millis();
}

bool isScreenFullyActive() {
    return isScreenActive && (currentBrightness > 10);
}

// Плавное неблокирующее управление подсветкой (вызывается в главном loop)
void processLcdAnimate() {
    if (isScreenActive && (millis() - lcdTimer >= LCD_HOLD_TIME)) {
        ScreenOff();
    }

    if (currentBrightness != targetBrightness) {
        if (millis() - fadeTimer >= FADE_SPEED_MS) {
            fadeTimer = millis();
            if (currentBrightness > targetBrightness) {
                currentBrightness -= 5;
                if (currentBrightness < 0) currentBrightness = 0;
            } else {
                currentBrightness += 5;
                if (currentBrightness > 100) currentBrightness = 100;
            }

            // Управляем подсветкой: если 0 — гасим ключ полностью
            if (currentBrightness == 0) {
                digitalWrite(1, LOW); 
            } else {
                digitalWrite(1, HIGH);
            }
        }
    }
}

// Форматирование температуры по вашему алгоритму
String TempToStr(float t) {
    if (isnan(t) || t == 888.8f || t == 888.0f) return "--.- C";
    long T = round(t * 10);
    int t1 = abs(T / 10);
    int t2 = abs(T % 10);
    return (t < 0 ? "-" : "") + String(t1) + "." + String(t2) + " *C";
}

String PressToStr(float p) {
    if (isnan(p) || p == 0.0f) return "--- мм";
    return String(abs((int)p)) + " мм";
}

String HumToStr(float h) {
    if (isnan(h) || h == 0.0f) return "--.- %";
    long H = round(h * 10);
    int h1 = H / 10;
    int h2 = abs(H % 10);
    return String(h1) + "." + String(h2) + " %";
}

String RgToStr(float r) {
    if (isnan(r) || r == 0.0f) return "-.- uS";
    long R = round(r * 10);
    int r1 = R / 10;
    int r2 = abs(R % 10);
    return String(r1) + "." + String(r2) + " uS";
}

// Геометрическая сетка вывода метеоданных (480x320 ландшафт)
void OutStrToScr(String mainTemp, String extHumi, String press, String intTemp, String intHumi, String rad) {
    auto gfx = display->getCanvas();
    if (!gfx) return;

    gfx->fillScreen(SCR_BLACK);

    // Рисуем стильные прямоугольные блоки под обновленное разрешение
    gfx->drawRoundRect(2, 2, 280, 190, 6, SCR_GRAY);       // Главная уличная температура
    gfx->drawRoundRect(285, 2, 192, 93, 6, SCR_GRAY);     // Уличная влажность
    gfx->drawRoundRect(285, 98, 192, 94, 6, SCR_GRAY);    // Атмосферное давление
    
    gfx->drawRoundRect(2, 196, 156, 122, 6, SCR_GRAY);    // Комнатная температура
    gfx->drawRoundRect(162, 196, 156, 122, 6, SCR_GRAY);  // Комнатная влажность
    gfx->drawRoundRect(322, 196, 156, 122, 6, SCR_GRAY);  // Радиационный фон

    // Вывод текста методом выравнивания координат по сетке дисплея
    gfx->setTextColor(SCR_RED);
    gfx->setTextSize(4);
    gfx->setCursor(20, 75);
    gfx->print(mainTemp);

    gfx->setTextColor(SCR_MAGENTA);
    gfx->setTextSize(2);
    gfx->setCursor(300, 40);
    gfx->print("H_ext: " + extHumi);

    gfx->setCursor(300, 135);
    gfx->print("P: " + press);

    gfx->setTextColor(SCR_YELLOW);
    gfx->setTextSize(2);
    gfx->setCursor(12, 245);
    gfx->print("T_in:" + intTemp);

    gfx->setCursor(172, 245);
    gfx->print("H_in:" + intHumi);

    gfx->setTextColor(0x07FF); // CYAN
    gfx->setCursor(332, 245);
    gfx->print("R: " + rad);

    display->flush(); // Переносим сформированный кадр на экран
}

// Метод селекции приоритетных датчиков по вашей исходной логике
void OutToScr(stSens *vSensVal) {
    ScreenOn();

    float b1 = 888.8, sr1 = 0.0, sr2 = 0.0, sd1 = 888.0, sd2 = 0.0, sd3 = 0.0;

    // Уличная температура (выбор живого датчика)
    if (vSensVal[0].actual)       b1 = vSensVal[0].value;
    else if (vSensVal[9].actual)  b1 = vSensVal[9].value;
    else if (vSensVal[7].actual)  b1 = vSensVal[7].value;
    else if (vSensVal[4].actual)  b1 = vSensVal[4].value;

    // Уличная влажность
    if (vSensVal[10].actual)      sr1 = vSensVal[10].value;
    else if (vSensVal[8].actual)  sr1 = vSensVal[8].value;
    else if (vSensVal[5].actual)  sr1 = vSensVal[5].value;

    // Атмосферное давление
    if (vSensVal[13].actual)      sr2 = vSensVal[13].value;
    else if (vSensVal[6].actual)  sr2 = vSensVal[6].value;

    // Внутренние параметры и радиация
    if (vSensVal[11].actual)      sd1 = vSensVal[11].value;
    if (vSensVal[12].actual)      sd2 = vSensVal[12].value;
    if (vSensVal[2].actual)       sd3 = vSensVal[2].value;

    OutStrToScr(TempToStr(b1), HumToStr(sr1), PressToStr(sr2), TempToStr(sd1), HumToStr(sd2), RgToStr(sd3));
}

// Экран отображения текущих часов и календаря (с оптимизированными днями недели)
void ShowTime() {
    ScreenOn();
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    auto gfx = display->getCanvas();
    if (!gfx) return;

    char ctme[16], cdte[24];
    sprintf(ctme, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    sprintf(cdte, "%02d-%02d-%4d", timeinfo.tm_mday, (timeinfo.tm_mon + 1), (timeinfo.tm_year + 1900));

    gfx->fillScreen(SCR_BLACK);
    
    // Отрисовка цифровых часов крупно по центру ландшафтного экрана
    gfx->setTextSize(6);
    gfx->setTextColor(SCR_RED);
    gfx->setCursor(110, 110);
    gfx->print(ctme);

    // Отрисовка календарной даты
    gfx->setTextSize(3);
    gfx->setTextColor(SCR_GREEN);
    gfx->setCursor(120, 200);
    gfx->print(cdte);

    // Быстрый табличный вывод дня недели без громоздкого switch-case
    int wday = timeinfo.tm_wday;
    if (wday >= 0 && wday <= 6) {
        gfx->setTextSize(3);
        gfx->setTextColor(SCR_CYAN);
        gfx->setCursor(220, 260);
        gfx->print(ruDays[wday].name);
    }

    display->flush();
}