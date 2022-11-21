#!/bin/sh

export CFLAGS="-O0 -g -fsanitize=address,undefined -Wall -Wpedantic -Wextra -Werror -std=c99"
make mines
