#include "lvgl_html_renderer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LV_HTML_MAX_TAG_LEN      31
#define LV_HTML_MAX_ATTR_NAME    31
#define LV_HTML_MAX_ATTR_VALUE   255
#define LV_HTML_MAX_ATTRS        24

typedef struct {
    const char * s;
    size_t pos;
    size_t len;
} html_parser_t;

typedef struct {
    char name[LV_HTML_MAX_ATTR_NAME + 1];
    char value[LV_HTML_MAX_ATTR_VALUE + 1];
} html_attr_t;

typedef struct {
    char name[LV_HTML_MAX_TAG_LEN + 1];
    int is_closing;
    int self_closing;
    html_attr_t attrs[LV_HTML_MAX_ATTRS];
    int attr_count;
} html_tag_t;

typedef struct {
    int ordered;
    int index;
} list_state_t;

typedef struct {
    int is_set;
    int is_pct;
    lv_coord_t value;
} html_dim_t;

typedef struct {
    html_dim_t width;
    html_dim_t height;
    int has_bg_color;
    lv_color_t bg_color;
    int has_text_color;
    lv_color_t text_color;
    int has_border_color;
    lv_color_t border_color;
    int has_border_width;
    lv_coord_t border_width;
    int has_radius;
    lv_coord_t radius;
    int has_pad_all;
    lv_coord_t pad_all;
    int has_pad_row;
    lv_coord_t pad_row;
    int has_pad_col;
    lv_coord_t pad_col;
    int has_margin_bottom;
    lv_coord_t margin_bottom;
    int has_text_align_center;
    int display_flex;
    int flex_row;
    int flex_column;
} html_style_t;

static lv_group_t * g_html_input_group = NULL;

static void skip_spaces(html_parser_t * p)
{
    while(p->pos < p->len && isspace((unsigned char)p->s[p->pos])) {
        p->pos++;
    }
}

static int is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':';
}

static int is_void_tag(const char * tag)
{
    return strcmp(tag, "br") == 0 ||
           strcmp(tag, "img") == 0 ||
           strcmp(tag, "hr") == 0 ||
           strcmp(tag, "input") == 0 ||
           strcmp(tag, "meta") == 0 ||
           strcmp(tag, "link") == 0;
}

