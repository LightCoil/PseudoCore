#!/bin/bash
. ./config.cfg

LOG=/dev/shm/pseudo_core.log

# 1) Swap
if swapon --show | grep -q "$SWAP_IMG_PATH"; then
  swapoff "$SWAP_IMG_PATH"
  fi
  swapon "$SWAP_IMG_PATH" || { echo "[!] swap on failed"; exit 1; }

  # 2) Лог
  rm -f "$LOG"; touch "$LOG"

  echo "[*] Compiling modules with LFS support..."
  gcc -O3 -D_FILE_OFFSET_BITS=64 -pthread \
      pseudo_core.c \
          cache.c \
              compress.c \
                  ring_cache.c \
                      scheduler.c \
                          -o pseudo_core -lzstd

                          if [[ $? -ne 0 ]]; then
                              echo "[!] Compilation failed"
                                  exit 1
                                  fi
                                  

                                  # 4) Запуск
                                  echo "[*] Starting pseudo cores..."
                                  ./pseudo_core >> "$LOG" 2>&1 &

                                  echo "[✓] Pseudo cores running, log: $LOG"
                                  
