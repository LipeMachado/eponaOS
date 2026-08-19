#include "gui.h"
#include "gpu.h"
#include "term.h"
#include "shell.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "vfs.h"
#include "heap.h"
#include "pit.h"
#include "string.h"
#include "acpi.h"
#include <stdint.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define FRAME_TICKS   1
#define CLOCK_TICKS   100

#define MAX_WINDOWS  8
#define TITLEBAR_H   22
#define BORDER_W     1
#define CLOSE_BTN_W  14
#define TASKBAR_H    26
#define TASKBAR_BTN_W 110

/* ==================== KolibriOS Color Palette ==================== */
#define CLR_DESKTOP       0x0000AA  /* solid blue desktop */
#define CLR_BORDER        0xAAAAAA  /* window border: light gray */
#define CLR_BORDER_FOCUS  0x0000AA  /* focused window border: blue */
#define CLR_TITLE_ACTIVE  0x0000AA  /* active title: blue */
#define CLR_TITLE_INACT   0xAAAAAA  /* inactive title: gray */
#define CLR_TITLE_TEXT    0xFFFFFF  /* title text: white */
#define CLR_TITLE_TEXT_I  0x444444  /* inactive title text: dark gray */
#define CLR_WINDOWBG      0xBBBBBB  /* window client: light gray */
#define CLR_MENU_BG       0xBBBBBB  /* menu/panel background */
#define CLR_TASKBAR       0xBBBBBB  /* taskbar bg */
#define CLR_TASKBAR_TOP   0x888888  /* taskbar top line */
#define CLR_TASKBTN       0xAAAAAA  /* taskbar button */
#define CLR_TASKBTN_ACT   0x0000AA  /* active taskbar button */
#define CLR_MENUBTN       0x0000AA  /* menu button */
#define CLR_ICON_BG       0xCCCCCC  /* icon background */
#define CLR_ICON_BORDER   0x888888  /* icon border */

/* ==================== Window types ==================== */

typedef enum { WIN_TERMINAL, WIN_ABOUT, WIN_FILEMAN } win_kind_t;

typedef struct {
    int used;
    int x, y;
    int cw, ch;
    char title[32];
    win_kind_t kind;
    uint32_t *buf;
    term_ctx_t term;
    uint16_t cur_row, cur_col;
    char line_buf[SHELL_LINE_MAX];
    int line_pos;
    int line_cpos;
} window_t;

static window_t g_win[MAX_WINDOWS];
static int g_zorder[MAX_WINDOWS];
static int g_win_count;
static int g_drag_zpos = -1;
static int g_drag_dx, g_drag_dy;
static int g_focus_zpos = -1;
static int g_menu_open = 0;

static int win_total_w(const window_t *w) { return w->cw + BORDER_W * 2; }
static int win_total_h(const window_t *w) { return w->ch + TITLEBAR_H + BORDER_W; }
static int win_client_x(const window_t *w) { return w->x + BORDER_W; }
static int win_client_y(const window_t *w) { return w->y + TITLEBAR_H; }

static int point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

/* ==================== Flat Drawing ==================== */

/* Draw flat window border (1px outline) */
static void draw_flat_border(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    gpu_fill_rect(x, y, w, 1, color);
    gpu_fill_rect(x, (uint16_t)(y + h - 1), w, 1, color);
    gpu_fill_rect(x, y, 1, h, color);
    gpu_fill_rect((uint16_t)(x + w - 1), y, 1, h, color);
}

/* Draw close button (X) - flat style */
static void draw_close_button(uint16_t x, uint16_t y) {
    uint16_t sz = 14;
    gpu_fill_rect(x, y, sz, sz, 0xAA0000);
    draw_flat_border(x, y, sz, sz, 0x880000);
    /* X */
    uint32_t c = 0xFFFFFF;
    for (int i = 2; i <= 8; i++) {
        gpu_put_pixel((uint16_t)(x + i), (uint16_t)(y + i), c);
        gpu_put_pixel((uint16_t)(x + 8 - i + 2), (uint16_t)(y + i), c);
    }
    for (int i = 3; i <= 7; i++) {
        gpu_put_pixel((uint16_t)(x + i + 1), (uint16_t)(y + i), c);
        gpu_put_pixel((uint16_t)(x + 7 - i + 3), (uint16_t)(y + i), c);
    }
}

