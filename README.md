# Mozart - terminal music player

A minimal and suckless TUI music player

## Dependencies

- libVLC
- ncursesw
- C++20 compiler

## Installation

    sudo make install

Alternatively if you want to just try mozart then run the following command :
    
    make && ./mozart

## Usage


    mozart [directories...]

## Controls

### Normal mode

| Key | Action |
|-----|--------|
| `Space` | Play / Pause |
| `j` / `k` | Navigate playlist |
| `↑` / `↓` | Navigate playlist |
| `n` / `p` | Next / Previous track |
| `Enter` | Play selected track |
| `s` | Toggle shuffle |
| `r` | Cycle repeat (off → one → all) |
| `+` / `-` | Volume up / down |
| `*` or `f` | Toggle star on selected |
| `Tab` | Toggle filter (all / starred only) |
| `a` | Add directory (recursive scan) |
| `d` | Remove selected from library |
| `←` / `→` | Seek back / forward 5s |
| `Esc` | Clear status / cancel input |
| `q` or `Ctrl-C` | Quit |

Library and stars are persisted to `~/.config/mozart/`.
