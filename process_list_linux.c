#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <limits.h>
#include "process_list.h"

ProcessList* create_process_list() {
    ProcessList *list = calloc(1, sizeof(ProcessList));
    if (!list) return NULL;
    
    list->capacity = 128; // should be enough for most cases
    list->count = 0;
    
    list->processes = malloc(sizeof(ProcessInfo) * list->capacity);
    if (!list->processes) {
        free(list);
        return NULL;
    }
    
    list->filter[0] = '\0';
    return list;
}

void free_process_list(ProcessList *list) {
    if (list) {
        free(list->processes);
        free(list);
    }
}

// check if string is all digits (for PID detection)
static int is_numeric(const char *str) {
    while (*str) {
        if (!isdigit(*str)) return 0;
        str++;
    }
    return 1;
}

// read VmRSS from /proc/[pid]/status
static long unsigned int get_memory_usage(const char *pid_str) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/status", pid_str);

    FILE *f = fopen(path, "r");
    if (!f) return 0; // process probably died

    char line[256];
    long unsigned int mem = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line, "VmRSS: %lu", &mem);
            break;
        }
    }
    fclose(f);
    return mem;
}

static void get_user_name(uid_t uid, char *buffer, size_t size) {
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        strncpy(buffer, pw->pw_name, size - 1);
        buffer[size - 1] = '\0';
    } else {
        snprintf(buffer, size, "%d", uid); // fallback to UID
    }
}

// comparators for qsort
static int compare_pid(const void *a, const void *b) {
    return ((ProcessInfo*)a)->pid - ((ProcessInfo*)b)->pid;
}

static int compare_mem(const void *a, const void *b) {
    long diff = ((ProcessInfo*)b)->memory_sq - ((ProcessInfo*)a)->memory_sq;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

static int compare_cpu(const void *a, const void *b) {
    float diff = ((ProcessInfo*)b)->cpu_usage - ((ProcessInfo*)a)->cpu_usage;
    if (diff > 0) return 1;
    if (diff < 0) return -1;
    return 0;
}

void sort_process_list(ProcessList *list) {
    if (!list || list->count == 0) return;
    
    switch (list->sort_mode) {
        case SORT_MEM:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_mem);
            break;
        case SORT_CPU:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_cpu);
            break;
        case SORT_PID:
        default:
            qsort(list->processes, list->count, sizeof(ProcessInfo), compare_pid);
            break;
    }
}

// reads the "cpu" line from /proc/stat
static void get_system_cpu_times(unsigned long long *total, unsigned long long *idle_out) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;
    
    char line[256];
    if (fgets(line, sizeof(line), f)) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu", 
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
            
            *idle_out = idle + iowait;
            *total = user + nice + system + idle + iowait + irq + softirq + steal;
        }
    }
    fclose(f);
}