/* Draw minimize button (_) - flat style */
static void draw_min_button(uint16_t x, uint16_t y) {
    uint16_t sz = 14;
    gpu_fill_rect(x, y, sz, sz, CLR_TASKBTN);
    draw_flat_border(x, y, sz, sz, 0x888888);
    for (int i = 3; i <= 10; i++)
        gpu_put_pixel((uint16_t)(x + i), (uint16_t)(y + 10), 0x444444);
}

/* ==================== Window Management ==================== */

static int win_create(win_kind_t kind, const char *title, int x, int y, int cw, int ch) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_win[i].used) continue;
        window_t *w = &g_win[i];
        w->used = 1;
        w->x = x; w->y = y; w->cw = cw; w->ch = ch;
        int n = 0;
        while (title[n] && n < 31) { w->title[n] = title[n]; n++; }
        w->title[n] = 0;
        w->kind = kind;
        w->buf = (uint32_t *)kmalloc((uint32_t)(cw * ch * 4));
        w->line_pos = 0; w->line_cpos = 0; w->line_buf[0] = 0;
        g_zorder[g_win_count] = i;
        g_win_count++;
        return i;
    }
    return -1;
}

static void win_raise(int zpos) {
    if (zpos == g_win_count - 1) return;
    int idx = g_zorder[zpos];
    for (int i = zpos; i < g_win_count - 1; i++)
        g_zorder[i] = g_zorder[i + 1];
    g_zorder[g_win_count - 1] = idx;
}

static void win_close(int zpos) {
    int idx = g_zorder[zpos];
    if (g_win[idx].buf) kfree(g_win[idx].buf);
    g_win[idx].used = 0;
    for (int i = zpos; i < g_win_count - 1; i++)
        g_zorder[i] = g_zorder[i + 1];
    g_win_count--;
}

static int win_hit_test(int px, int py) {
    for (int z = g_win_count - 1; z >= 0; z--) {
        window_t *w = &g_win[g_zorder[z]];
        if (!w->used) continue;
        if (point_in(px, py, w->x, w->y, win_total_w(w), win_total_h(w)))
            return z;
    }
    return -1;
}

/* ==================== Window Content Renderers ==================== */

static void win_render_terminal_init(window_t *w) {
    for (int i = 0; i < w->cw * w->ch; i++)
        w->buf[i] = 0x000000;

    term_ctx_init(&w->term);
    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(0x0F, 0x00);
    gpu_set_cursor(0, 0);

    term_ctx_print(&w->term, "EponaOS Terminal\r\n");
    term_ctx_print(&w->term, "Type 'help' for commands.\r\n\r\n");

    gpu_get_cursor(&w->cur_row, &w->cur_col);
    shell_print_prompt();
    gpu_get_cursor(&w->cur_row, &w->cur_col);
    gpu_end_target();
    gpu_reset_console_viewport();
}

static void win_render_about(window_t *w) {
    for (int i = 0; i < w->cw * w->ch; i++)
        w->buf[i] = CLR_WINDOWBG;

    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(0x01, 0x07);
    gpu_set_cursor(0, 0);

    gpu_print("         EponaOS\n\n");
    gpu_set_color(0x00, 0x07);
    gpu_print("  A hobby operating system written\n");
    gpu_print("  from scratch in C and x86 ASM.\n\n");
    gpu_print("  Kernel: x86_64 long mode\n");
    gpu_print("  Memory: PMM + paging 4-level\n");
    gpu_print("  Filesystem: FAT32\n");
    gpu_print("  Network: ARP, TCP, HTTP\n");
    gpu_print("  GUI: KolibriOS-style\n\n");
    gpu_print("  (c) 2025 EponaOS Project\n");

    gpu_end_target();
    gpu_reset_console_viewport();
}

static int fm_readdir_cb(const char *name, uint32_t size, uint8_t flags, void *arg) {
    (void)arg;
    gpu_print("  ");
    if (flags & VFS_DIR) gpu_set_color(0x01, 0x07);
    gpu_print(name);
    if (flags & VFS_DIR) gpu_print("/");
    gpu_set_color(0x00, 0x07);
    if (!(flags & VFS_DIR)) {
        gpu_print("  (");
        char num[12]; int ni = 11; num[ni] = 0;
        if (size == 0) num[--ni] = '0';
        while (size) { num[--ni] = (char)('0' + size % 10); size /= 10; }
        gpu_print(&num[ni]);
        gpu_print(" bytes)");
    }
    gpu_print("\n");
    return 0;
}

