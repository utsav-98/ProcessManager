#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include "ui.h"
#include "process_list.h"

// Theme enum - added more themes because why not
typedef enum {
    THEME_DEFAULT,
    THEME_DRACULA,
    THEME_MATRIX,      // my favorite :)
    THEME_SOLARIZED,
    THEME_MONOKAI,
    THEME_GRUVBOX,
    THEME_NORD,
    THEME_CATPPUCCIN,
    THEME_TOKYO_NIGHT,
    THEME_EVERFOREST,
    THEME_COUNT
} Theme;

static Theme current_theme = THEME_DEFAULT;
static int is_searching = 0;
static int pending_g = 0;  // for vim-style 'gg' navigation
static int show_cpu_cores = 0;
static int show_process_details = 0;
static int mem_in_mb = 0;          // 0 for KB, 1 for MB
static int show_help = 0;          // for help menu popup
static int show_kill_confirm = 0;  // for kill confirmation popup
static pid_t kill_confirm_pid = 0;  // PID to kill if confirmed
static char kill_confirm_name[64]; // Name of process to kill if confirmed
static int kill_confirm_selected = 0; // 0 for Yes, 1 for No

// color pair macros - each theme gets 4 pairs
#define PAIR_HEADER(t) (1 + (t)*4)
#define PAIR_SELECT(t) (2 + (t)*4)
#define PAIR_BG(t) (3 + (t)*4)
#define PAIR_BORDER(t) (50 + (t))

// gauge colors (shared across themes)
#define PAIR_GAUGE_LOW   10
#define PAIR_GAUGE_MID   11
#define PAIR_GAUGE_HIGH  12

void init_ui() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);  // hide cursor
    keypad(stdscr, TRUE);
    timeout(100);  // 100ms timeout for getch()
    start_color();
    use_default_colors();
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    // Initialize all the themes
    // (these are approximations using 8 basic colors)
    
    init_pair(PAIR_HEADER(THEME_DEFAULT), COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_SELECT(THEME_DEFAULT), COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_BG(THEME_DEFAULT), COLOR_CYAN, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_DEFAULT), COLOR_CYAN, -1);

    init_pair(PAIR_HEADER(THEME_DRACULA), COLOR_YELLOW, COLOR_MAGENTA);
    init_pair(PAIR_SELECT(THEME_DRACULA), COLOR_WHITE, COLOR_RED);
    init_pair(PAIR_BG(THEME_DRACULA), COLOR_MAGENTA, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_DRACULA), COLOR_MAGENTA, -1);

    init_pair(PAIR_HEADER(THEME_MATRIX), COLOR_BLACK, COLOR_GREEN);
    init_pair(PAIR_SELECT(THEME_MATRIX), COLOR_BLACK, COLOR_WHITE);
    init_pair(PAIR_BG(THEME_MATRIX), COLOR_GREEN, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_MATRIX), COLOR_GREEN, -1);
    
    init_pair(PAIR_HEADER(THEME_SOLARIZED), COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_SELECT(THEME_SOLARIZED), COLOR_WHITE, COLOR_CYAN);
    init_pair(PAIR_BG(THEME_SOLARIZED), COLOR_BLUE, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_SOLARIZED), COLOR_BLUE, -1);
    
    init_pair(PAIR_HEADER(THEME_MONOKAI), COLOR_BLACK, COLOR_YELLOW);
    init_pair(PAIR_SELECT(THEME_MONOKAI), COLOR_WHITE, COLOR_MAGENTA);
    init_pair(PAIR_BG(THEME_MONOKAI), COLOR_YELLOW, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_MONOKAI), COLOR_YELLOW, -1);
    
    init_pair(PAIR_HEADER(THEME_GRUVBOX), COLOR_BLACK, COLOR_YELLOW);
    init_pair(PAIR_SELECT(THEME_GRUVBOX), COLOR_YELLOW, COLOR_RED);
    init_pair(PAIR_BG(THEME_GRUVBOX), COLOR_YELLOW, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_GRUVBOX), COLOR_RED, -1);
    
    init_pair(PAIR_HEADER(THEME_NORD), COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_SELECT(THEME_NORD), COLOR_BLACK, COLOR_WHITE);
    init_pair(PAIR_BG(THEME_NORD), COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_NORD), COLOR_CYAN, -1);
    
    init_pair(PAIR_HEADER(THEME_CATPPUCCIN), COLOR_BLACK, COLOR_MAGENTA);
    init_pair(PAIR_SELECT(THEME_CATPPUCCIN), COLOR_BLACK, COLOR_CYAN);
    init_pair(PAIR_BG(THEME_CATPPUCCIN), COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_CATPPUCCIN), COLOR_MAGENTA, -1);
    
    init_pair(PAIR_HEADER(THEME_TOKYO_NIGHT), COLOR_WHITE, COLOR_BLUE);
    init_pair(PAIR_SELECT(THEME_TOKYO_NIGHT), COLOR_BLACK, COLOR_MAGENTA);
    init_pair(PAIR_BG(THEME_TOKYO_NIGHT), COLOR_BLUE, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_TOKYO_NIGHT), COLOR_BLUE, -1);
    
    init_pair(PAIR_HEADER(THEME_EVERFOREST), COLOR_BLACK, COLOR_GREEN);
    init_pair(PAIR_SELECT(THEME_EVERFOREST), COLOR_BLACK, COLOR_YELLOW);
    init_pair(PAIR_BG(THEME_EVERFOREST), COLOR_GREEN, COLOR_BLACK);
    init_pair(PAIR_BORDER(THEME_EVERFOREST), COLOR_GREEN, -1);

    // gauge colors for progress bars
    init_pair(PAIR_GAUGE_LOW, COLOR_GREEN, -1);
    init_pair(PAIR_GAUGE_MID, COLOR_YELLOW, -1);
    init_pair(PAIR_GAUGE_HIGH, COLOR_RED, -1);

    // Extra pairs for the confirmation popup
    init_pair(60, COLOR_RED, -1);    // Selected action
    init_pair(61, COLOR_WHITE, -1);  // Inactive action
    init_pair(62, COLOR_BLACK, COLOR_RED); // Focused button (filled)
}

