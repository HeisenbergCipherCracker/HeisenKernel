#include <hal/hal.h>
#include <proc/sched.h>

uint8_t sched_on = 0;
uint8_t pit_ticks;

extern void pit_int();

void sched_state(int on) {
    sched_on = on;
}

int get_sched_state() {
    return sched_on;
}

void pit_send_command(uint8_t cmd) {
    outportb(PIT_REG_COMMAND, cmd);
}

void pit_send_data(uint16_t data, uint8_t counter) {
    if(counter == PIT_COUNTER_0)
        outportb(PIT_REG_COUNTER0, data);
    else if(counter == PIT_COUNTER_1)
        outportb(PIT_REG_COUNTER1, data);
    else if(counter == PIT_COUNTER_2)
        outportb(PIT_REG_COUNTER2, data);
}

uint8_t pit_read_data(uint8_t counter) {
    if(counter == PIT_COUNTER_0)
        return inportb(PIT_REG_COUNTER0);
    else if(counter == PIT_COUNTER_1)
        return inportb(PIT_REG_COUNTER1);
    else if(counter == PIT_COUNTER_2)
        return inportb(PIT_REG_COUNTER2);
    else
        return NULL;
}

void pit_init() {
    install_ir(32, 0x80 | 0x0E, 0x8, &pit_int);
}

void pit_start_counter(uint32_t frequency, uint8_t counter, uint8_t mode) {
    if(frequency == 0)
        return;
    
    uint16_t divisor = (uint16_t) 1193180 / (uint16_t) frequency;
    
    uint8_t ocw = 0;
    ocw = (ocw & ~PIT_MODE_MASK) | mode;
    ocw = (ocw & ~PIT_RL_MASK) | PIT_RL_DATA;
    ocw = (ocw & ~PIT_COUNTER_MASK) | counter;
    pit_send_command(ocw);
    pit_send_data(divisor & 0xFF, PIT_COUNTER_0);
    pit_send_data((divisor >> 8) & 0xFF, PIT_COUNTER_0);
    
    pit_ticks = 0;
}

int get_tick_count() {
    return pit_ticks;
}

void reset_tick_count() {
    pit_ticks = 0;
}