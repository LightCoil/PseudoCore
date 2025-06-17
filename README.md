# PseudoCore - High-Performance Data Management Prototype


## Overview

PseudoCore is a high-performance data management system designed to optimize data access, caching, and task scheduling at the block level. It aims to emulate core-level operations with a focus on multi-threading, data compression, and predictive data fetching. This system can be used in scenarios requiring efficient data handling, such as storage systems, virtualization, or I/O optimization.

### Key Features
- **Multi-Core Processing**: Utilizes multiple threads to simulate core operations with task migration and load balancing.
- **Advanced Caching**: Implements a multi-level caching mechanism with LRU eviction and dirty page handling.
- **Data Compression**: Uses Zstandard (Zstd) for adaptive data compression to reduce storage and transfer overhead.
- **Predictive Fetching**: Incorporates anticipatory algorithms to prefetch data based on access patterns.
- **Task Scheduling**: Manages workload distribution across cores with a focus on "hot" data blocks.

## Disclaimer
This is a **prototype** implementation. While significant efforts have been made to improve performance and reliability, there are still unresolved issues, including:
- Potential race conditions in high-concurrency scenarios despite mutex usage.
- Incomplete error handling in some edge cases.
- Hard-coded parameters that may not be optimal for all use cases.
- Limited testing under diverse workloads.


## Installation

### Prerequisites
- **Operating System**: Linux or Unix-like systems (Windows compatibility not guaranteed).
- **Compiler**: GCC or Clang with C11 support.
- **Dependencies**: 
  - `libzstd` for compression (install via package manager, e.g., `sudo apt install libzstd-dev` on Ubuntu).
  - POSIX threads (`pthread`) support.

### Build Instructions
1. Clone the repository:
   ```bash
   git clone https://github.com/LightCoil/pseudocore.git
   cd pseudocore
   ```
2. Build the project using the provided Makefile:
   ```bash
   make
   ```
3. If successful, the executable `pseudo_core` will be created in the project directory.

### Configuration
- Edit `config.h` to adjust parameters like `CORES`, `SEGMENT_MB`, `BLOCK_SIZE`, or compression levels if needed.
- Ensure the swap image file path (`SWAP_IMG_PATH`) in `config.h` points to a valid location with read/write permissions.

## Usage

### Running the Application
1. Ensure the swap image file (`storage_swap.img`) exists or create one:
   ```bash
   dd if=/dev/zero of=storage_swap.img bs=1M count=1024
   ```
   This creates a 1GB swap file. Adjust `count` based on your needs and `SEGMENT_MB` settings.
2. Run the PseudoCore executable:
   ```bash
   ./pseudo_core
   ```
or
 ```bash
   ./pseudo_core_daemon
   ```
3. The system will start multiple threads simulating core operations, performing data access, caching, compression, and scheduling.

### Monitoring
- Logs are output to `stderr` with timestamps and core-specific information for debugging and performance analysis.
- Use tools like `htop` or `top` to monitor CPU and memory usage across cores.

### Stopping the Application
- Press `Ctrl+C` to send a termination signal (`SIGINT`). The application handles this gracefully by shutting down threads and cleaning up resources.
- For daemon
-  ```bash
   sudo kill $(cat /var/run/pseudo_core.pid)
   ```

## Project Structure
- **anticipator.h**: Predictive data fetching logic.
- **block_priority.h**: Hot block detection and prioritization.
- **cache.h / cache.c**: Multi-level caching with LRU eviction.
- **compress.h / compress.c**: Data compression using Zstd.
- **pseudo_core.c**: Main application logic for core simulation.
- **ring_cache.h / ring_cache.c**: Circular buffer for data logging.
- **scheduler.h / scheduler.c**: Task scheduling and load balancing across cores.
- **config.h**: Configuration parameters for tuning system behavior.

## Known Issues
- **Memory Management**: Cache size is not dynamically limited, which may lead to excessive memory usage.
- **Synchronization**: While mutexes are used, some edge cases may still result in race conditions under extreme loads.
- **Compression Levels**: Adaptive compression is basic and may not always choose optimal levels.
- **Testing**: Limited testing on diverse hardware and workloads; performance may vary.

## Contributing
We welcome contributions to improve PseudoCore! Please follow these steps:
1. Fork the repository.
2. Create a branch for your feature or bugfix (`git checkout -b feature-name`).
3. Commit your changes with descriptive messages (`git commit -m "Description of change"`).
4. Push to your branch (`git push origin feature-name`).
5. Open a Pull Request with a detailed description of your changes.

### Development Guidelines
- Follow clean architecture principles and modular design.
- Ensure thread safety in all multi-threaded operations.
- Add logging for significant events or errors.
- Write unit tests for new functionality when possible.

## License
This project is licensed under the GNU 3 License - see the [LICENSE](LICENSE) file for details.


