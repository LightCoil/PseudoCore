# PseudoCore Prototype

**Warning: This is a prototype. Not for production use.**

PseudoCore is a high-performance data management system prototype, designed for research and demonstration purposes. The codebase is structured according to Clean Architecture and SOLID principles, with a focus on modularity, encapsulation, and performance.

## Architecture Overview

- **Application Layer:** CoreManager, TaskScheduler, DataManager
- **Domain Layer:** CoreEntity, TaskEntity, BlockEntity
- **Infrastructure Layer:** CacheEngine, CompressionEngine, StorageEngine

Each component is designed with:
- Strict typing and encapsulation
- Thread safety
- Error handling
- Performance metrics
- Data integrity checks
- Resource management

## Main Components
- `pseudo_core.c` — Main core logic (foreground, high load)
- `pseudo_core_daemon.c` — Daemonized version (background, reduced load)
- `cache.c`, `compress.c`, `ring_cache.c`, `scheduler.c` — Supporting modules

## Build Instructions

```sh
make clean
make
```

This will build two binaries:
- `pseudo_core` — Foreground prototype
- `pseudo_core_daemon` — Daemonized version

## Usage

### Foreground (high load, blocks terminal)
```sh
./pseudo_core
```
- Runs in the foreground
- High CPU and I/O load
- Press `Ctrl+C` to stop

### Daemon (recommended, reduced load)
```sh
sudo ./pseudo_core_daemon
```
- Runs in the background as a daemon
- Uses 2 threads and smaller segments
- Logs to syslog (check with `tail -f /var/log/syslog | grep pseudo_core`)
- PID file: `/var/run/pseudo_core.pid`
- To stop:
  ```sh
  sudo kill $(cat /var/run/pseudo_core.pid)
  ```

## Storage
- Data is stored in `storage_swap.img` in the current directory

## Notes
- This is a research prototype. No guarantees, no warranties.
- Code and configuration are subject to change.
- For any issues, review logs and source code. 