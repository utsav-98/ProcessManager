#include "process_list.h"
#include "ui.h"
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// TODO: network stats?
// FIXME: selection jumps when filtering? fixed? ::: FIXED BTW
// WTF it's sunday again

int main() {
  // double buffering - basically we keep 2 lists to compare CPU usage
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

  // main loop - runs forever until user quits
  while (1) {
    if (needs_redraw) {
      get_system_info(&sys_info, list, prev_list);
      draw_ui(list, selected_index, scroll_offset, &sys_info);
      needs_redraw = 0;
    }

    ch = getch(); // timeout is 100ms, set in init_ui()

    if (ch == 'q') {
      break; // bye bye

    } else if (ch != ERR) {
      int action = handle_input(ch, list, &selected_index, &scroll_offset);

      if (action == ACTION_REFRESH) {
        refresh_process_list(list, prev_list);

        // bounds check
        if (selected_index >= list->count)
          selected_index = list->count - 1;
        if (selected_index < 0)
          selected_index = 0;

        needs_redraw = 1;

      } else if (action == ACTION_REDRAW) {
        needs_redraw = 1;
      }

    } else {
      // no input, check if we should auto-refresh
      static int refresh_counter = 0;
      refresh_counter++;

      // refresh every ~1 second (10 * 100ms)
      if (refresh_counter > 10) {
        pid_t current_pid = -1;
        if (selected_index < list->count) {
          current_pid = list->processes[selected_index].pid;
        }

        // swap the buffers - this is faster than copying
        ProcessList *temp = prev_list;
        prev_list = list;
        list = temp;

        // copy UI state
        list->sort_mode = prev_list->sort_mode;
        strcpy(list->filter, prev_list->filter);

        refresh_process_list(list, prev_list);

        // if filter returns nothing, clear it
        if (list->count == 0 && list->filter[0] != '\0') {
          list->filter[0] = '\0';
          reset_search_mode();
          refresh_process_list(list, prev_list);
        }

        // try to keep same process selected
        if (current_pid != -1) {
          int found = 0;

          for (int i = 0; i < list->count; i++) {
            if (list->processes[i].pid == current_pid) {
              selected_index = i;
              found = 1;
              break;
            }
          }

          if (!found) {
            if (selected_index >= list->count)
              selected_index = list->count - 1;
            if (selected_index < 0)
              selected_index = 0;
          }

          // make sure selected item is visible
          int height, width;
          getmaxyx(stdscr, height, width);
          (void)width; // shut up compiler

          int list_height = height - 14;
          if (list_height < 1)
            list_height = 1;

          if (selected_index < scroll_offset) {
            scroll_offset = selected_index;
          } else if (selected_index >= scroll_offset + list_height) {
            scroll_offset = selected_index - list_height + 1;
          }
        }

        needs_redraw = 1;
        refresh_counter = 0;
      }
    }
  }

  // cleanup
  cleanup_ui();
  free_process_list(list);
  free_process_list(prev_list);

  return 0;
}