void get_system_info(SystemInfo *info, ProcessList *list, ProcessList *prev_list) {
    // Memory info
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[256];
        unsigned long mem_total = 0, mem_free = 0, buffers = 0, cached = 0, srecl = 0;
        unsigned long mem_avail = 0, swap_total = 0, swap_free = 0;
        
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total) == 1) continue;
            if (sscanf(line, "MemFree: %lu kB", &mem_free) == 1) continue;
            if (sscanf(line, "MemAvailable: %lu kB", &mem_avail) == 1) continue;
            if (sscanf(line, "Buffers: %lu kB", &buffers) == 1) continue;
            if (sscanf(line, "Cached: %lu kB", &cached) == 1) continue;
            if (sscanf(line, "SReclaimable: %lu kB", &srecl) == 1) continue;
            if (sscanf(line, "SwapTotal: %lu kB", &swap_total) == 1) continue;
            if (sscanf(line, "SwapFree: %lu kB", &swap_free) == 1) continue;
        }
        fclose(f);
        info->mem_total = mem_total;
        info->mem_free = mem_free;
        info->mem_available = mem_avail;
        info->mem_cached = cached + srecl;
        info->swap_total = swap_total;
        info->swap_free = swap_free;
        info->mem_used = mem_total - mem_free - buffers - cached - srecl;
    }

    // CPU percentage (delta calculation)
    if (prev_list && list->total_cpu_time > prev_list->total_cpu_time) {
        unsigned long long total_diff = list->total_cpu_time - prev_list->total_cpu_time;
        unsigned long long idle_diff = list->total_cpu_idle - prev_list->total_cpu_idle;
        unsigned long long used_diff = total_diff - idle_diff;
        info->cpu_percent = (float)used_diff / total_diff * 100.0f;
    } else {
        info->cpu_percent = 0.0f;
    }

    // Uptime
    FILE *fu = fopen("/proc/uptime", "r");
    if (fu) {
        double uptime_sec;
        if (fscanf(fu, "%lf", &uptime_sec) == 1) {
            info->uptime = uptime_sec;
        }
        fclose(fu);
    }

    gethostname(info->hostname, sizeof(info->hostname));

    // Kernel version
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        strncpy(info->kernel, buffer.release, sizeof(info->kernel) - 1);
        info->kernel[sizeof(info->kernel) - 1] = '\0';
    }

    getloadavg(info->load_avg, 3);

    // Disk I/O - this was a pain to figure out
    FILE *fd = fopen("/proc/diskstats", "r");
    unsigned long long current_read = 0, current_write = 0;
    if (fd) {
        char line[512];
        while (fgets(line, sizeof(line), fd)) {
            int major, minor;
            char dev_name[32];
            unsigned long long r_completed, r_merged, r_sectors, w_completed, w_merged, w_sectors;
            
            if (sscanf(line, "%d %d %s %llu %llu %llu %*u %llu %llu %llu", 
                       &major, &minor, dev_name, 
                       &r_completed, &r_merged, &r_sectors,
                       &w_completed, &w_merged, &w_sectors) >= 9) {
                
                // only count actual drives, not partitions
                int is_drive = 0;
                if (strncmp(dev_name, "sd", 2) == 0 && strlen(dev_name) == 3) is_drive = 1; // sda, sdb, etc
                if (strncmp(dev_name, "vd", 2) == 0 && strlen(dev_name) == 3) is_drive = 1; // vda (VMs)
                if (strncmp(dev_name, "nvme", 4) == 0 && strstr(dev_name, "n1") && !strstr(dev_name, "p")) is_drive = 1;

                if (is_drive) {
                    current_read += r_sectors;
                    current_write += w_sectors;
                }
            }
        }
        fclose(fd);
    }
    
    // calculate rates (sectors are 512 bytes)
    if (list->old_disk_read_sectors > 0) {
        if (current_read >= list->old_disk_read_sectors)
             info->disk_read_rate = (double)(current_read - list->old_disk_read_sectors) * 512.0 / 1024.0;
        else info->disk_read_rate = 0;
        
        if (current_write >= list->old_disk_write_sectors)
             info->disk_write_rate = (double)(current_write - list->old_disk_write_sectors) * 512.0 / 1024.0;
        else info->disk_write_rate = 0;
    }
    list->old_disk_read_sectors = current_read;
    list->old_disk_write_sectors = current_write;

    // Per-core CPU stats
    FILE *fstat = fopen("/proc/stat", "r");
    if (fstat) {
        char line[512];
        int core_idx = 0;
        while (fgets(line, sizeof(line), fstat)) {
            if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
                if (core_idx >= 32) break; // max 32 cores
                unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
                sscanf(line, "%*s %llu %llu %llu %llu %llu %llu %llu %llu", 
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
                
                unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
                unsigned long long idle_total = idle + iowait;
                
                unsigned long long diff_total = 0;
                unsigned long long diff_idle = 0;
                
                if (list->core_old_totals[core_idx] > 0) {
                    diff_total = total - list->core_old_totals[core_idx];
                    diff_idle = idle_total - list->core_old_idles[core_idx];
                }
                
                if (diff_total > 0) {
                    info->core_percents[core_idx] = (float)(diff_total - diff_idle) / diff_total * 100.0f;
                } else {
                    info->core_percents[core_idx] = 0;
                }
                
                list->core_old_totals[core_idx] = total;
                list->core_old_idles[core_idx] = idle_total;
                
                core_idx++;
            }
        }
        info->core_count = core_idx;
        fclose(fstat);
    }

    // Temperature sensors - different systems have different paths :/
    long temp_mc = 0;
    int found_temp = 0;
    
    const char *priority_names[] = {"x86_pkg_temp", "TCPU", "INT3400 Thermal", "acpitz", "SEN1", NULL};
    
    for (int p = 0; priority_names[p] != NULL && !found_temp; p++) {
        for (int i = 0; i < 10; i++) {
            char type_path[64], type_buf[64];
            snprintf(type_path, sizeof(type_path), "/sys/class/thermal/thermal_zone%d/type", i);
            FILE *f_type = fopen(type_path, "r");
            if (f_type) {
                if (fgets(type_buf, sizeof(type_buf), f_type)) {
                    type_buf[strcspn(type_buf, "\n")] = 0;
                    if (strcasecmp(type_buf, priority_names[p]) == 0) {
                        char temp_path[64];
                        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/thermal_zone%d/temp", i);
                        FILE *f_temp = fopen(temp_path, "r");
                        if (f_temp) {
                            if (fscanf(f_temp, "%ld", &temp_mc) == 1) {
                                info->cpu_temp = temp_mc / 1000.0;
                                found_temp = 1;
                            }
                            fclose(f_temp);
                        }
                    }
                }
                fclose(f_type);
            }
            if (found_temp) break;
        }
    }
    
    if (!found_temp) {
         // fallback to zone0
         FILE *ft = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
         if (ft) {
             if (fscanf(ft, "%ld", &temp_mc) == 1) info->cpu_temp = temp_mc / 1000.0;
             fclose(ft);
         }
    }
    
    // Battery temp (if exists)
    long bat_mc = 0;
    FILE *fb = fopen("/sys/class/power_supply/BAT0/temp", "r");
    if (!fb) fb = fopen("/sys/class/power_supply/BAT1/temp", "r");
    if (fb) {
         if (fscanf(fb, "%ld", &bat_mc) == 1) {
             if (bat_mc > 1000) info->bat_temp = bat_mc / 1000.0;
             else info->bat_temp = bat_mc / 10.0;
         }
         fclose(fb);
    }
}

