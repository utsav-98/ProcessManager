#include "process_list.h"
#include "ui.h"
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: network stats?
// FIXME: selection jumps when filtering? fixed?

int main() {
  ProcessList *list = create_process_list();
  ProcessList *prev_list = create_process_list();

  if (!list || !prev_list) {
    fprintf(stderr, "Failed to create process list\n");
    return 1;
  }

  init_ui();

  int selected_index = 0;
  int scroll_offset = 0;
  int ch;

  list->sort_mode = SORT_PID;
  refresh_process_list(list, NULL);

  SystemInfo sys_info = {0};
  int needs_redraw = 1;

  while (1) {
    if (needs_redraw) {
      get_system_info(&sys_info, list, prev_list);
      draw_ui(list, selected_index, scroll_offset, &sys_info);
      needs_redraw = 0;
    }

    ch = getch(); 

    if (ch == 'q') {
      break; 
    } else if (ch != ERR || needs_redraw) {
      pid_t saved_pid = -1;
      if (selected_index < list->count) {
        saved_pid = list->processes[selected_index].pid;
      }

      int action = ACTION_NONE;
      if (ch != ERR) {
        action = handle_input(ch, list, &selected_index, &scroll_offset);
      }

      static int refresh_counter = 0;
      int refresh_now = (action == ACTION_REFRESH);
      
      if (ch == ERR) {
        if (++refresh_counter > 10) {
            refresh_now = 1;
            refresh_counter = 0;
        }
      }

      if (refresh_now) {
        ProcessList *temp = prev_list;
        prev_list = list;
        list = temp;

        list->sort_mode = prev_list->sort_mode;
        strcpy(list->filter, prev_list->filter);

        refresh_process_list(list, prev_list);
        needs_redraw = 1;
      }

      if (refresh_now || action == ACTION_REDRAW || action == ACTION_REFRESH) {
        if (saved_pid != -1) {
          int found = 0;
          for (int i = 0; i < list->count; i++) {
            if (list->processes[i].pid == saved_pid) {
              selected_index = i;
              found = 1;
              break;
            }
          }
          if (!found) {
            if (selected_index >= list->count) selected_index = list->count - 1;
            if (selected_index < 0) selected_index = 0;
          }
        } else {
          if (selected_index >= list->count) selected_index = list->count - 1;
          if (selected_index < 0) selected_index = 0;
        }

        int h, w;
        getmaxyx(stdscr, h, w);
        int lh = h - 14;
        if (lh < 1) lh = 1;

        if (selected_index < scroll_offset) scroll_offset = selected_index;
        else if (selected_index >= scroll_offset + lh) scroll_offset = selected_index - lh + 1;

        if (scroll_offset > list->count - lh) scroll_offset = list->count - lh;
        if (scroll_offset < 0) scroll_offset = 0;
        
        needs_redraw = 1;
      }
    }
  }

  cleanup_ui();
  free_process_list(list);
  free_process_list(prev_list);

  return 0;
}
