// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <sys/types.h>

void uart_init(void);
void uart_init_early(void);

int uart_putc(char c);
int uart_getc(bool wait);

/* panic-time uart accessors, intended to be run with interrupts disabled */
int uart_pputc(char c);
int uart_pgetc(void);