static void strtolower_inplace(char * s)
{
    while(*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

static char * str_dup_range(const char * s, size_t begin, size_t end)
{
    size_t n;
    char * out;

    if(end < begin) end = begin;
    n = end - begin;
    out = (char *)malloc(n + 1);
    if(!out) return NULL;
    if(n > 0) memcpy(out, s + begin, n);
    out[n] = '\0';
    return out;
}

static char * str_dup_c(const char * s)
{
    size_t n;
    char * out;

    if(!s) return NULL;
    n = strlen(s);
    out = (char *)malloc(n + 1);
    if(!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static char * load_text_file(const char * path)
{
    FILE * fp;
    long size;
    char * buf;

    if(path == NULL) return NULL;

    fp = fopen(path, "rb");
    if(!fp) return NULL;

    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if(size < 0) {
        fclose(fp);
        return NULL;
    }

    if(fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if(!buf) {
        fclose(fp);
        return NULL;
    }

    if(size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }

    buf[size] = '\0';
    fclose(fp);
    return buf;
}

static void trim_inplace(char * s)
{
    size_t len;
    size_t start = 0;
    size_t end;

    if(!s) return;

    len = strlen(s);
    while(start < len && isspace((unsigned char)s[start])) start++;
    end = len;
    while(end > start && isspace((unsigned char)s[end - 1])) end--;

    if(start > 0 && end >= start) {
        memmove(s, s + start, end - start);
    }
    s[end - start] = '\0';
}

static void collapse_spaces_inplace(char * s)
{
    char * r = s;
    char * w = s;
    int prev_space = 1;

    if(!s) return;

    while(*r) {
        int is_space = isspace((unsigned char)*r) ? 1 : 0;
        if(is_space) {
            if(!prev_space) {
                *w++ = ' ';
                prev_space = 1;
            }
        }
        else {
            *w++ = *r;
            prev_space = 0;
        }
        r++;
    }

    if(w > s && w[-1] == ' ') w--;
    *w = '\0';
}

static void html_entity_decode_inplace(char * s)
{
    char * r = s;
    char * w = s;

    if(!s) return;

    while(*r) {
        if(strncmp(r, "&lt;", 4) == 0) {
            *w++ = '<';
            r += 4;
        }
        else if(strncmp(r, "&gt;", 4) == 0) {
            *w++ = '>';
            r += 4;
        }
        else if(strncmp(r, "&amp;", 5) == 0) {
            *w++ = '&';
            r += 5;
        }
        else if(strncmp(r, "&quot;", 6) == 0) {
            *w++ = '"';
            r += 6;
        }
        else if(strncmp(r, "&apos;", 6) == 0) {
            *w++ = '\'';
            r += 6;
        }
        else if(strncmp(r, "&nbsp;", 6) == 0) {
            *w++ = ' ';
            r += 6;
        }
        else {
            *w++ = *r++;
        }
    }

    *w = '\0';
}

static int parse_int_or_default(const char * s, int def_value)
{
    int value = 0;
    int sign = 1;
    int found = 0;

    if(!s || !*s) return def_value;

    while(*s && !isdigit((unsigned char)*s) && *s != '-') s++;
    if(*s == '-') {
        sign = -1;
        s++;
    }

    while(isdigit((unsigned char)*s)) {
        found = 1;
        value = value * 10 + (*s - '0');
        s++;
    }

    if(!found) return def_value;
    return value * sign;
}

static int streq_icase(const char * a, const char * b)
{
    if(!a || !b) return 0;
    while(*a && *b) {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int attr_truthy(const char * value)
{
    if(value == NULL || value[0] == '\0') return 1;
    if(streq_icase(value, "true")) return 1;
    if(streq_icase(value, "yes")) return 1;
    if(streq_icase(value, "on")) return 1;
    if(strcmp(value, "1") == 0) return 1;
    return 0;
}

static void tag_init(html_tag_t * tag)
{
    memset(tag, 0, sizeof(*tag));
}

static const char * tag_get_attr(const html_tag_t * tag, const char * name)
{
    int i;
    if(!tag || !name) return NULL;
    for(i = 0; i < tag->attr_count; i++) {
        if(strcmp(tag->attrs[i].name, name) == 0) return tag->attrs[i].value;
    }
    return NULL;
}

static int tag_has_attr(const html_tag_t * tag, const char * name)
{
    return tag_get_attr(tag, name) != NULL;
}

static int read_quoted_or_unquoted_value(html_parser_t * p, char * out, size_t out_sz)
{
    size_t n = 0;
    char quote = 0;

    skip_spaces(p);
    if(p->pos >= p->len) return 0;

    if(p->s[p->pos] == '"' || p->s[p->pos] == '\'') {
        quote = p->s[p->pos++];
        while(p->pos < p->len && p->s[p->pos] != quote) {
            if(n + 1 < out_sz) out[n++] = p->s[p->pos];
            p->pos++;
        }
        if(p->pos < p->len && p->s[p->pos] == quote) p->pos++;
    }
    else {
        while(p->pos < p->len) {
            char c = p->s[p->pos];
            if(isspace((unsigned char)c) || c == '>' || c == '/') break;
            if(n + 1 < out_sz) out[n++] = c;
            p->pos++;
        }
    }

    out[n] = '\0';
    return 1;
}

static int parse_tag(html_parser_t * p, html_tag_t * tag)
{
    char attr_name[LV_HTML_MAX_ATTR_NAME + 1];
    char attr_value[LV_HTML_MAX_ATTR_VALUE + 1];
    size_t n = 0;

    tag_init(tag);
    skip_spaces(p);
    if(p->pos >= p->len || p->s[p->pos] != '<') return 0;
    p->pos++;

    if(p->pos < p->len && p->s[p->pos] == '!') {
        if(p->pos + 2 < p->len && p->s[p->pos + 1] == '-' && p->s[p->pos + 2] == '-') {
            p->pos += 3;
            while(p->pos + 2 < p->len) {
                if(p->s[p->pos] == '-' && p->s[p->pos + 1] == '-' && p->s[p->pos + 2] == '>') {
                    p->pos += 3;
                    return 0;
                }
                p->pos++;
            }
            return 0;
        }
        while(p->pos < p->len && p->s[p->pos] != '>') p->pos++;
        if(p->pos < p->len) p->pos++;
        return 0;
    }

    if(p->pos < p->len && p->s[p->pos] == '/') {
        tag->is_closing = 1;
        p->pos++;
    }

    while(p->pos < p->len && is_name_char(p->s[p->pos])) {
        if(n < LV_HTML_MAX_TAG_LEN) tag->name[n++] = p->s[p->pos];
        p->pos++;
    }
    tag->name[n] = '\0';
    strtolower_inplace(tag->name);

    while(p->pos < p->len) {
        skip_spaces(p);
        if(p->pos >= p->len) break;

        if(p->s[p->pos] == '>') {
            p->pos++;
            break;
        }

        if(p->s[p->pos] == '/') {
            tag->self_closing = 1;
            p->pos++;
            skip_spaces(p);
            if(p->pos < p->len && p->s[p->pos] == '>') {
                p->pos++;
                break;
            }
            continue;
        }

        n = 0;
        while(p->pos < p->len && is_name_char(p->s[p->pos])) {
            if(n < LV_HTML_MAX_ATTR_NAME) attr_name[n++] = p->s[p->pos];
            p->pos++;
        }
        attr_name[n] = '\0';
        strtolower_inplace(attr_name);

        skip_spaces(p);
        attr_value[0] = '\0';
        if(p->pos < p->len && p->s[p->pos] == '=') {
            p->pos++;
            read_quoted_or_unquoted_value(p, attr_value, sizeof(attr_value));
        }

        if(tag->attr_count < LV_HTML_MAX_ATTRS && attr_name[0] != '\0') {
            strncpy(tag->attrs[tag->attr_count].name, attr_name, LV_HTML_MAX_ATTR_NAME);
            tag->attrs[tag->attr_count].name[LV_HTML_MAX_ATTR_NAME] = '\0';
            strncpy(tag->attrs[tag->attr_count].value, attr_value, LV_HTML_MAX_ATTR_VALUE);
            tag->attrs[tag->attr_count].value[LV_HTML_MAX_ATTR_VALUE] = '\0';
            tag->attr_count++;
        }
    }

    if(is_void_tag(tag->name)) tag->self_closing = 1;
    return tag->name[0] != '\0';
}

static void skip_closing_tag_if_present(html_parser_t * p, const char * tag_name)
{
    size_t save = p->pos;
    html_tag_t close_tag;

    skip_spaces(p);
    if(p->pos >= p->len || p->s[p->pos] != '<') return;
    if(!parse_tag(p, &close_tag)) {
        p->pos = save;
        return;
    }

    if(!(close_tag.is_closing && strcmp(close_tag.name, tag_name) == 0)) {
        p->pos = save;
    }
}

static void consume_until_matching_close(html_parser_t * p, const char * tag_name)
{
    html_tag_t tag;
    int depth = 1;

    while(p->pos < p->len && depth > 0) {
        if(p->s[p->pos] != '<') {
            p->pos++;
            continue;
        }

        if(!parse_tag(p, &tag)) continue;
        if(strcmp(tag.name, tag_name) != 0) continue;
        if(tag.is_closing) depth--;
        else if(!tag.self_closing) depth++;
    }
}

static char * read_text_until_close_tag(html_parser_t * p, const char * tag_name)
{
    size_t start = p->pos;
    size_t i = p->pos;
    size_t end = p->pos;
    int depth = 1;
    char * out;

    while(i < p->len) {
        if(p->s[i] == '<') {
            size_t j = i + 1;
            int closing = 0;
            char name[LV_HTML_MAX_TAG_LEN + 1];
            size_t n = 0;

            if(j < p->len && p->s[j] == '/') {
                closing = 1;
                j++;
            }

            while(j < p->len && is_name_char(p->s[j])) {
                if(n < LV_HTML_MAX_TAG_LEN) name[n++] = (char)tolower((unsigned char)p->s[j]);
                j++;
            }
            name[n] = '\0';

            if(name[0] != '\0' && strcmp(name, tag_name) == 0) {
                if(closing) {
                    depth--;
                    if(depth == 0) {
                        end = i;
                        break;
                    }
                }
                else if(!is_void_tag(name)) {
                    depth++;
                }
            }
        }
        i++;
    }

    out = str_dup_range(p->s, start, end);
    if(!out) return NULL;

    {
        char * src = out;
        char * dst = out;
        int inside_tag = 0;
        while(*src) {
            if(*src == '<') {
                inside_tag = 1;
            }
            else if(*src == '>') {
                inside_tag = 0;
                src++;
                if(*src && !isspace((unsigned char)*src)) {
                    *dst++ = ' ';
                }
                continue;
            }
            else if(!inside_tag) {
                *dst++ = *src;
            }
            src++;
        }
        *dst = '\0';
    }

    html_entity_decode_inplace(out);
    collapse_spaces_inplace(out);
    trim_inplace(out);

    p->pos = end;
    return out;
}

static char * read_plain_text_until_lt(html_parser_t * p)
{
    size_t start = p->pos;
    char * text;

    while(p->pos < p->len && p->s[p->pos] != '<') p->pos++;
    text = str_dup_range(p->s, start, p->pos);
    if(text) {
        html_entity_decode_inplace(text);
        collapse_spaces_inplace(text);
        trim_inplace(text);
    }
    return text;
}

static int css_named_color(const char * s, lv_color_t * out)
{
    if(streq_icase(s, "white")) { *out = lv_color_white(); return 1; }
    if(streq_icase(s, "black")) { *out = lv_color_black(); return 1; }
    if(streq_icase(s, "red"))   { *out = lv_palette_main(LV_PALETTE_RED); return 1; }
    if(streq_icase(s, "green")) { *out = lv_palette_main(LV_PALETTE_GREEN); return 1; }
    if(streq_icase(s, "blue"))  { *out = lv_palette_main(LV_PALETTE_BLUE); return 1; }
    if(streq_icase(s, "gray") || streq_icase(s, "grey")) { *out = lv_palette_main(LV_PALETTE_GREY); return 1; }
    if(streq_icase(s, "yellow")) { *out = lv_palette_main(LV_PALETTE_YELLOW); return 1; }
    if(streq_icase(s, "orange")) { *out = lv_palette_main(LV_PALETTE_ORANGE); return 1; }
    if(streq_icase(s, "purple")) { *out = lv_palette_main(LV_PALETTE_DEEP_PURPLE); return 1; }
    return 0;
}

static int parse_html_color(const char * s, lv_color_t * out)
{
    unsigned long value;
    char * end_ptr = NULL;
    if(!s || !*s || !out) return 0;

    while(*s && isspace((unsigned char)*s)) s++;
    if(*s == '#') {
        s++;
        value = strtoul(s, &end_ptr, 16);
        if(end_ptr == s) return 0;
        if(strlen(s) == 3) {
            unsigned long r = (value >> 8) & 0xF;
            unsigned long g = (value >> 4) & 0xF;
            unsigned long b = value & 0xF;
            r = (r << 4) | r;
            g = (g << 4) | g;
            b = (b << 4) | b;
            *out = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
            return 1;
        }
        if(strlen(s) >= 6) {
            unsigned long r = (value >> 16) & 0xFF;
            unsigned long g = (value >> 8) & 0xFF;
            unsigned long b = value & 0xFF;
            *out = lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b);
            return 1;
        }
    }

    return css_named_color(s, out);
}

static html_dim_t parse_dimension_value(const char * s)
{
    html_dim_t dim;
    char * end_ptr;

    dim.is_set = 0;
    dim.is_pct = 0;
    dim.value = 0;

    if(!s) return dim;
    while(*s && isspace((unsigned char)*s)) s++;
    if(*s == '\0') return dim;

    dim.is_set = 1;
    dim.value = (lv_coord_t)strtol(s, &end_ptr, 10);
    if(end_ptr == s) {
        dim.is_set = 0;
        return dim;
    }

    while(*end_ptr && isspace((unsigned char)*end_ptr)) end_ptr++;
    if(*end_ptr == '%') dim.is_pct = 1;
    return dim;
}

static void html_style_init(html_style_t * style)
{
    memset(style, 0, sizeof(*style));
}

static void parse_inline_style(const char * style_str, html_style_t * style)
{
    char * copy;
    char * item;

    if(!style_str || !*style_str || !style) return;

    copy = str_dup_c(style_str);
    if(!copy) return;

    item = strtok(copy, ";");
    while(item) {
        char * colon = strchr(item, ':');
        if(colon) {
            lv_color_t color;
            *colon = '\0';
            trim_inplace(item);
            trim_inplace(colon + 1);
            strtolower_inplace(item);

            if(strcmp(item, "width") == 0) {
                style->width = parse_dimension_value(colon + 1);
            }
            else if(strcmp(item, "height") == 0) {
                style->height = parse_dimension_value(colon + 1);
            }
            else if(strcmp(item, "background") == 0 || strcmp(item, "background-color") == 0) {
                if(parse_html_color(colon + 1, &color)) {
                    style->has_bg_color = 1;
                    style->bg_color = color;
                }
            }
            else if(strcmp(item, "color") == 0) {
                if(parse_html_color(colon + 1, &color)) {
                    style->has_text_color = 1;
                    style->text_color = color;
                }
            }
            else if(strcmp(item, "border-color") == 0) {
                if(parse_html_color(colon + 1, &color)) {
                    style->has_border_color = 1;
                    style->border_color = color;
                }
            }
            else if(strcmp(item, "border-width") == 0) {
                style->has_border_width = 1;
                style->border_width = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "border-radius") == 0 || strcmp(item, "radius") == 0) {
                style->has_radius = 1;
                style->radius = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "padding") == 0) {
                style->has_pad_all = 1;
                style->pad_all = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "row-gap") == 0) {
                style->has_pad_row = 1;
                style->pad_row = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "column-gap") == 0) {
                style->has_pad_col = 1;
                style->pad_col = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "gap") == 0) {
                style->has_pad_row = 1;
                style->has_pad_col = 1;
                style->pad_row = (lv_coord_t)parse_int_or_default(colon + 1, 0);
                style->pad_col = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "margin-bottom") == 0) {
                style->has_margin_bottom = 1;
                style->margin_bottom = (lv_coord_t)parse_int_or_default(colon + 1, 0);
            }
            else if(strcmp(item, "display") == 0) {
                if(streq_icase(colon + 1, "flex")) style->display_flex = 1;
            }
            else if(strcmp(item, "flex-direction") == 0) {
                style->display_flex = 1;
                if(streq_icase(colon + 1, "row")) style->flex_row = 1;
                else style->flex_column = 1;
            }
            else if(strcmp(item, "text-align") == 0) {
                if(streq_icase(colon + 1, "center")) style->has_text_align_center = 1;
            }
        }
        item = strtok(NULL, ";");
    }

    free(copy);
}

