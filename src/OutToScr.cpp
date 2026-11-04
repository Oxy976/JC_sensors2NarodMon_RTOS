#include "OutToScr.h"
#include <lvgl.h>

static lv_obj_t *lbl_ext_temp = nullptr;
static lv_obj_t *lbl_ext_humi = nullptr;
static lv_obj_t *lbl_press    = nullptr;
static lv_obj_t *lbl_int_temp = nullptr;
static lv_obj_t *lbl_int_humi = nullptr;
static lv_obj_t *lbl_rad      = nullptr;

void ScreenOn() {}
void ScreenOff() {}
void processLcdAnimate() {}
bool isScreenFullyActive() { return true; }

void init_lvgl_ui() {
    static lv_style_t style_box;
    lv_style_init(&style_box);
    lv_style_set_bg_color(&style_box, lv_color_make(20, 20, 20)); 
    lv_style_set_border_color(&style_box, lv_color_make(100, 100, 100)); 
    lv_style_set_border_width(&style_box, 2);
    lv_style_set_radius(&style_box, 8);

    // 1. Уличная температура
    lv_obj_t *box_ext_temp = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_ext_temp, 275, 185);
    lv_obj_align(box_ext_temp, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_add_style(box_ext_temp, &style_box, 0);

    lbl_ext_temp = lv_label_create(box_ext_temp);
    lv_obj_align(lbl_ext_temp, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_ext_temp, lv_color_make(255, 0, 0), 0); 
    lv_obj_set_style_text_font(lbl_ext_temp, &lv_font_montserrat_24, 0); 
    lv_label_set_text(lbl_ext_temp, "--.- C");

    // 2. Уличная влажность
    lv_obj_t *box_ext_humi = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_ext_humi, 190, 90);
    lv_obj_align(box_ext_humi, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_style(box_ext_humi, &style_box, 0);

    lbl_ext_humi = lv_label_create(box_ext_humi);
    lv_obj_align(lbl_ext_humi, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_ext_humi, lv_color_make(255, 0, 255), 0); 
    lv_label_set_text(lbl_ext_humi, "H_ext: -- %");

    // 3. Атмосферное давление
    lv_obj_t *box_press = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_press, 190, 90);
    lv_obj_align(box_press, LV_ALIGN_TOP_RIGHT, -5, 100);
    lv_obj_add_style(box_press, &style_box, 0);

    lbl_press = lv_label_create(box_press);
    lv_obj_align(lbl_press, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_press, lv_color_make(255, 0, 255), 0); 
    lv_label_set_text(lbl_press, "Bar: --- mmHg");

    // Нижний ряд блоков
    int box_w = 153, box_h = 115;

    lv_obj_t *box_int_temp = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_int_temp, box_w, box_h);
    lv_obj_align(box_int_temp, LV_ALIGN_BOTTOM_LEFT, 5, -5);
    lv_obj_add_style(box_int_temp, &style_box, 0);

    lbl_int_temp = lv_label_create(box_int_temp);
    lv_obj_align(lbl_int_temp, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_int_temp, lv_color_make(255, 255, 0), 0); 
    lv_label_set_text(lbl_int_temp, "T_int: --.- C");

    lv_obj_t *box_int_humi = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_int_humi, box_w, box_h);
    lv_obj_align(box_int_humi, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_style(box_int_humi, &style_box, 0);

    lbl_int_humi = lv_label_create(box_int_humi);
    lv_obj_align(lbl_int_humi, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_int_humi, lv_color_make(255, 255, 0), 0); 
    lv_label_set_text(lbl_int_humi, "H_int: -- %");

    lv_obj_t *box_rad = lv_obj_create(lv_scr_act());
    lv_obj_set_size(box_rad, box_w, box_h);
    lv_obj_align(box_rad, LV_ALIGN_BOTTOM_RIGHT, -5, -5);
    lv_obj_add_style(box_rad, &style_box, 0);

    lbl_rad = lv_label_create(box_rad);
    lv_obj_align(lbl_rad, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_rad, lv_color_make(0, 255, 255), 0); 
    lv_label_set_text(lbl_rad, "Rad: -.-- uS");
}

void OutToScr(stSens *vSensVal) {
    if (lbl_ext_temp == nullptr) {
        init_lvgl_ui(); 
    }

    // ВЫВОДИМ ДАННЫЕ НАПРЯМУЮ, ЕСЛИ ЗНАЧЕНИЕ НЕ NULL И НЕ НАУШНИКИ/ЗАГЛУШКИ
    // 1. Уличная температура (Индекс 4)
    if (!isnan(vSensVal[4].value) && vSensVal[4].value != 0.0) {
        lv_label_set_text_fmt(lbl_ext_temp, "%.1f C", vSensVal[4].value);
    } else {
        lv_label_set_text(lbl_ext_temp, "--.- C");
    }

    // 2. Уличная влажность (Индекс 5)
    if (!isnan(vSensVal[5].value) && vSensVal[5].value != 0.0) {
        lv_label_set_text_fmt(lbl_ext_humi, "H_ext: %.1f %%", vSensVal[5].value);
    } else {
        lv_label_set_text(lbl_ext_humi, "H_ext: -- %%");
    }

    // 3. Атмосферное давление (Индекс 6)
    if (!isnan(vSensVal[6].value) && vSensVal[6].value != 0.0) {
        lv_label_set_text_fmt(lbl_press, "Bar: %d mmHg", (int)vSensVal[6].value);
    } else {
        lv_label_set_text(lbl_press, "Bar: --- mmHg");
    }

    // 4. Комнатная температура (Индекс 11)
    if (!isnan(vSensVal[11].value) && vSensVal[11].value != 0.0) {
        lv_label_set_text_fmt(lbl_int_temp, "T_int: %.1f C", vSensVal[11].value);
    } else {
        lv_label_set_text(lbl_int_temp, "T_int: --.- C");
    }

    // 5. Комнатная влажность (Индекс 12)
    if (!isnan(vSensVal[12].value) && vSensVal[12].value != 0.0) {
        lv_label_set_text_fmt(lbl_int_humi, "H_int: %.1f %%", vSensVal[12].value);
    } else {
        lv_label_set_text(lbl_int_humi, "H_int: -- %%");
    }

    // 6. Радиация (Индекс 2)
    if (vSensVal[2].actual) {
        lv_label_set_text_fmt(lbl_rad, "Rad: %.2f uS", vSensVal[2].value);
    } else {
        lv_label_set_text(lbl_rad, "Rad: -.-- uS");
    }
}

void ShowTime() {}