void cleanup_ui() {
    endwin();
}

void toggle_theme() {
    current_theme = (current_theme + 1) % THEME_COUNT;
}

void reset_search_mode() {
    is_searching = 0;
}

// draws a progress bar like [||||||||....]
void draw_bar(int y, int x, int width, float percent, int color_pair_unused) {
    (void)color_pair_unused;
    
    int color = PAIR_GAUGE_LOW;
    if (percent > 75.0) color = PAIR_GAUGE_HIGH;
    else if (percent > 50.0) color = PAIR_GAUGE_MID;

    mvprintw(y, x, "[");
    
    int bar_width = width - 2;
    int filled = (int)(bar_width * (percent / 100.0f));
    
    attron(COLOR_PAIR(color));
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            addch('|');
        } else {
            attroff(COLOR_PAIR(color));
            addch('.');
        }
    }
    attroff(COLOR_PAIR(color));
    
    mvprintw(y, x + width - 1, "]");
}

void draw_box(int y, int x, int h, int w, int color_pair, const char *title) {
    attron(COLOR_PAIR(color_pair));
    
    mvhline(y, x, 0, w);
    mvhline(y+h-1, x, 0, w);
    mvvline(y, x, 0, h);
    mvvline(y, x+w-1, 0, h);
    
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x+w-1, ACS_URCORNER);
    mvaddch(y+h-1, x, ACS_LLCORNER);
    mvaddch(y+h-1, x+w-1, ACS_LRCORNER);
    
    if (title) {
        mvprintw(y, x+2, " %s ", title);
    }
    
    attroff(COLOR_PAIR(color_pair));
}

