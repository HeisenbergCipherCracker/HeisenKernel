
/**
 * MIT License
 *
 * Copyright (c) 2024 Heisenberg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * @author Heisenberg
 * @file sched.h

 */
#ifndef SCHED_H
#define SCHED_H

#include <proc/proc.h>

process_t *get_cur_proc();
process_t *get_proc_by_id(int id);
uint32_t schedule(uint32_t esp);
void sched_add_proc(process_t *proc);
void sched_remove_proc(int id);
void sched_init();
int get_nproc();
void print_procs();

#endif