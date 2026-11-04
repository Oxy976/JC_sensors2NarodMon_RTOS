#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0  /* Фикс: 0 для корректного отображения цветов на JC3248 */

#define LV_MEM_CUSTOM      1 
#define LV_TICK_CUSTOM     1
#define LV_TICK_CUSTOM_SYS_TIME_EXPR  millis()

/* Шрифты и виджеты, необходимые для метеостанции */
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_DEFAULT          &lv_font_montserrat_14

#define LV_USE_BTN         1
#define LV_USE_LABEL       1
#define LV_USE_CANVAS      1

#endif