// draws a confirmation popup in the center of the screen
void draw_kill_confirm_popup(pid_t pid, const char *process_name) {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    // popup dimensions - bigger to fit the new style
    int popup_h = 10;
    int popup_w = 54;
    int popup_y = (height - popup_h) / 2;
    int popup_x = (width - popup_w) / 2;
    
    // fill background with a shadow-like effect or just solid
    attron(COLOR_PAIR(PAIR_BG(current_theme)));
    for (int i = 0; i < popup_h; i++) {
        mvhline(popup_y + i, popup_x, ' ', popup_w);
    }
    attroff(COLOR_PAIR(PAIR_BG(current_theme)));
    
    // Draw the main border (reddish/orange)
    draw_box(popup_y, popup_x, popup_h, popup_w, PAIR_GAUGE_HIGH, "SIGKILL");
    
    // Center text lines
    char line1[128], line2[128];
    snprintf(line1, sizeof(line1), "Send signal: 9 (SIGKILL)");
    snprintf(line2, sizeof(line2), "To PID: %d (%s)", pid, process_name);
    
    int line1_x = popup_x + (popup_w - (int)strlen(line1)) / 2;
    int line2_x = popup_x + (popup_w - (int)strlen(line2)) / 2;
    
    // Draw Line 1 with highlight on '9'
    mvprintw(popup_y + 2, line1_x, "Send signal: ");
    attron(COLOR_PAIR(60) | A_BOLD);
    printw("9");
    attroff(COLOR_PAIR(60) | A_BOLD);
    printw(" (SIGKILL)");
    
    // Draw Line 2 with highlight on PID
    mvprintw(popup_y + 3, line2_x, "To PID: ");
    attron(COLOR_PAIR(60) | A_BOLD);
    printw("%d", pid);
    attroff(COLOR_PAIR(60) | A_BOLD);
    printw(" (%s)", process_name);
    
    // Draw Buttons
    int btn_w = 14;
    int btn_h = 3;
    int btn_spacing = 4;
    int total_btns_w = (btn_w * 2) + btn_spacing;
    int btns_start_x = popup_x + (popup_w - total_btns_w) / 2;
    int btns_y = popup_y + 5;
    
    // "Yes" Button
    if (kill_confirm_selected == 0) {
        // Draw filled background for selected button
        attron(COLOR_PAIR(62));
        for (int i = 0; i < btn_h - 2; i++) {
            mvhline(btns_y + 1 + i, btns_start_x + 1, ' ', btn_w - 2);
        }
        mvprintw(btns_y + 1, btns_start_x + (btn_w - 3) / 2, "Yes");
        attroff(COLOR_PAIR(62));
        draw_box(btns_y, btns_start_x, btn_h, btn_w, PAIR_GAUGE_HIGH, NULL);
    } else {
        draw_box(btns_y, btns_start_x, btn_h, btn_w, 61, NULL);
        mvprintw(btns_y + 1, btns_start_x + (btn_w - 3) / 2, "Yes");
    }
    
    // "No" Button
    if (kill_confirm_selected == 1) {
        // Draw filled background for selected button
        attron(COLOR_PAIR(62));
        for (int i = 0; i < btn_h - 2; i++) {
            mvhline(btns_y + 1 + i, btns_start_x + btn_w + btn_spacing + 1, ' ', btn_w - 2);
        }
        mvprintw(btns_y + 1, btns_start_x + btn_w + btn_spacing + (btn_w - 2) / 2, "No");
        attroff(COLOR_PAIR(62));
        draw_box(btns_y, btns_start_x + btn_w + btn_spacing, btn_h, btn_w, PAIR_GAUGE_HIGH, NULL);
    } else {
        draw_box(btns_y, btns_start_x + btn_w + btn_spacing, btn_h, btn_w, 61, NULL);
        mvprintw(btns_y + 1, btns_start_x + btn_w + btn_spacing + (btn_w - 2) / 2, "No");
    }
}

