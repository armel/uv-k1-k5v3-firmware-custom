#!/bin/sh
set -eu

cd "$(dirname "$0")"
cc -std=c99 -Wall -Wextra -Werror -O2 \
    -I.. host_decrs41.c ../decrs41.c -o /tmp/host_decrs41
/tmp/host_decrs41