static void win_render_fileman(window_t *w) {
    for (int i = 0; i < w->cw * w->ch; i++)
        w->buf[i] = CLR_WINDOWBG;

    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(0x00, 0x07);
    gpu_set_cursor(0, 0);

    gpu_print("  EponaOS File Manager\n");
    gpu_print("  --------------------\n\n");
    gpu_print("  /\n");
    vfs_readdir("/", fm_readdir_cb, NULL);

    gpu_end_target();
    gpu_reset_console_viewport();
}

/* ==================== Terminal Input ==================== */

static void gui_terminal_redraw_line(window_t *w) {
    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(w->term.fg, w->term.bg);
    gpu_set_cursor(w->cur_row, w->cur_col);
    shell_redraw_line(w->line_buf, w->line_pos, w->line_cpos);
    gpu_get_cursor(&w->cur_row, &w->cur_col);
    gpu_end_target();
    gpu_reset_console_viewport();
}

static void gui_dispatch_command(window_t *w) {
    gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
    gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
    gpu_set_color(w->term.fg, w->term.bg);
    gpu_set_cursor(w->cur_row, w->cur_col);
    shell_set_gui_context(1);
    shell_dispatch_line(w->line_buf);
    shell_set_gui_context(0);
    shell_print_prompt();
    gpu_get_cursor(&w->cur_row, &w->cur_col);
    gpu_end_target();
    gpu_reset_console_viewport();
    w->line_pos = 0; w->line_cpos = 0; w->line_buf[0] = 0;
}

static void win_terminal_feed(window_t *w, int c) {
    if (c == '\n') {
        gpu_begin_target(w->buf, (uint16_t)w->cw, (uint16_t)w->ch);
        gpu_set_console_viewport(0, 0, (uint16_t)(w->cw / 8), (uint16_t)(w->ch / 16));
        gpu_set_color(w->term.fg, w->term.bg);
        gpu_set_cursor(w->cur_row, w->cur_col);
        term_ctx_putc(&w->term, '\r');
        term_ctx_putc(&w->term, '\n');
        gpu_get_cursor(&w->cur_row, &w->cur_col);
        gpu_end_target();
        gpu_reset_console_viewport();
        gui_dispatch_command(w);
        return;
    }
    if (c == '\b') {
        if (w->line_cpos > 0) {
            shell_backspace_char(w->line_buf, &w->line_pos, &w->line_cpos);
            gui_terminal_redraw_line(w);
        }
        return;
    }
    if (c == KEY_DEL) { shell_delete_char(w->line_buf, &w->line_pos, &w->line_cpos); gui_terminal_redraw_line(w); return; }
    if (c == '\t') return;
    if (c == KEY_LEFT) { if (w->line_cpos > 0) { w->line_cpos--; gui_terminal_redraw_line(w); } return; }
    if (c == KEY_RIGHT) { if (w->line_cpos < w->line_pos) { w->line_cpos++; gui_terminal_redraw_line(w); } return; }
    if (c == KEY_HOME) { w->line_cpos = 0; gui_terminal_redraw_line(w); return; }
    if (c == KEY_END) { w->line_cpos = w->line_pos; gui_terminal_redraw_line(w); return; }
    if (c >= 32 && c < 127 && w->line_pos < SHELL_LINE_MAX - 1) {
        shell_insert_char(w->line_buf, &w->line_pos, &w->line_cpos, c, SHELL_LINE_MAX);
        gui_terminal_redraw_line(w);
    }
}

/* ==================== Window Chrome (KolibriOS flat style) ==================== */