// parse /proc/[pid]/stat - this format is annoying because of the (comm) field
static void get_process_stats(const char *pid_str, ProcessInfo *proc) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/stat", pid_str);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char buffer[2048];
    if (fgets(buffer, sizeof(buffer), f)) {
        char *open_paren = strchr(buffer, '(');
        char *close_paren = strrchr(buffer, ')');
        if (open_paren && close_paren && close_paren > open_paren) {
             size_t len = close_paren - open_paren - 1;
             if (len > sizeof(proc->name) - 1) len = sizeof(proc->name) - 1;
             
             strncpy(proc->name, open_paren + 1, len);
             proc->name[len] = '\0';
             
             strncpy(proc->command, proc->name, sizeof(proc->command)-1);
             
             char *rest = close_paren + 2;
             proc->state = *rest;
             
             unsigned long long utime = 0, stime = 0;
             int ppid = 0, priority = 0, nice = 0, threads = 1;
             
             // parse the fields after state
             int field = 0;
             char *token = strtok(rest, " ");
             while (token) {
                 if (field == 1) ppid = atoi(token);
                 if (field == 11) utime = strtoull(token, NULL, 10);
                 if (field == 12) stime = strtoull(token, NULL, 10);
                 if (field == 15) priority = atoi(token);
                 if (field == 16) nice = atoi(token);
                 if (field == 17) threads = atoi(token);
                 
                 if (field > 17) break;
                 token = strtok(NULL, " ");
                 field++;
             }
             proc->utime = utime;
             proc->stime = stime;
             proc->ppid = ppid;
             proc->priority = priority;
             proc->nice = nice;
             proc->threads = threads;
             
             // map state char to readable name
             switch (proc->state) {
                 case 'R': strcpy(proc->status_name, "Running"); break;
                 case 'S': strcpy(proc->status_name, "Sleeping"); break;
                 case 'D': strcpy(proc->status_name, "Disk Sleep"); break;
                 case 'Z': strcpy(proc->status_name, "Zombie"); break;
                 case 'T': strcpy(proc->status_name, "Stopped"); break;
                 case 't': strcpy(proc->status_name, "Tracing"); break;
                 case 'X': strcpy(proc->status_name, "Dead"); break;
                 case 'x': strcpy(proc->status_name, "Dead"); break;
                 case 'K': strcpy(proc->status_name, "Wakekill"); break;
                 case 'W': strcpy(proc->status_name, "Waking"); break;
                 case 'P': strcpy(proc->status_name, "Parked"); break;
                 case 'I': strcpy(proc->status_name, "Idle"); break;
                 default:  snprintf(proc->status_name, 16, "Unknown(%c)", proc->state); break;
             }
        }
    }
    fclose(f);
}

