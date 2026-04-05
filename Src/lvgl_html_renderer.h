#ifndef LVGL_HTML_RENDERER_H
#define LVGL_HTML_RENDERER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lightweight HTML-to-LVGL renderer.
 *
 * This is NOT a browser engine. It is a controllable HTML subset renderer that
 * maps a small set of HTML/custom tags to LVGL widgets.
 *
 * Supported container tags:
 *   html, body, div, section, article, form, header, footer, nav
 *
 * Supported text tags:
 *   h1, h2, h3, h4, h5, h6,
 *   p, span, label, strong, em, small, code,
 *   a, br, hr
 *
 * Supported interactive tags:
 *   button,
 *   input  (type=text, password, number, checkbox),
 *   textarea,
 *   select, option
 *
 * Supported media / shape tags:
 *   img,
 *   rect, circle, box
 *
 * Common supported attributes:
 *   id, class, style, width, height
 *
 * Extra supported attributes:
 *   input    : type, value, placeholder, checked, disabled, readonly
 *   textarea : value, placeholder, rows, disabled, readonly
 *   select   : value, disabled
 *   option   : value, selected
 *   img      : src
 *   rect     : fill, bg, border-color, border-width, radius, text
 *   circle   : fill, bg, border-color, border-width, text
 *
 * Basic inline style support:
 *   width, height,
 *   background / background-color,
 *   color,
 *   border-color, border-width, border-radius,
 *   padding, row-gap, column-gap, gap,
 *   margin-bottom,
 *   display:flex, flex-direction:row|column,
 *   text-align:center
 *
 * Keyboard input note:
 *   If you want SDL keyboard input to drive <input>/<textarea>/<select>, bind
 *   your keyboard indev to the group returned by lv_html_get_input_group().
 */

void lv_html_set_input_group(lv_group_t * group);
lv_group_t * lv_html_get_input_group(void);

lv_obj_t * lv_html_render_file(lv_obj_t * parent, const char * html_path);
lv_obj_t * lv_html_render_from_string(lv_obj_t * parent, const char * html);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_HTML_RENDERER_H */