static void draw_window_chrome(const window_t *w, int focused) {
    int ow = win_total_w(w);
    int oh = win_total_h(w);

    /* Flat border around entire window */
    uint32_t border_color = focused ? CLR_BORDER_FOCUS : CLR_BORDER;
    draw_flat_border((uint16_t)w->x, (uint16_t)w->y, (uint16_t)ow, (uint16_t)oh, border_color);

    /* Title bar */
    uint32_t title_bg = focused ? CLR_TITLE_ACTIVE : CLR_TITLE_INACT;
    uint32_t title_fg = focused ? CLR_TITLE_TEXT : CLR_TITLE_TEXT_I;
    gpu_fill_rect((uint16_t)(w->x + 1), (uint16_t)(w->y + 1),
                  (uint16_t)(ow - 2), TITLEBAR_H - 1, title_bg);

    /* Title text */
    gpu_draw_text((uint16_t)(w->x + 6), (uint16_t)(w->y + 5), w->title, title_fg, title_bg);

    /* Close button */
    int cbx = w->x + ow - 18;
    int cby = w->y + 4;
    draw_close_button((uint16_t)cbx, (uint16_t)cby);

    /* Minimize button */
    int mbx = w->x + ow - 34;
    int mby = w->y + 4;
    draw_min_button((uint16_t)mbx, (uint16_t)mby);

    /* Client area (filled with window bg) */
    gpu_fill_rect((uint16_t)(w->x + 1), (uint16_t)(w->y + TITLEBAR_H),
                  (uint16_t)(ow - 2), (uint16_t)(w->ch), CLR_WINDOWBG);
}

static void compose_windows(void) {
    for (int z = 0; z < g_win_count; z++) {
        window_t *w = &g_win[g_zorder[z]];
        if (!w->used) continue;
        draw_window_chrome(w, z == g_win_count - 1);
        gpu_blit_buffer(w->buf, (uint16_t)w->cw, (uint16_t)w->ch,
                        (uint16_t)win_client_x(w), (uint16_t)win_client_y(w));
    }
}

/* ==================== Desktop Icons (KolibriOS grid) ==================== */

static void draw_desktop_icon(uint16_t x, uint16_t y, const char *label, uint32_t color) {
    gpu_fill_rect(x, y, 40, 40, CLR_ICON_BG);
    draw_flat_border(x, y, 40, 40, CLR_ICON_BORDER);
    gpu_fill_rect((uint16_t)(x + 6), (uint16_t)(y + 4), 28, 22, color);
    int lbl_len = str_len(label);
    int lbl_x = x + 20 - (lbl_len * 4);
    if (lbl_x < x) lbl_x = x;
    gpu_draw_text((uint16_t)lbl_x, (uint16_t)(y + 44), label, 0xFFFFFF, CLR_DESKTOP);
}

static void draw_desktop_icons(uint16_t screen_w) {
    (void)screen_w;
    draw_desktop_icon(20, 20,  "Terminal",  0x000000);
    draw_desktop_icon(90, 20,  "File Manager", 0x808000);
    draw_desktop_icon(160, 20, "About",     0x008000);
    draw_desktop_icon(230, 20, "Network",   0x000080);
    draw_desktop_icon(300, 20, "System",    0x800080);

    draw_desktop_icon(20, 90,  "Run...",    0x808080);
    draw_desktop_icon(90, 90,  "Help",      0x008080);
    draw_desktop_icon(160, 90, "Console",   0x444444);
    draw_desktop_icon(230, 90, "Settings",  0x804000);
    draw_desktop_icon(300, 90, "Shutdown",  0xAA0000);
}

/* ==================== Taskbar (KolibriOS style) ==================== */

static uint32_t *g_cursor_bg;
static int g_cursor_saved;
static int g_cursor_x;
static int g_cursor_y;

typedef struct { int x, y; uint8_t buttons; } gui_pointer_t;

static uint8_t cmos_read(uint8_t reg) { outb(CMOS_ADDR, reg); return inb(CMOS_DATA); }
static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v & 0x0F) + ((v >> 4) * 10)); }

static void read_time(char out[6]) {
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) { minute = bcd_to_bin(minute); hour = bcd_to_bin(hour & 0x7F); }
    out[0] = (char)('0' + (hour / 10) % 10);
    out[1] = (char)('0' + hour % 10);
    out[2] = ':';
    out[3] = (char)('0' + (minute / 10) % 10);
    out[4] = (char)('0' + minute % 10);
    out[5] = 0;
}

