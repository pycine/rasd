# rasd — رصد

> A lightweight network usage monitor written in C.  
> Records bandwidth silently in the background. Query it whenever you want.

---

## Features

- **Live recording** — reads `/proc/net/dev` directly, no Python runtime, no bloat
- **SQLite backend** — WAL mode, batched writes, persistent across reboots
- **Multiple views** — bar graph, compact, sparkline
- **Flexible queries** — today, yesterday, this week, specific date, since a date, by month
- **JSON output** — pipe-friendly for scripting
- **Systemd integration** — runs as a service, starts on boot
- **~472KB RAM** — because it's C

---

## Dependencies

```bash
# Debian/Ubuntu/Kali
sudo apt install libsqlite3-dev

# Arch
sudo pacman -S sqlite
```

---

## Build & Install

```bash
git clone https://github.com/pycine/rasd
cd rasd
make
sudo make install           # installs to /usr/local/bin/rasd
sudo make install-service   # installs and registers systemd unit
```

Enable on boot:

```bash
sudo systemctl enable --now rasd
systemctl status rasd
```

---

## Usage

### Recording

```bash
rasd record                  # record with default 3s interval
rasd record -w 5             # 5 second interval
rasd record -w 1 -v          # 1s interval, verbose output
rasd record -d               # dry run (no DB writes)
rasd record --json           # live JSON output
```

> When running as a systemd service, recording starts automatically at boot.

### Querying

```bash
rasd                         # today's usage (default)
rasd --today                 # today
rasd --yesterday             # yesterday
rasd --thisweek              # this week
rasd --month                 # current month
rasd --month 3               # march
rasd --date 2026-04-20       # specific date
rasd --since 2026-01-01      # from date until now
```

### Display options

```bash
rasd --today                         # bar graph (default)
rasd --today --style compact         # compact single-line view
rasd --today --style spark           # sparkline view
rasd --today -g                      # show in GB instead of MB
rasd --thisweek --json               # JSON output
```

### Database

```bash
rasd db --stats              # record count, size, oldest/newest record
rasd db --stats --json       # same as JSON
rasd db --prune 30           # delete records older than 30 days
```

---

## Example Output

**Bar style:**
```
  Today Usage
  ------------
  █ Upload  █ Download

00:00     ██░░░░░░░░░░░░░░   0.12↑   1.43↓ MB
01:00                         0.00↑   0.00↓ MB
...

Total: 1.24↑  18.67↓ MB
```

**Spark style:**
```
  Today Usage
  ────────────
  ↑ ▁▁▁▂▂▄▄▇▇█▇▅▃▂▁▁  peak 14:00 4.21 MB
  ↓ ▁▁▂▃▄▆▇██▇▆▄▃▂▁▁  peak 13:00 9.87 MB
    0  2  4  6  8  10 12 14 16

  Total: 12.43↑  67.21↓ MB
```

---

## Data Location

```
~/.rasd/db.sqlite
```

---

## Project Structure

```
rasd/
├── src/
│   ├── main.c       # argument parsing, dispatch
│   ├── record.c/h   # /proc/net/dev sampler, recording loop
│   ├── db.c/h       # SQLite layer
│   ├── fetch.c/h    # time range queries, bucketing
│   └── display.c/h  # bar / compact / spark renderers
├── Makefile
└── rasd.service     # systemd unit
```

---

## Name

**rasd** — from Arabic رصد, meaning *surveillance* or *observation*.  
A quiet daemon that watches your network so you don't have to.

---