static uid_t get_uid(const char *pid_str) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/status", pid_str);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[256];
    uid_t uid = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            sscanf(line, "Uid: %u", &uid);
            break;
        }
    }
    fclose(f);
    return uid;
}

static void get_cmdline(const char *pid_str, char *buffer, size_t size) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid_str);
    FILE *f = fopen(path, "r");
    if (!f) return;

    size_t len = fread(buffer, 1, size - 1, f);
    if (len > 0) {
        buffer[len] = '\0';
        // cmdline args are null-separated, replace with spaces
        for (size_t i = 0; i < len - 1; i++) {
            if (buffer[i] == '\0') buffer[i] = ' ';
        }
    }
    fclose(f);
}

void refresh_process_list(ProcessList *list, ProcessList *prev_list) {
    unsigned long long current_total_cpu = 0, current_idle_cpu = 0;
    
    get_system_cpu_times(&current_total_cpu, &current_idle_cpu);
    
    unsigned long long total_diff = 0;
    
    if (prev_list && prev_list->total_cpu_time > 0) {
        if (current_total_cpu >= prev_list->total_cpu_time) {
            total_diff = current_total_cpu - prev_list->total_cpu_time;
        }
    }
    list->total_cpu_time = current_total_cpu;
    list->total_cpu_idle = current_idle_cpu;

    DIR *proc = opendir("/proc");
    if (!proc) return;

    list->count = 0;

    struct dirent *entry;
    while ((entry = readdir(proc))) {
        if (!is_numeric(entry->d_name)) continue; // skip non-PID entries

        // resize array if needed
        if (list->count >= list->capacity) {
            list->capacity *= 2;
            ProcessInfo *new_ptr = realloc(list->processes, sizeof(ProcessInfo) * list->capacity);
            if (!new_ptr) break; // out of memory, stop here
            list->processes = new_ptr;
        }

        ProcessInfo *p = &list->processes[list->count];
        p->pid = atoi(entry->d_name);
        
        // initialize
        p->uid = 0;
        p->state = '?';
        p->memory_sq = 0;
        p->cpu_usage = 0.0f;
        p->utime = 0;
        p->stime = 0;
        memset(p->user, 0, sizeof(p->user));
        memset(p->command, 0, sizeof(p->command));

        p->uid = get_uid(entry->d_name);
        get_user_name(p->uid, p->user, sizeof(p->user));
        p->memory_sq = get_memory_usage(entry->d_name);
        
        get_process_stats(entry->d_name, p);
        
        // CPU usage calculation - compare with previous snapshot
        if (prev_list && total_diff > 0) {
            // find this PID in prev_list (yeah this is O(n^2) but whatever)
            for (int k = 0; k < prev_list->count; k++) {
                if (prev_list->processes[k].pid == p->pid) {
                    unsigned long long prev_process_time = prev_list->processes[k].utime + prev_list->processes[k].stime;
                    unsigned long long curr_process_time = p->utime + p->stime;
                    
                    if (curr_process_time >= prev_process_time) {
                        unsigned long long proc_diff = curr_process_time - prev_process_time;
                        
                        // formula: (process_delta / system_delta) * 100 * num_cores
                        p->cpu_usage = 100.0f * ((float)proc_diff / (float)total_diff);
                        
                        int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
                        p->cpu_usage *= num_cores;
                    }
                    break;
                }
            }
        }
        
        // get full command line
        char cmdline[MAX_CMD_LEN];
        cmdline[0] = '\0';
        get_cmdline(entry->d_name, cmdline, sizeof(cmdline));
        if (strlen(cmdline) > 0) {
             strncpy(p->command, cmdline, sizeof(p->command) - 1);
             p->command[sizeof(p->command) - 1] = '\0';
        }

        // apply filter if active
        if (list->filter[0] != '\0') {
            char pid_str[32];
            snprintf(pid_str, 32, "%d", p->pid);
            
            if (!strcasestr(p->command, list->filter) && 
                !strcasestr(p->user, list->filter) &&
                !strstr(pid_str, list->filter)) {
                continue; // doesn't match, skip it
            }
        }

        list->count++;
    }

    closedir(proc);
    sort_process_list(list);
}
