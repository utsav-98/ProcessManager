# process manager

a simple terminal process manager for linux, kinda like htop but simpler

#

## what it does

-  shows running processes with CPU and memory usage
-  real-time updates
-  multiple color themes (press 't' to cycle through them)
-  search/filter processes
-  popup confirmation for killing processes
-  toggle memory format (KB/MB)
-  built-in help menu
-  vim-style navigation (j/k)
-  mouse scrolling support

## building

you need ncurses installed:

```bash
# debian/ubuntu
sudo apt-get install libncurses5-dev

# arch
sudo pacman -S ncurses

# fedora
sudo dnf install ncurses-devel
```

then just:

```bash
make
./process_manager
```

## controls

| key           | what it does                          |
| ------------- | ------------------------------------- |
| q             | quit                                  |
| j/k or arrows | move up/down                          |
| gg            | jump to top (vim style)               |
| G             | jump to bottom                        |
| m             | sort by memory                        |
| c             | sort by CPU                           |
| p             | sort by PID                           |
| t             | change theme                          |
| /             | search/filter                         |
| ESC           | clear filter                          |
| Enter         | show/hide process details             |
| 1             | toggle per-core CPU view              |
| M             | toggle memory format (KB/MB)          |
| H             | open/hide help menu                   |
| K             | kill process (sends SIGKILL with popup)|
| h / l         | select yes/no in kill popup           |

## notes

-  you might need sudo to kill processes owned by other users
-  works on linux only (uses /proc filesystem)
-  tested on ubuntu and arch

## themes

there's like 10 different color themes (most of them suck, but Nord and Catppuccin are fixed now) :

-  default (cyan)
-  dracula
-  matrix (green, my fav)
-  solarized
-  monokai
-  gruvbox
-  nord
-  catppuccin
-  tokyo night
-  everforest

## todo

-  [ ] Add network status?
-  [ ] tree view for parent/child processes
-  [x] Toggle Memory format <MB || KB>
-  [x] conformation to kill specific process
-  [ ] Signal selction

## license

MIT or GPL or whatever, do what you want with it