static void draw_taskbar(uint16_t w, uint16_t h) {
    int tb_y = h - TASKBAR_H;

    /* Taskbar background */
    gpu_fill_rect(0, (uint16_t)tb_y, w, TASKBAR_H, CLR_TASKBAR);
    gpu_fill_rect(0, (uint16_t)tb_y, w, 1, CLR_TASKBAR_TOP);

    /* Menu button (left side) */
    int mbx = 2, mby = tb_y + 3, mbw = 50, mbh = 20;
    gpu_fill_rect((uint16_t)mbx, (uint16_t)mby, (uint16_t)mbw, (uint16_t)mbh, CLR_MENUBTN);
    draw_flat_border((uint16_t)mbx, (uint16_t)mby, (uint16_t)mbw, (uint16_t)mbh, 0x000066);
    gpu_draw_text((uint16_t)(mbx + 8), (uint16_t)(mby + 5), "Menu", 0xFFFFFF, CLR_MENUBTN);

    /* Separator */
    gpu_fill_rect(54, (uint16_t)(tb_y + 3), 1, 20, CLR_TASKBAR_TOP);

    /* Task buttons */
    int bx = 58;
    for (int z = 0; z < g_win_count && bx < (int)w - 80; z++) {
        window_t *win = &g_win[g_zorder[z]];
        if (!win->used) continue;
        int bw = TASKBAR_BTN_W;
        int is_focused = (z == g_win_count - 1);
        uint32_t btn_bg = is_focused ? CLR_TASKBTN_ACT : CLR_TASKBTN;
        uint32_t btn_fg = is_focused ? 0xFFFFFF : 0x444444;
        gpu_fill_rect((uint16_t)bx, (uint16_t)(tb_y + 3), (uint16_t)bw, 20, btn_bg);
        draw_flat_border((uint16_t)bx, (uint16_t)(tb_y + 3), (uint16_t)bw, 20,
                         is_focused ? 0x000066 : 0x888888);
        /* Title */
        int max_chars = (bw - 10) / 8;
        char truncated[16];
        int tl = str_len(win->title);
        if (tl > max_chars) tl = max_chars;
        for (int i = 0; i < tl; i++) truncated[i] = win->title[i];
        truncated[tl] = 0;
        gpu_draw_text((uint16_t)(bx + 5), (uint16_t)(tb_y + 7), truncated, btn_fg, btn_bg);
        bx += bw + 2;
    }

    /* Clock (right side) */
    int clock_w = 60;
    int clock_x = w - clock_w - 4;
    int clock_y = tb_y + 3;
    gpu_fill_rect((uint16_t)clock_x, (uint16_t)clock_y, (uint16_t)clock_w, 20, CLR_TASKBAR);
    draw_flat_border((uint16_t)clock_x, (uint16_t)clock_y, (uint16_t)clock_w, 20, 0x888888);
    char time[6];
    read_time(time);
    gpu_draw_text((uint16_t)(clock_x + 8), (uint16_t)(clock_y + 5), time, 0x444444, CLR_TASKBAR);
}

/* ==================== Menu Panel ==================== */

static void draw_menu_panel(uint16_t screen_w, uint16_t screen_h) {
    (void)screen_w;
    int mx = 2, my = screen_h - TASKBAR_H - 200;
    int mw = 180, mh = 198;

    /* Menu background */
    gpu_fill_rect((uint16_t)mx, (uint16_t)my, (uint16_t)mw, (uint16_t)mh, CLR_MENU_BG);
    draw_flat_border((uint16_t)mx, (uint16_t)my, (uint16_t)mw, (uint16_t)mh, 0x888888);

    /* Header bar */
    gpu_fill_rect((uint16_t)(mx + 1), (uint16_t)(my + 1), (uint16_t)(mw - 2), 20, 0x0000AA);
    gpu_draw_text((uint16_t)(mx + 8), (uint16_t)(my + 5), "EponaOS", 0xFFFFFF, 0x0000AA);

    /* Menu items - flat style */
    int iy = my + 24;
    int item_h = 24;

    const char *items[] = {
        "Terminal",
        "File Manager",
        "About",
        "Help",
        "Run...",
        "-------",
        "Shut Down"
    };
    int num_items = 7;

    for (int i = 0; i < num_items; i++) {
        if (items[i][0] == '-') {
            gpu_fill_rect((uint16_t)(mx + 4), (uint16_t)(iy + 2), (uint16_t)(mw - 8), 1, 0x888888);
            iy += 6;
            continue;
        }
        /* Hover-ready flat item */
        gpu_fill_rect((uint16_t)(mx + 2), (uint16_t)iy, (uint16_t)(mw - 4), (uint16_t)item_h, CLR_MENU_BG);
        gpu_draw_text((uint16_t)(mx + 12), (uint16_t)(iy + 6), items[i], 0x000000, CLR_MENU_BG);
        iy += item_h;
    }
}

