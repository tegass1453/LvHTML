# LvHTML

[中文](#中文) | [English](#english)

---

## 中文

LvHTML 是一个基于 LVGL 的轻量级 HTML 子集渲染器。

它允许使用简单的类 HTML 语法描述 UI，并将其渲染为原生 LVGL 控件。项目目标不是实现浏览器，而是提供一个小型、可嵌入的 UI 描述层。

### 特性

* 解析基础 HTML 结构
* 将标签映射为 LVGL 控件
* 支持基础容器布局
* 输入组件：

  * input（单行输入）
  * textarea（多行输入）
  * select（下拉框）
* 基础图形：

  * rect
  * circle
* 文本渲染（标题、段落、内联文本）
* 支持部分 inline style

### 支持标签

**文本**

* h1 ~ h6
* p
* span
* label
* strong
* em
* code

**容器**

* div
* section
* article
* form

**输入**

* input
* textarea
* select / option
* button

**图形**

* rect
* circle

**其他**

* img
* a
* br
* hr

### 示例

```html
<div style="padding:10px; gap:10px;">
  <label>User Name</label>
  <input type="text" placeholder="Enter name" />

  <label>Mode</label>
  <select>
    <option selected>LIN</option>
    <option>CAN</option>
  </select>

  <rect width="120" height="60" fill="#4fc3f7" text="RECT"></rect>
</div>
```

### 使用方法

初始化 LVGL 后调用：

```c
lv_obj_clean(lv_scr_act());
lv_html_render_file(lv_scr_act(), "page.html");
```

如需键盘输入支持：

```c
lv_indev_set_group(keyboard_indev, lv_html_get_input_group());
```

### 限制

* 不是完整 HTML/CSS 引擎
* 不支持 JavaScript
* CSS 支持有限（仅 inline 且属性有限）
* 不支持复杂布局（flex/grid 不完整）
* 字体需由 LVGL 配置（建议 ASCII）

### 项目目标

LvHTML 专注于简单与可移植性，适用于：

* 嵌入式 UI 快速开发
* 配置界面
* 简单仪表盘

如需完整网页支持，建议使用浏览器内核。

---

## English

LvHTML is a lightweight HTML subset renderer built on top of LVGL.

It allows describing UI using simple HTML-like syntax and renders it as native LVGL widgets. The goal is not to build a browser, but to provide a small and embeddable UI description layer.

### Features

* Parse basic HTML structure
* Map tags to LVGL widgets
* Simple layout containers
* Input components:

  * input (single line)
  * textarea (multi-line)
  * select (dropdown)
* Basic graphics:

  * rect
  * circle
* Text rendering (labels, headings, inline text)
* Partial inline style support

### Supported Tags

**Text**

* h1 ~ h6
* p
* span
* label
* strong
* em
* code

**Containers**

* div
* section
* article
* form

**Input**

* input
* textarea
* select / option
* button

**Graphics**

* rect
* circle

**Others**

* img
* a
* br
* hr

### Example

```html
<div style="padding:10px; gap:10px;">
  <label>User Name</label>
  <input type="text" placeholder="Enter name" />

  <label>Mode</label>
  <select>
    <option selected>LIN</option>
    <option>CAN</option>
  </select>

  <rect width="120" height="60" fill="#4fc3f7" text="RECT"></rect>
</div>
```

### Usage

Initialize LVGL and call:

```c
lv_obj_clean(lv_scr_act());
lv_html_render_file(lv_scr_act(), "page.html");
```

Optional keyboard input support:

```c
lv_indev_set_group(keyboard_indev, lv_html_get_input_group());
```

### Limitations

* Not a full HTML/CSS engine
* No JavaScript support
* Limited CSS (inline only, partial properties)
* No complex layout system (flex/grid not fully supported)
* Fonts must be configured in LVGL (ASCII recommended)

### Goal

LvHTML focuses on simplicity and portability. Suitable for:

* Embedded UI prototyping
* Configuration panels
* Simple dashboards

If full web compatibility is required, use a browser engine instead.

---

## License

MIT (or your preferred license)
