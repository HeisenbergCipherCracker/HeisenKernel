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
#ifndef _PIT_H_
#define _PIT_H_

/**
 * @file pit.h
 * @author Konstantin Tcholokachvili
 * @date 2013, 2014
 * @license MIT License
 *
 * @see [en] i82C54's datasheet
 * @see [en] http://www.osdever.net/bkerndev/Docs/pit.htm
 * @see [en] http://www.jamesmolloy.co.uk/tutorial_html/5.-IRQs and the PIT.html
 *
 * Programmable Interrupt Timer
 */

#include <lib/types.h>

/** 
 * Changes timer interrupt frequency from the default one (18.222 Hz)
 * 
 * @param frequency Frequency at which interrupts whill be raised
 * @return status Status indicating the success of operation
 */
ret_t x86_pit_set_frequency(uint32_t frequency);

/**
* Timer's interrupt handler called periodically
*
* @param id
*/
void timer_interrupt_handler(int number);

#endif // _PIT_H_