/* ==================== Cursor ==================== */

static int cursor_w(void) { return 12; }
static int cursor_h(void) { return 16; }

static gui_pointer_t read_pointer(uint16_t w, uint16_t h) {
    gui_pointer_t p;
    int mx, my;
    uint8_t buttons;
    mouse_get_state(&mx, &my, &buttons);
    int x = (int)(w / 2) + mx;
    int y = (int)(h / 2) - my;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (int)w - cursor_w()) x = (int)w - cursor_w();
    if (y > (int)h - cursor_h()) y = (int)h - cursor_h();
    p.x = x; p.y = y; p.buttons = buttons;
    return p;
}

static void cursor_init(void) {
    if (g_cursor_bg) return;
    g_cursor_bg = (uint32_t *)kmalloc((uint32_t)(cursor_w() * cursor_h() * 4));
}

static void save_cursor_bg(int x, int y) {
    gpu_save_region(g_cursor_bg, (uint16_t)x, (uint16_t)y, (uint16_t)cursor_w(), (uint16_t)cursor_h());
    g_cursor_x = x; g_cursor_y = y; g_cursor_saved = 1;
}

static void restore_cursor_bg(void) {
    if (!g_cursor_saved) return;
    gpu_restore_region(g_cursor_bg, (uint16_t)g_cursor_x, (uint16_t)g_cursor_y,
                       (uint16_t)cursor_w(), (uint16_t)cursor_h());
    g_cursor_saved = 0;
}

/* KolibriOS-style arrow cursor */
static void draw_cursor_bitmap(int bx, int by) {
    static const char *cursor[16] = {
        "X...........",
        "XX..........",
        "XXX.........",
        "XXXX........",
        "XXXXX.......",
        "XXXXXX......",
        "XXXXXXX.....",
        "XXXXXXXX....",
        "XXXXXXXXX...",
        "XX..XXXXX...",
        "X...XXXX....",
        "....XXXX....",
        "...XXX......",
        "...XXX......",
        "..XX........",
        "..XX........",
    };
    for (int yy = 0; yy < 16; yy++)
        for (int xx = 0; xx < 12; xx++)
            if (cursor[yy][xx] == 'X')
                gpu_put_pixel((uint16_t)(bx + xx), (uint16_t)(by + yy), 0x000000);
}

static void draw_cursor_at(gui_pointer_t p) {
    cursor_init();
    save_cursor_bg(p.x, p.y);
    draw_cursor_bitmap(p.x, p.y);
}

/* ==================== Desktop ==================== */

static void draw_desktop(uint16_t w, uint16_t h) {
    gpu_fill_rect(0, 0, w, h, CLR_DESKTOP);
    draw_desktop_icons(w);
    draw_taskbar(w, h);
}

/* ==================== Main Loop ==================== */

