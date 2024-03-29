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

 */
#ifndef _PIC_H_
#define _PIC_H_

#include <lib/types.h>
#include "irq.h"



#define ICW1_INIT 0x10
#define ICW4_8086 0x01
#define OCW2_EOI 0x20 /*Set the mode of PIC controllers*/
#define OCW3_READ_ISR 0x0b
#define OCW3_READ_IRR 0x0a /*define a value for the  read irr command register*/
#define EOI 0x20 /*End if interrupt command*/


/**
 * @file pic.h Programmable Interruptr Controller
 * @date 2010
 * @license MIT License
 *
 * PIC's functions
 * Only used byinterrupt.c
 *
 * @see Intel 8259A datasheet
 */

/** Setup PIC and Disable all IRQ lines */
void x86_pic_setup(void);

void x86_pic_enable_irq_line(uint32_t numirq);

void x86_pic_disable_irq_line(uint32_t numirq);

#endif
