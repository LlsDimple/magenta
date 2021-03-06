// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Magenta expects some magic in the high bits. Encapsulate that here,
// so that callers do not have to set it manually.
.macro magenta_syscall num
    movabs $(0x00ff00ff00000000 | \num), %rax
    syscall
.endm

// CFI aware push and pop macros.
.macro push_reg reg
    push \reg
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset \reg, 0
.endm
.macro pop_reg reg
    pop \reg
    .cfi_adjust_cfa_offset -8
    .cfi_same_value \reg
.endm