static void html_set_margin_bottom_compat(lv_obj_t * obj, lv_coord_t value)
{
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 9)
    lv_obj_set_style_margin_bottom(obj, value, 0);
#else
    /* LVGL 8.x builds in this project do not expose margin setters consistently.
     * Use bottom padding as a close visual fallback so the renderer still compiles
     * and keeps some vertical spacing semantics. */
    lv_obj_set_style_pad_bottom(obj, value, 0);
#endif
}

static void apply_dimension_to_obj(lv_obj_t * obj, const html_dim_t * dim, int for_width)
{
    lv_coord_t value;
    if(!obj || !dim || !dim->is_set) return;
    value = dim->is_pct ? lv_pct(dim->value) : dim->value;
    if(for_width) lv_obj_set_width(obj, value);
    else lv_obj_set_height(obj, value);
}

static void apply_common_style(lv_obj_t * obj, const html_style_t * style)
{
    if(!obj || !style) return;

    apply_dimension_to_obj(obj, &style->width, 1);
    apply_dimension_to_obj(obj, &style->height, 0);

    if(style->has_bg_color) {
        lv_obj_set_style_bg_color(obj, style->bg_color, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    if(style->has_border_color) {
        lv_obj_set_style_border_color(obj, style->border_color, 0);
    }
    if(style->has_border_width) {
        lv_obj_set_style_border_width(obj, style->border_width, 0);
    }
    if(style->has_radius) {
        lv_obj_set_style_radius(obj, style->radius, 0);
    }
    if(style->has_pad_all) {
        lv_obj_set_style_pad_all(obj, style->pad_all, 0);
    }
    if(style->has_pad_row) {
        lv_obj_set_style_pad_row(obj, style->pad_row, 0);
    }
    if(style->has_pad_col) {
        lv_obj_set_style_pad_column(obj, style->pad_col, 0);
    }
    if(style->has_margin_bottom) {
        html_set_margin_bottom_compat(obj, style->margin_bottom);
    }
    if(style->display_flex) {
        lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(obj, style->flex_row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    }
}

static void apply_label_style(lv_obj_t * label, const html_style_t * style)
{
    if(!label || !style) return;
    apply_common_style(label, style);
    if(style->has_text_color) {
        lv_obj_set_style_text_color(label, style->text_color, 0);
    }
    if(style->has_text_align_center) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void default_label_common(lv_obj_t * label)
{
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(100));
}

static lv_obj_t * create_root_container(lv_obj_t * parent)
{
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_set_layout(root, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(root, LV_DIR_VER);
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_set_style_pad_column(root, 10, 0);
    return root;
}

static lv_obj_t * create_block_container(lv_obj_t * parent, const html_tag_t * tag)
{
    lv_obj_t * cont = lv_obj_create(parent);
    html_style_t style;
    const char * width_attr = tag_get_attr(tag, "width");
    const char * height_attr = tag_get_attr(tag, "height");
    const char * style_attr = tag_get_attr(tag, "style");

    lv_obj_set_width(cont, lv_pct(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_style_pad_column(cont, 6, 0);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    html_style_init(&style);
    if(width_attr) style.width = parse_dimension_value(width_attr);
    if(height_attr) style.height = parse_dimension_value(height_attr);
    parse_inline_style(style_attr, &style);
    apply_common_style(cont, &style);

    return cont;
}

static lv_group_t * ensure_input_group(void)
{
    if(g_html_input_group == NULL) {
        g_html_input_group = lv_group_create();
    }
    return g_html_input_group;
}

void lv_html_set_input_group(lv_group_t * group)
{
    g_html_input_group = group;
}

lv_group_t * lv_html_get_input_group(void)
{
    return ensure_input_group();
}

static void focus_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    if(obj && g_html_input_group) {
        lv_group_focus_obj(obj);
    }
}

static void register_interactive_widget(lv_obj_t * obj)
{
    lv_group_t * group;
    if(!obj) return;
    group = ensure_input_group();
    lv_group_add_obj(group, obj);
    lv_obj_add_event_cb(obj, focus_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(obj, focus_event_cb, LV_EVENT_FOCUSED, NULL);
}

static void style_heading(lv_obj_t * label, int level)
{
    lv_color_t color;
    switch(level) {
        case 1: color = lv_palette_darken(LV_PALETTE_BLUE, 3); break;
        case 2: color = lv_palette_darken(LV_PALETTE_BLUE, 2); break;
        case 3: color = lv_palette_darken(LV_PALETTE_DEEP_PURPLE, 2); break;
        default: color = lv_palette_darken(LV_PALETTE_GREY, 4); break;
    }
    lv_obj_set_style_text_color(label, color, 0);
}

static void render_text_label(lv_obj_t * parent, const char * text, const html_tag_t * tag)
{
    lv_obj_t * label;
    html_style_t style;
    const char * style_attr;

    if(text == NULL || *text == '\0') return;

    label = lv_label_create(parent);
    default_label_common(label);
    lv_label_set_text(label, text);

    html_style_init(&style);
    style_attr = tag ? tag_get_attr(tag, "style") : NULL;
    parse_inline_style(style_attr, &style);
    apply_label_style(label, &style);
}

static void render_heading(lv_obj_t * parent, const char * text, int level, const html_tag_t * tag)
{
    lv_obj_t * label;
    html_style_t style;

    if(text == NULL || *text == '\0') return;

    label = lv_label_create(parent);
    default_label_common(label);
    lv_label_set_text(label, text);
    style_heading(label, level);

    html_style_init(&style);
    parse_inline_style(tag ? tag_get_attr(tag, "style") : NULL, &style);
    apply_label_style(label, &style);
}

static void render_anchor(lv_obj_t * parent, const char * text, const html_tag_t * tag)
{
    lv_obj_t * label;
    html_style_t style;
    const char * href;
    const char * shown;

    href = tag ? tag_get_attr(tag, "href") : NULL;
    shown = (text && *text) ? text : (href ? href : "");
    if(*shown == '\0') return;

    label = lv_label_create(parent);
    default_label_common(label);
    lv_label_set_text(label, shown);
    lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);

    html_style_init(&style);
    parse_inline_style(tag ? tag_get_attr(tag, "style") : NULL, &style);
    apply_label_style(label, &style);
}

static void button_event_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    const char * text = label ? lv_label_get_text(label) : "button";
    printf("[lv_html] clicked: %s\n", text ? text : "button");
}

static void render_button(lv_obj_t * parent, const char * text, const html_tag_t * tag)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_t * label = lv_label_create(btn);
    html_style_t style;

    lv_label_set_text(label, (text && *text) ? text : "Button");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);
    register_interactive_widget(btn);

    html_style_init(&style);
    parse_inline_style(tag ? tag_get_attr(tag, "style") : NULL, &style);
    if(!style.width.is_set) style.width.value = 160, style.width.is_set = 1;
    apply_common_style(btn, &style);
}

static void render_hr(lv_obj_t * parent, const html_tag_t * tag)
{
    lv_obj_t * line = lv_obj_create(parent);
    html_style_t style;

    lv_obj_set_width(line, lv_pct(100));
    lv_obj_set_height(line, 1);
    lv_obj_set_style_bg_color(line, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);

    html_style_init(&style);
    parse_inline_style(tag ? tag_get_attr(tag, "style") : NULL, &style);
    apply_common_style(line, &style);
}

static void render_img(lv_obj_t * parent, const html_tag_t * tag)
{
    lv_obj_t * img;
    html_style_t style;
    const char * src = tag_get_attr(tag, "src");
    const char * width_attr = tag_get_attr(tag, "width");
    const char * height_attr = tag_get_attr(tag, "height");

    if(!src || src[0] == '\0') return;

    img = lv_img_create(parent);
    lv_img_set_src(img, src);

    html_style_init(&style);
    if(width_attr) style.width = parse_dimension_value(width_attr);
    if(height_attr) style.height = parse_dimension_value(height_attr);
    parse_inline_style(tag_get_attr(tag, "style"), &style);
    apply_common_style(img, &style);
}

static lv_obj_t * create_list_container(lv_obj_t * parent)
{
    lv_obj_t * list = lv_obj_create(parent);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_height(list, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_left(list, 14, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    return list;
}

static void render_list_item(lv_obj_t * parent, const char * text, const list_state_t * state)
{
    lv_obj_t * row = lv_obj_create(parent);
    lv_obj_t * bullet = lv_label_create(row);
    lv_obj_t * label = lv_label_create(row);
    char prefix[32];

    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 8, 0);

    if(state && state->ordered) {
        snprintf(prefix, sizeof(prefix), "%d.", state->index);
    }
    else {
        strncpy(prefix, "-", sizeof(prefix) - 1);
        prefix[sizeof(prefix) - 1] = '\0';
    }

    lv_label_set_text(bullet, prefix);
    default_label_common(label);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_text(label, (text && *text) ? text : "");
}

static void render_input(lv_obj_t * parent, const html_tag_t * tag)
{
    const char * type = tag_get_attr(tag, "type");
    const char * value = tag_get_attr(tag, "value");
    const char * placeholder = tag_get_attr(tag, "placeholder");
    html_style_t style;

    if(type && (streq_icase(type, "checkbox") || streq_icase(type, "radio"))) {
        lv_obj_t * cb = lv_checkbox_create(parent);
        lv_checkbox_set_text(cb, value && *value ? value : "");
        if(tag_has_attr(tag, "checked") && attr_truthy(tag_get_attr(tag, "checked"))) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        if(tag_has_attr(tag, "disabled")) {
            lv_obj_add_state(cb, LV_STATE_DISABLED);
        }
        register_interactive_widget(cb);
        html_style_init(&style);
        parse_inline_style(tag_get_attr(tag, "style"), &style);
        apply_common_style(cb, &style);
        return;
    }

    {
        lv_obj_t * ta = lv_textarea_create(parent);
        lv_textarea_set_one_line(ta, 1);
        lv_obj_set_width(ta, 220);
        lv_obj_set_height(ta, 40);
        if(value) lv_textarea_set_text(ta, value);
        if(placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
        if(type && streq_icase(type, "password")) {
            lv_textarea_set_password_mode(ta, 1);
        }
        if(type && streq_icase(type, "number")) {
            lv_textarea_set_accepted_chars(ta, "0123456789.-+");
        }
        if(tag_has_attr(tag, "readonly") && attr_truthy(tag_get_attr(tag, "readonly"))) {
            lv_textarea_set_cursor_click_pos(ta, 0);
            lv_obj_add_state(ta, LV_STATE_DISABLED);
        }
        if(tag_has_attr(tag, "disabled")) {
            lv_obj_add_state(ta, LV_STATE_DISABLED);
        }
        register_interactive_widget(ta);

        html_style_init(&style);
        if(tag_get_attr(tag, "width")) style.width = parse_dimension_value(tag_get_attr(tag, "width"));
        if(tag_get_attr(tag, "height")) style.height = parse_dimension_value(tag_get_attr(tag, "height"));
        parse_inline_style(tag_get_attr(tag, "style"), &style);
        apply_common_style(ta, &style);
    }
}

static void render_textarea(lv_obj_t * parent, const html_tag_t * tag, const char * inner_text)
{
    lv_obj_t * ta = lv_textarea_create(parent);
    html_style_t style;
    const char * value = tag_get_attr(tag, "value");
    const char * placeholder = tag_get_attr(tag, "placeholder");
    int rows = parse_int_or_default(tag_get_attr(tag, "rows"), 4);

    lv_textarea_set_one_line(ta, 0);
    lv_obj_set_width(ta, 320);
    lv_obj_set_height(ta, 28 * rows + 10);

    if(value && *value) lv_textarea_set_text(ta, value);
    else if(inner_text && *inner_text) lv_textarea_set_text(ta, inner_text);
    if(placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    if(tag_has_attr(tag, "readonly") && attr_truthy(tag_get_attr(tag, "readonly"))) {
        lv_textarea_set_cursor_click_pos(ta, 0);
        lv_obj_add_state(ta, LV_STATE_DISABLED);
    }
    if(tag_has_attr(tag, "disabled")) {
        lv_obj_add_state(ta, LV_STATE_DISABLED);
    }

    register_interactive_widget(ta);

    html_style_init(&style);
    if(tag_get_attr(tag, "width")) style.width = parse_dimension_value(tag_get_attr(tag, "width"));
    if(tag_get_attr(tag, "height")) style.height = parse_dimension_value(tag_get_attr(tag, "height"));
    parse_inline_style(tag_get_attr(tag, "style"), &style);
    apply_common_style(ta, &style);
}

static void append_option_line(char ** buffer, const char * text)
{
    size_t old_len;
    size_t add_len;
    char * new_buf;

    if(!text || !*text) return;
    if(*buffer == NULL) {
        *buffer = str_dup_c(text);
        return;
    }

    old_len = strlen(*buffer);
    add_len = strlen(text);
    new_buf = (char *)realloc(*buffer, old_len + add_len + 2);
    if(!new_buf) return;
    new_buf[old_len] = '\n';
    memcpy(new_buf + old_len + 1, text, add_len + 1);
    *buffer = new_buf;
}

static char * collect_select_options(html_parser_t * p, int * selected_index)
{
    char * options = NULL;
    int index = 0;

    while(p->pos < p->len) {
        if(p->s[p->pos] == '<') {
            html_tag_t tag;
            if(!parse_tag(p, &tag)) continue;

            if(tag.is_closing && strcmp(tag.name, "select") == 0) {
                break;
            }

            if(tag.is_closing) continue;

            if(strcmp(tag.name, "option") == 0) {
                char * text = read_text_until_close_tag(p, "option");
                append_option_line(&options, text && *text ? text : "Option");
                if(tag_has_attr(&tag, "selected") && selected_index) {
                    *selected_index = index;
                }
                index++;
                free(text);
                skip_closing_tag_if_present(p, "option");
                continue;
            }

            if(!tag.self_closing) {
                consume_until_matching_close(p, tag.name);
            }
        }
        else {
            p->pos++;
        }
    }

    return options;
}
static void dropdown_open_fix_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * dd = lv_event_get_target(e);

    if(code == LV_EVENT_READY || code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t * list = lv_dropdown_get_list(dd);
        if(list) {
            lv_obj_set_style_max_height(list, 160, 0);
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
            lv_obj_set_style_anim_time(list, 0, 0);
            lv_obj_set_style_anim_time(dd, 0, 0);
#endif
        }
    }
}
static void dropdown_options_delete_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_DELETE) {
        void * ptr = lv_event_get_user_data(e);
        if(ptr) free(ptr);
    }
}

static void render_select(lv_obj_t * parent, html_parser_t * p, const html_tag_t * tag)
{
    lv_obj_t * dd = lv_dropdown_create(parent);
    html_style_t style;
    int selected_index = 0;
    char * options = NULL;
    const char * inline_options = tag_get_attr(tag, "options");
    const char * value = tag_get_attr(tag, "value");

    lv_obj_set_width(dd, 220);

    if(inline_options && *inline_options) {
        options = str_dup_c(inline_options);
    }
    else {
        options = collect_select_options(p, &selected_index);
    }

    if(!(options && *options)) {
        options = str_dup_c("Option 1\nOption 2");
    }

    if(options && *options) {
        lv_dropdown_set_options_static(dd, options);
        lv_obj_add_event_cb(dd, dropdown_options_delete_cb, LV_EVENT_DELETE, options);

        lv_obj_add_event_cb(dd, dropdown_open_fix_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(dd, dropdown_open_fix_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(dd, dropdown_open_fix_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

#if defined(LV_DIR_BOTTOM)
    lv_dropdown_set_dir(dd, LV_DIR_BOTTOM);
#elif defined(LV_DROPDOWN_DIR_BOTTOM)
    lv_dropdown_set_dir(dd, LV_DROPDOWN_DIR_BOTTOM);
#endif

#ifdef LV_SYMBOL_DOWN
    lv_dropdown_set_symbol(dd, LV_SYMBOL_DOWN);
#endif

#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
    lv_dropdown_set_selected_highlight(dd, true);
#endif

    if(value && *value && options) {
        char * copy = str_dup_c(options);
        if(copy) {
            char * line = strtok(copy, "\n");
            int idx = 0;
            while(line) {
                if(strcmp(line, value) == 0) {
                    selected_index = idx;
                    break;
                }
                idx++;
                line = strtok(NULL, "\n");
            }
            free(copy);
        }
    }

    lv_dropdown_set_selected(dd, (uint16_t)selected_index);

    if(tag_has_attr(tag, "disabled")) {
        lv_obj_add_state(dd, LV_STATE_DISABLED);
    }

    html_style_init(&style);
    if(tag_get_attr(tag, "width"))  style.width  = parse_dimension_value(tag_get_attr(tag, "width"));
    if(tag_get_attr(tag, "height")) style.height = parse_dimension_value(tag_get_attr(tag, "height"));
    parse_inline_style(tag_get_attr(tag, "style"), &style);
    apply_common_style(dd, &style);
}
static void render_shape_text(lv_obj_t * shape, const char * text, const html_style_t * style)
{
    if(text && *text) {
        lv_obj_t * label = lv_label_create(shape);
        default_label_common(label);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_center(label);
        if(style) apply_label_style(label, style);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void render_shape(lv_obj_t * parent, const html_tag_t * tag, const char * tag_name, const char * inner_text)
{
    lv_obj_t * shape = lv_obj_create(parent);
    html_style_t style;
    lv_color_t color;
    const char * fill = tag_get_attr(tag, "fill");
    const char * bg = tag_get_attr(tag, "bg");
    const char * border_color = tag_get_attr(tag, "border-color");
    const char * border_width = tag_get_attr(tag, "border-width");
    const char * radius = tag_get_attr(tag, "radius");
    const char * width_attr = tag_get_attr(tag, "width");
    const char * height_attr = tag_get_attr(tag, "height");
    const char * text_attr = tag_get_attr(tag, "text");
    const char * text = text_attr && *text_attr ? text_attr : inner_text;

    lv_obj_set_style_pad_all(shape, 0, 0);
    lv_obj_set_style_border_width(shape, 1, 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(shape, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);

    html_style_init(&style);
    if(width_attr) style.width = parse_dimension_value(width_attr);
    if(height_attr) style.height = parse_dimension_value(height_attr);
    parse_inline_style(tag_get_attr(tag, "style"), &style);

    if(!style.width.is_set) {
        style.width.is_set = 1;
        style.width.value = strcmp(tag_name, "circle") == 0 ? 80 : 120;
    }
    if(!style.height.is_set) {
        style.height.is_set = 1;
        style.height.value = strcmp(tag_name, "circle") == 0 ? 80 : 60;
    }

    if(fill && parse_html_color(fill, &color)) {
        style.has_bg_color = 1;
        style.bg_color = color;
    }
    else if(bg && parse_html_color(bg, &color)) {
        style.has_bg_color = 1;
        style.bg_color = color;
    }

    if(border_color && parse_html_color(border_color, &color)) {
        style.has_border_color = 1;
        style.border_color = color;
    }
    if(border_width) {
        style.has_border_width = 1;
        style.border_width = (lv_coord_t)parse_int_or_default(border_width, 1);
    }
    if(radius) {
        style.has_radius = 1;
        style.radius = (lv_coord_t)parse_int_or_default(radius, 0);
    }
    if(strcmp(tag_name, "circle") == 0) {
        style.has_radius = 1;
        style.radius = LV_RADIUS_CIRCLE;
    }

    apply_common_style(shape, &style);
    render_shape_text(shape, text, &style);
}

static void render_nodes(html_parser_t * p, lv_obj_t * parent, const char * stop_tag,
                         list_state_t * current_list, int depth);

static void render_nodes(html_parser_t * p, lv_obj_t * parent, const char * stop_tag,
                         list_state_t * current_list, int depth)
{
    while(p->pos < p->len) {
        if(p->s[p->pos] == '<') {
            html_tag_t tag;
            char * text;

            if(!parse_tag(p, &tag)) continue;

            if(stop_tag && tag.is_closing && strcmp(tag.name, stop_tag) == 0) {
                return;
            }

            if(tag.is_closing) continue;

            if(strcmp(tag.name, "head") == 0 || strcmp(tag.name, "style") == 0 ||
               strcmp(tag.name, "script") == 0 || strcmp(tag.name, "title") == 0) {
                consume_until_matching_close(p, tag.name);
                continue;
            }

            if(strcmp(tag.name, "html") == 0 || strcmp(tag.name, "body") == 0 ||
               strcmp(tag.name, "div") == 0 || strcmp(tag.name, "section") == 0 ||
               strcmp(tag.name, "article") == 0 || strcmp(tag.name, "form") == 0 ||
               strcmp(tag.name, "header") == 0 || strcmp(tag.name, "footer") == 0 ||
               strcmp(tag.name, "nav") == 0) {
                lv_obj_t * cont = parent;
                if(strcmp(tag.name, "html") != 0 && strcmp(tag.name, "body") != 0) {
                    cont = create_block_container(parent, &tag);
                }
                render_nodes(p, cont, tag.name, current_list, depth + 1);
                continue;
            }

            if(strcmp(tag.name, "ul") == 0 || strcmp(tag.name, "ol") == 0) {
                list_state_t list_state;
                lv_obj_t * list_parent = create_list_container(parent);
                list_state.ordered = (strcmp(tag.name, "ol") == 0);
                list_state.index = 1;
                render_nodes(p, list_parent, tag.name, &list_state, depth + 1);
                continue;
            }

            if(strcmp(tag.name, "li") == 0) {
                text = read_text_until_close_tag(p, "li");
                render_list_item(parent, text ? text : "", current_list);
                if(current_list && current_list->ordered) current_list->index++;
                free(text);
                skip_closing_tag_if_present(p, "li");
                continue;
            }

            if(strcmp(tag.name, "img") == 0) {
                render_img(parent, &tag);
                continue;
            }

            if(strcmp(tag.name, "rect") == 0 || strcmp(tag.name, "box") == 0 || strcmp(tag.name, "circle") == 0) {
                text = NULL;
                if(!tag.self_closing) {
                    text = read_text_until_close_tag(p, tag.name);
                    skip_closing_tag_if_present(p, tag.name);
                }
                render_shape(parent, &tag, tag.name, text);
                free(text);
                continue;
            }

            if(strcmp(tag.name, "br") == 0) {
                render_text_label(parent, " ", &tag);
                continue;
            }

            if(strcmp(tag.name, "hr") == 0) {
                render_hr(parent, &tag);
                continue;
            }

            if(strcmp(tag.name, "p") == 0 || strcmp(tag.name, "span") == 0 ||
               strcmp(tag.name, "label") == 0 || strcmp(tag.name, "strong") == 0 ||
               strcmp(tag.name, "em") == 0 || strcmp(tag.name, "small") == 0 ||
               strcmp(tag.name, "code") == 0) {
                text = read_text_until_close_tag(p, tag.name);
                render_text_label(parent, text ? text : "", &tag);
                free(text);
                skip_closing_tag_if_present(p, tag.name);
                continue;
            }

            if(strcmp(tag.name, "a") == 0) {
                text = read_text_until_close_tag(p, "a");
                render_anchor(parent, text ? text : "", &tag);
                free(text);
                skip_closing_tag_if_present(p, "a");
                continue;
            }

            if(strcmp(tag.name, "button") == 0) {
                text = read_text_until_close_tag(p, "button");
                render_button(parent, text ? text : "Button", &tag);
                free(text);
                skip_closing_tag_if_present(p, "button");
                continue;
            }

            if(strcmp(tag.name, "input") == 0) {
                render_input(parent, &tag);
                continue;
            }

            if(strcmp(tag.name, "textarea") == 0) {
                text = read_text_until_close_tag(p, "textarea");
                render_textarea(parent, &tag, text);
                free(text);
                skip_closing_tag_if_present(p, "textarea");
                continue;
            }

            if(strcmp(tag.name, "select") == 0) {
                render_select(parent, p, &tag);
                continue;
            }

            if(strcmp(tag.name, "option") == 0) {
                text = read_text_until_close_tag(p, "option");
                render_text_label(parent, text ? text : "", &tag);
                free(text);
                skip_closing_tag_if_present(p, "option");
                continue;
            }

            if(strcmp(tag.name, "h1") == 0 || strcmp(tag.name, "h2") == 0 ||
               strcmp(tag.name, "h3") == 0 || strcmp(tag.name, "h4") == 0 ||
               strcmp(tag.name, "h5") == 0 || strcmp(tag.name, "h6") == 0) {
                int level = tag.name[1] - '0';
                text = read_text_until_close_tag(p, tag.name);
                render_heading(parent, text ? text : "", level, &tag);
                free(text);
                skip_closing_tag_if_present(p, tag.name);
                continue;
            }

            if(!tag.self_closing) {
                text = read_text_until_close_tag(p, tag.name);
                if(text && *text) render_text_label(parent, text, &tag);
                free(text);
                skip_closing_tag_if_present(p, tag.name);
                continue;
            }
        }
        else {
            char * text = read_plain_text_until_lt(p);
            if(text && *text) render_text_label(parent, text, NULL);
            free(text);
        }
    }

    (void)depth;
}

lv_obj_t * lv_html_render_from_string(lv_obj_t * parent, const char * html)
{
    html_parser_t parser;
    lv_obj_t * root;

    if(parent == NULL || html == NULL) return NULL;

    parser.s = html;
    parser.pos = 0;
    parser.len = strlen(html);

    root = create_root_container(parent);
    render_nodes(&parser, root, NULL, NULL, 0);
    return root;
}

lv_obj_t * lv_html_render_file(lv_obj_t * parent, const char * html_path)
{
    char * html;
    lv_obj_t * root;

    html = load_text_file(html_path);
    if(html == NULL) {
        lv_obj_t * label = lv_label_create(parent);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, lv_pct(100));
        lv_label_set_text_fmt(label, "Failed to open HTML file:\n%s", html_path ? html_path : "(null)");
        lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_RED), 0);
        return label;
    }

    root = lv_html_render_from_string(parent, html);
    free(html);
    return root;
}