// draws a help menu popup in the center of the screen
void draw_help_menu() {
    int height, width;
    getmaxyx(stdscr, height, width);
    
    int popup_h = 16;
    int popup_w = 70; // Wider to accommodate two columns
    int popup_y = (height - popup_h) / 2;
    int popup_x = (width - popup_w) / 2;
    
    attron(COLOR_PAIR(PAIR_BG(current_theme)));
    for (int i = 0; i < popup_h; i++) {
        mvhline(popup_y + i, popup_x, ' ', popup_w);
    }
    attroff(COLOR_PAIR(PAIR_BG(current_theme)));
    
    draw_box(popup_y, popup_x, popup_h, popup_w, PAIR_BORDER(current_theme), "Help Menu");
    
    int ty = popup_y + 2;
    int col1_x = popup_x + 3;
    int col2_x = popup_x + 35; // Start of second column
    
    // Column 1: Navigation
    attron(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    mvprintw(ty, col1_x, "Navigation");
    attroff(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    
    int curr_y = ty + 2;
    mvprintw(curr_y++, col1_x, "j, Down : Scroll Down");
    mvprintw(curr_y++, col1_x, "k, Up   : Scroll Up");
    mvprintw(curr_y++, col1_x, "gg      : Jump to Top");
    mvprintw(curr_y++, col1_x, "G       : Jump to Bottom");
    mvprintw(curr_y++, col1_x, "h, l    : Select Button");
    
    // Column 2: Actions
    attron(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    mvprintw(ty, col2_x, "Actions");
    attroff(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    
    curr_y = ty + 2;
    mvprintw(curr_y++, col2_x, "K     : Kill (SIGKILL)");
    mvprintw(curr_y++, col2_x, "/     : Search/Filter");
    mvprintw(curr_y++, col2_x, "Enter : Toggle Details");
    mvprintw(curr_y++, col2_x, "M     : Memory unit");
    mvprintw(curr_y++, col2_x, "t     : Cycle Theme");
    mvprintw(curr_y++, col2_x, "1     : CPU Core View");
    mvprintw(curr_y++, col2_x, "c,m,p : Sort Mode");
    mvprintw(curr_y++, col2_x, "H     : Toggle Help");
    mvprintw(curr_y++, col2_x, "q,ESC : Quit/Back");
    
    attron(A_DIM);
    mvprintw(popup_y + popup_h - 2, popup_x + (popup_w - 22) / 2, "-- Press H to close --");
    attroff(A_DIM);
}


void draw_ui(ProcessList *list, int selected_index, int scroll_offset, SystemInfo *sys_info) {
    int height, width;
    getmaxyx(stdscr, height, width);

    bkgd(COLOR_PAIR(PAIR_BG(current_theme)));
    erase();

    
    int dash_h = 12;  // dashboard height
    int col_w = width / 3;
    
    // CPU panel
    draw_box(0, 0, dash_h, col_w, PAIR_BORDER(current_theme), "CPU Info [1:Cores]");
    
    if (show_cpu_cores && sys_info->core_count > 0) {
        // per-core view - fits up to 20 cores in 2 columns
        int cores_per_col = dash_h - 2;
        int col1_w = (col_w - 4) / 2;
        
        for (int i = 0; i < sys_info->core_count; i++) {
            if (i >= cores_per_col * 2) break;  // can't fit more
            
            int row = i % cores_per_col;
            int col_x = (i / cores_per_col) * (col1_w + 1) + 2;
            
            char label[16];
            snprintf(label, sizeof(label), "%d:%.0f%%", i, sys_info->core_percents[i]);
            mvprintw(1+row, col_x, "%-7s", label);
            
            draw_bar(1+row, col_x+8, col1_w - 9, sys_info->core_percents[i], 0);
        }
    } else {
        // aggregate view
        mvprintw(2, 2, "Usage: %5.1f%%", sys_info->cpu_percent);
        draw_bar(3, 2, col_w - 4, sys_info->cpu_percent, 0);
    }

    // Memory panel
    draw_box(0, col_w, dash_h, col_w, PAIR_BORDER(current_theme), "Memory Info");
    
    float mem_percent = 0;
    if (sys_info->mem_total > 0) mem_percent = (float)sys_info->mem_used / sys_info->mem_total * 100.0f;
    
    mvprintw(1, col_w + 2, "RAM: %5.1f%%", mem_percent);
    draw_bar(2, col_w + 2, col_w - 4, mem_percent, 0);
    
    int x_start = col_w + 2;
    #define TO_MB(k) ((k)/1024)
    
    mvprintw(3, x_start, "Total: %luM", TO_MB(sys_info->mem_total));
    mvprintw(4, x_start, "Used : %luM", TO_MB(sys_info->mem_used));
    mvprintw(5, x_start, "Free : %luM", TO_MB(sys_info->mem_free));
    mvprintw(6, x_start, "Cache: %luM", TO_MB(sys_info->mem_cached));
    mvprintw(7, x_start, "Avail: %luM", TO_MB(sys_info->mem_available));
    
    // swap info
    float swap_percent = 0;
    if (sys_info->swap_total > 0) {
        unsigned long swap_used = sys_info->swap_total - sys_info->swap_free;
        swap_percent = (float)swap_used / sys_info->swap_total * 100.0f;
        mvprintw(8, x_start, "Swap : %luM / %luM", TO_MB(swap_used), TO_MB(sys_info->swap_total));
        draw_bar(9, x_start, col_w - 4, swap_percent, 0);
    } else {
        mvprintw(8, x_start, "Swap : Disabled");
    }
    
    // System info panel
    draw_box(0, col_w * 2, dash_h, width - col_w * 2, PAIR_BORDER(current_theme), "System");
    
    mvprintw(1, col_w*2 + 2, "Host: %s", sys_info->hostname);
    mvprintw(2, col_w*2 + 2, "Kernel: %s", sys_info->kernel);
    
    mvprintw(3, col_w*2 + 2, "CPU Temp: %.1f C", sys_info->cpu_temp);
    if (sys_info->bat_temp > 0) {
        mvprintw(3, col_w*2 + 22, "Bat: %.1f C", sys_info->bat_temp);
    }
    
    mvprintw(4, col_w*2 + 2, "Load: %.2f %.2f %.2f", sys_info->load_avg[0], sys_info->load_avg[1], sys_info->load_avg[2]);
    mvprintw(5, col_w*2 + 2, "IO R: %.0f K/s W: %.0f K/s", sys_info->disk_read_rate, sys_info->disk_write_rate);

    // Process list
    int list_start_y = dash_h;
    int list_h = height - list_start_y - 1;
    
    // table header
    attron(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    mvprintw(list_start_y, 0, "%-8s %-12s %-10s %-10s %-10s %-10s %s", 
             " PID", " PROG", " USER", mem_in_mb ? " MEM (MB)" : " MEM (KB)", " CPU (%)", " STATE", " COMMAND");
    attroff(A_BOLD | COLOR_PAIR(PAIR_HEADER(current_theme)));
    
    // process rows
    for (int i = 0; i < list_h - 1; i++) {
        int process_idx = scroll_offset + i;
        if (process_idx >= list->count) break;

        ProcessInfo *p = &list->processes[process_idx];

        if (process_idx == selected_index) {
            attron(COLOR_PAIR(PAIR_SELECT(current_theme)));
        }

        char display_cmd[256];
        int available_width = width;
        
        int details_w = 0;
        int list_width = width;
        
        if (show_process_details) {
            details_w = (int)(width * 0.4);
            list_width = width - details_w;
            available_width = list_width;
        }

        // truncate command if too long
        int cmd_col = 74;
        if (available_width > cmd_col) {
            strncpy(display_cmd, p->command, available_width - cmd_col - 1);
            display_cmd[available_width-cmd_col-1] = '\0';
        } else {
            if (available_width > 15) {
                strncpy(display_cmd, p->command, available_width - 15);
                display_cmd[available_width-15] = '\0';
            } else {
                display_cmd[0] = '\0';
            }
        }

        char display_name[16];
        strncpy(display_name, p->name, 12);
        display_name[12] = '\0';

        char line_buf[512];
        if (mem_in_mb) {
            snprintf(line_buf, sizeof(line_buf), " %-8d %-12s %-10s %-10.1f %-10.1f %-10c %s", 
                     p->pid, display_name, p->user, (float)p->memory_sq / 1024.0f, p->cpu_usage, p->state, display_cmd);
        } else {
            snprintf(line_buf, sizeof(line_buf), " %-8d %-12s %-10s %-10lu %-10.1f %-10c %s", 
                     p->pid, display_name, p->user, p->memory_sq, p->cpu_usage, p->state, display_cmd);
        }
        
        mvaddnstr(list_start_y + 1 + i, 0, line_buf, list_width);
        
        if (process_idx == selected_index) {
             mvchgat(list_start_y + 1 + i, 0, list_width, A_NORMAL, PAIR_SELECT(current_theme), NULL);
             attroff(COLOR_PAIR(PAIR_SELECT(current_theme)));
        }
    }
    
    // Process details sidebar (if enabled)
    if (show_process_details) {
        int details_w = (int)(width * 0.4);
        int details_x = width - details_w;
        int details_h = list_h;
        int details_y = list_start_y;

        draw_box(details_y, details_x, details_h, details_w, PAIR_BORDER(current_theme), "Process Details");
        
        if (selected_index >= 0 && selected_index < list->count) {
             ProcessInfo *sel = &list->processes[selected_index];
             int tx = details_x + 2;
             int ty = details_y + 2;
             
             attron(A_BOLD);
             mvprintw(ty++, tx, "PID: %d  (Parent: %d)", sel->pid, sel->ppid);
             mvprintw(ty++, tx, "Name: %s", sel->user);
             attroff(A_BOLD);
             
             ty++;
             mvprintw(ty++, tx, "State: %s (%c)", sel->status_name, sel->state);
             mvprintw(ty++, tx, "Threads: %d", sel->threads);
             mvprintw(ty++, tx, "Prior/Nice: %d / %d", sel->priority, sel->nice);
             
             ty++;
             if (mem_in_mb) {
                 mvprintw(ty++, tx, "Memory: %.1f MB", (float)sel->memory_sq / 1024.0f);
             } else {
                 mvprintw(ty++, tx, "Memory: %lu KB", sel->memory_sq);
             }
             mvprintw(ty++, tx, "CPU: %.1f%%", sel->cpu_usage);
             
             ty++;
             mvprintw(ty++, tx, "Command:");
             // wrap long command lines
             int max_cmd_w = details_w - 4;
             int cmd_len = strlen(sel->command);
             int printed = 0;
             while (printed < cmd_len && ty < details_y + details_h - 1) {
                 mvaddnstr(ty++, tx, sel->command + printed, max_cmd_w);
                 printed += max_cmd_w;
             }
        } else {
             mvprintw(details_y + 2, details_x + 2, "No Process Selected");
        }
    }

    // Status bar at bottom
    const char *sort_str = "PID";
    if (list->sort_mode == SORT_MEM) sort_str = "MEM";
    if (list->sort_mode == SORT_CPU) sort_str = "CPU";
    
    const char *theme_str = "Def";
    if (current_theme == THEME_DRACULA) theme_str = "Drac";
    if (current_theme == THEME_MATRIX) theme_str = "Mtrx";
    if (current_theme == THEME_SOLARIZED) theme_str = "Solr";
    if (current_theme == THEME_MONOKAI) theme_str = "Mono";
    if (current_theme == THEME_GRUVBOX) theme_str = "Gruv";
    if (current_theme == THEME_NORD) theme_str = "Nord";
    if (current_theme == THEME_CATPPUCCIN) theme_str = "Catp";
    if (current_theme == THEME_TOKYO_NIGHT) theme_str = "Toky";
    if (current_theme == THEME_EVERFOREST) theme_str = "Ever";

    move(height-1, 0);
    clrtoeol();
    
    if (is_searching) {
        attron(A_REVERSE);
        printw("SEARCH: %s_", list->filter);
        attroff(A_REVERSE);
    } else if (list->filter[0] != '\0') {
         printw("Filter: %s (Esc to clear) | Found: %d", list->filter, list->count);
    } else {
         printw("Total: %d | Sort: %s | Theme: %s | /:Search | q:Quit | H:Help | t:Theme | M:MemUnit | K:Kill", 
                 list->count, sort_str, theme_str);
    }
    
    if (show_kill_confirm) {
        draw_kill_confirm_popup(kill_confirm_pid, kill_confirm_name);
    }
    
    if (show_help) {
        draw_help_menu();
    }
    
    refresh();
}

int handle_input(int ch, ProcessList *list, int *selected_index, int *scroll_offset) {
    int height, width;
    getmaxyx(stdscr, height, width);
    (void)width;
    
    int list_height = height - 14;
    if (list_height < 1) list_height = 1;

    // search mode input handling
    if (is_searching) {
        if (ch == 27) {  // ESC
            is_searching = 0;
            list->filter[0] = '\0';
            return ACTION_REFRESH;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            is_searching = 0;
            return ACTION_REDRAW;
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            size_t len = strlen(list->filter);
            if (len > 0) {
                list->filter[len - 1] = '\0';
                return ACTION_REFRESH;
            }
        } else if (ch >= 32 && ch <= 126) {  // printable chars
            size_t len = strlen(list->filter);
            if (len < sizeof(list->filter) - 1) {
                list->filter[len] = (char)ch;
                list->filter[len + 1] = '\0';
                return ACTION_REFRESH;
            }
        }
        return ACTION_NONE;
    }

    // vim-style 'gg' navigation
    if (pending_g) {
        if (ch == 'g') {
            *selected_index = 0;
            *scroll_offset = 0;
            pending_g = 0;
            return ACTION_REDRAW;
        }
        pending_g = 0;
        return ACTION_NONE;
    }

    // kill confirmation input handling
    if (show_kill_confirm) {
        if (ch == 'y' || ch == 'Y' || (ch == '\n' && kill_confirm_selected == 0)) {
            kill(kill_confirm_pid, 9); // SIGKILL
            show_kill_confirm = 0;
            return ACTION_REFRESH;
        } else if (ch == 'n' || ch == 'N' || ch == 27 || (ch == '\n' && kill_confirm_selected == 1)) {
            show_kill_confirm = 0;
            return ACTION_REDRAW;
        } else if (ch == KEY_LEFT || ch == 'h') {
            kill_confirm_selected = 0;
            return ACTION_REDRAW;
        } else if (ch == KEY_RIGHT || ch == 'l') {
            kill_confirm_selected = 1;
            return ACTION_REDRAW;
        }
        return ACTION_NONE;
    }

    // help menu input handling
    if (show_help) {
        if (ch == 'h' || ch == 'H' || ch == 27 || ch == 'q') {
            show_help = 0;
            return ACTION_REDRAW;
        }
        return ACTION_NONE;
    }

    switch (ch) {
        case KEY_MOUSE: {
            MEVENT event;
            if (getmouse(&event) == OK) {
                // mouse wheel scrolling
                if (event.bstate & (BUTTON4_PRESSED | 65536)) {  // scroll up
                    if (*selected_index > 0) {
                         (*selected_index)--;
                         if (*selected_index < *scroll_offset) (*scroll_offset)--;
                         return ACTION_REDRAW;
                    }
                }
                if (event.bstate & (BUTTON5_PRESSED | 2097152)) {  // scroll down
                    if (*selected_index < list->count - 1) {
                         (*selected_index)++;
                         if (*selected_index >= *scroll_offset + list_height) (*scroll_offset)++;
                         return ACTION_REDRAW;
                    }
                }
                #if NCURSES_MOUSE_VERSION > 1
                    if (event.bstate & BUTTON4_PRESSED) {
                        if (*selected_index > 0) {
                             (*selected_index)--;
                             if (*selected_index < *scroll_offset) (*scroll_offset)--;
                             return ACTION_REDRAW;
                        }
                    }
                    if (event.bstate & BUTTON5_PRESSED) {
                        if (*selected_index < list->count - 1) {
                             (*selected_index)++;
                             if (*selected_index >= *scroll_offset + list_height) (*scroll_offset)++;
                             return ACTION_REDRAW;
                        }
                    }
                #endif
            }
            break;
        }
        case '1':
            show_cpu_cores = !show_cpu_cores;
            return ACTION_REDRAW;
        case '/':
            is_searching = 1;
            list->filter[0] = '\0';
            return ACTION_REFRESH;
        case 'g':
            pending_g = 1;
            break;
        case 'G':  // jump to bottom
            *selected_index = list->count - 1;
            if (*selected_index < 0) *selected_index = 0;
            if (*selected_index >= *scroll_offset + list_height) {
                *scroll_offset = *selected_index - list_height + 1;
            }
            if (list->count < list_height) {
                *scroll_offset = 0;
            }
            return ACTION_REDRAW;
        case 'h':
        case 'H':
            show_help = !show_help;
            return ACTION_REDRAW;
        case KEY_UP:
        case 'k':
            if (*selected_index > 0) {
                (*selected_index)--;
                if (*selected_index < *scroll_offset) {
                    (*scroll_offset)--;
                }
                return ACTION_REDRAW;
            }
            break;
        case KEY_DOWN:
        case 'j':
            if (*selected_index < list->count - 1) {
                (*selected_index)++;
                if (*selected_index >= *scroll_offset + list_height) {
                    (*scroll_offset)++;
                }
                return ACTION_REDRAW;
            }
            break;
        case 'K':  // kill process confirmation
            if (list->count > 0 && *selected_index < list->count) {
                 kill_confirm_pid = list->processes[*selected_index].pid;
                 strncpy(kill_confirm_name, list->processes[*selected_index].name, sizeof(kill_confirm_name) - 1);
                 kill_confirm_name[sizeof(kill_confirm_name) - 1] = '\0';
                 show_kill_confirm = 1;
                 kill_confirm_selected = 0; // default to Yes
                 return ACTION_REDRAW;
            }
            break;
        case 'M':
            mem_in_mb = !mem_in_mb;
            return ACTION_REDRAW;
        case 'm':
            list->sort_mode = SORT_MEM;
            sort_process_list(list);
            return ACTION_REDRAW;
        case 'c':
            list->sort_mode = SORT_CPU;
            sort_process_list(list);
            return ACTION_REDRAW;
        case 'p':
            list->sort_mode = SORT_PID;
            sort_process_list(list);
            return ACTION_REDRAW;
        case 't':
            toggle_theme();
            return ACTION_REDRAW;
        case '\n':
        case KEY_ENTER:
            show_process_details = !show_process_details;
            return ACTION_REDRAW;
    }
    return ACTION_NONE;
}