void gui_run_desktop(void) {
    if (!gpu_is_framebuffer_enabled()) {
        gpu_print("GUI requer framebuffer VBE.\n");
        return;
    }

    uint16_t w = gpu_width();
    uint16_t h = gpu_height();
    char now[6] = {0};
    char last_time[6] = {0};
    int last_buttons = 0;
    g_cursor_saved = 0;

    gpu_set_target_back(1);

    for (int i = 0; i < MAX_WINDOWS; i++) g_win[i].used = 0;
    g_win_count = 0;
    g_drag_zpos = -1;

    /* Create initial windows */
    int ti = win_create(WIN_TERMINAL, "Terminal", 200, 60, 520, 300);
    int ai = win_create(WIN_ABOUT, "About EponaOS", 400, 120, 380, 240);
    if (ti >= 0) win_render_terminal_init(&g_win[ti]);
    if (ai >= 0) win_render_about(&g_win[ai]);
    g_focus_zpos = g_win_count > 0 ? g_win_count - 1 : -1;

    draw_desktop(w, h);
    compose_windows();
    gpu_flip();

    read_time(now);
    for (int i = 0; i < 6; i++) last_time[i] = now[i];
    uint64_t last_clock_tick = pit_ticks();
    uint64_t last_frame_tick = pit_ticks();

    while (1) {
        uint64_t tick = pit_ticks();
        if (tick - last_clock_tick >= CLOCK_TICKS) {
            read_time(now);
            last_clock_tick = tick;
        }

        int old_x = g_cursor_x, old_y = g_cursor_y;
        int had_old = g_cursor_saved;
        restore_cursor_bg();

        int full_redraw = 0;
        if (now[0] != last_time[0] || now[1] != last_time[1] ||
            now[3] != last_time[3] || now[4] != last_time[4]) {
            for (int i = 0; i < 6; i++) last_time[i] = now[i];
            full_redraw = 1;
        }

        gui_pointer_t p = read_pointer(w, h);
        int clicked = (p.buttons & 1) && !(last_buttons & 1);
        int released = !(p.buttons & 1) && (last_buttons & 1);
        last_buttons = p.buttons;

        if (clicked) {
            /* Check Menu button */
            int mbx = 2, mby = h - TASKBAR_H + 3;
            if (point_in(p.x, p.y, mbx, mby, 50, 20)) {
                g_menu_open = !g_menu_open;
                full_redraw = 1;
            } else if (g_menu_open) {
                /* Check menu items */
                int menu_x = 2, menu_y = h - TASKBAR_H - 200;
                int iy = menu_y + 24;
                int item_h = 24;
                int items_clicked = 0;
                for (int i = 0; i < 7; i++) {
                    if (i == 5) { iy += 6; continue; } /* separator */
                    if (point_in(p.x, p.y, menu_x + 2, iy, 176, item_h)) {
                        g_menu_open = 0;
                        items_clicked = 1;
                        if (i == 0) {
                            win_create(WIN_TERMINAL, "Terminal", 200, 60, 520, 300);
                            int last = g_win_count - 1;
                            if (last >= 0) win_render_terminal_init(&g_win[g_zorder[last]]);
                        } else if (i == 1) {
                            win_create(WIN_FILEMAN, "File Manager", 150, 50, 480, 320);
                            int last = g_win_count - 1;
                            if (last >= 0) win_render_fileman(&g_win[g_zorder[last]]);
                        } else if (i == 2) {
                            win_create(WIN_ABOUT, "About EponaOS", 350, 100, 380, 240);
                            int last = g_win_count - 1;
                            if (last >= 0) win_render_about(&g_win[g_zorder[last]]);
                        } else if (i == 6) {
                            /* Shut Down */
                            draw_desktop(w, h);
                            gpu_draw_text((uint16_t)(w/2 - 100), (uint16_t)(h/2 - 10),
                                         "It is now safe to turn off your computer.",
                                         0xFFFFFF, CLR_DESKTOP);
                            gpu_flip();
                            acpi_shutdown();
                        }
                        full_redraw = 1;
                        break;
                    }
                    iy += item_h;
                }
                if (!items_clicked) {
                    g_menu_open = 0;
                    full_redraw = 1;
                }
            } else {
                /* Window hit test */
                int zpos = win_hit_test(p.x, p.y);
                if (zpos >= 0) {
                    window_t *hw = &g_win[g_zorder[zpos]];
                    int ow = win_total_w(hw);
                    int cbx = hw->x + ow - 18;
                    int cby = hw->y + 4;
                    if (point_in(p.x, p.y, cbx, cby, 14, 14)) {
                        win_close(zpos);
                        g_focus_zpos = g_win_count > 0 ? g_win_count - 1 : -1;
                    } else {
                        win_raise(zpos);
                        g_focus_zpos = g_win_count - 1;
                        if (point_in(p.x, p.y, hw->x, hw->y, ow, TITLEBAR_H)) {
                            g_drag_zpos = g_focus_zpos;
                            g_drag_dx = p.x - hw->x;
                            g_drag_dy = p.y - hw->y;
                        }
                    }
                    full_redraw = 1;
                }
                /* Check desktop icon clicks */
                if (!full_redraw) {
                    int icon_xs[] = {20, 90, 160, 230, 300, 20, 90, 160, 230, 300};
                    int icon_ys[] = {20, 20, 20, 20, 20, 90, 90, 90, 90, 90};
                    for (int i = 0; i < 10; i++) {
                        if (point_in(p.x, p.y, icon_xs[i], icon_ys[i], 40, 60)) {
                            if (i == 0) {
                                win_create(WIN_TERMINAL, "Terminal", 200, 60, 520, 300);
                                int last = g_win_count - 1;
                                if (last >= 0) win_render_terminal_init(&g_win[g_zorder[last]]);
                            } else if (i == 1) {
                                win_create(WIN_FILEMAN, "File Manager", 150, 50, 480, 320);
                                int last = g_win_count - 1;
                                if (last >= 0) win_render_fileman(&g_win[g_zorder[last]]);
                            } else if (i == 2) {
                                win_create(WIN_ABOUT, "About EponaOS", 350, 100, 380, 240);
                                int last = g_win_count - 1;
                                if (last >= 0) win_render_about(&g_win[g_zorder[last]]);
                            } else if (i == 9) {
                                draw_desktop(w, h);
                                gpu_draw_text((uint16_t)(w/2 - 100), (uint16_t)(h/2 - 10),
                                              "It is now safe to turn off your computer.",
                                              0xFFFFFF, CLR_DESKTOP);
                                gpu_flip();
                                acpi_shutdown();
                            }
                            full_redraw = 1;
                            break;
                        }
                    }
                }
            }
        } else if (released) {
            g_drag_zpos = -1;
        } else if ((p.buttons & 1) && g_drag_zpos >= 0) {
            window_t *dw = &g_win[g_zorder[g_drag_zpos]];
            int nx = p.x - g_drag_dx;
            int ny = p.y - g_drag_dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (nx > (int)w - win_total_w(dw)) nx = (int)w - win_total_w(dw);
            if (ny > (int)h - win_total_h(dw) - TASKBAR_H) ny = (int)h - win_total_h(dw) - TASKBAR_H;
            if (nx != dw->x || ny != dw->y) { dw->x = nx; dw->y = ny; full_redraw = 1; }
        }

        if (full_redraw) {
            draw_desktop(w, h);
            compose_windows();
            if (g_menu_open) draw_menu_panel(w, h);
            draw_cursor_bitmap(p.x, p.y);
            gpu_flip();
        } else {
            draw_cursor_at(p);
            int rx0 = had_old ? (old_x < p.x ? old_x : p.x) : p.x;
            int ry0 = had_old ? (old_y < p.y ? old_y : p.y) : p.y;
            int rx1 = had_old ? (old_x > p.x ? old_x : p.x) + cursor_w() : p.x + cursor_w();
            int ry1 = had_old ? (old_y > p.y ? old_y : p.y) + cursor_h() : p.y + cursor_h();
            gpu_flip_rect((uint16_t)rx0, (uint16_t)ry0, (uint16_t)(rx1 - rx0), (uint16_t)(ry1 - ry0));
        }

        uint64_t target = last_frame_tick + FRAME_TICKS;
        while (pit_ticks() < target) __asm__ volatile("pause");
        last_frame_tick = pit_ticks();

        int c = keyboard_getc();
        if (c == 27) break;
        if (c && g_focus_zpos >= 0) {
            window_t *fw = &g_win[g_zorder[g_focus_zpos]];
            if (fw->kind == WIN_TERMINAL) {
                win_terminal_feed(fw, c);
                gpu_blit_buffer(fw->buf, (uint16_t)fw->cw, (uint16_t)fw->ch,
                                (uint16_t)win_client_x(fw), (uint16_t)win_client_y(fw));
                gpu_flip_rect((uint16_t)win_client_x(fw), (uint16_t)win_client_y(fw),
                              (uint16_t)fw->cw, (uint16_t)fw->ch);
            }
        }
    }

    for (int z = g_win_count - 1; z >= 0; z--) win_close(z);
    gpu_set_target_back(0);
    gpu_clear(0x0F, 0x00);
}
