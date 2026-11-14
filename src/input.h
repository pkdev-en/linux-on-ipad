#ifndef LX_INPUT_H
#define LX_INPUT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* Same fields as Linux's struct input_event (minus the timestamp), so a future
 * virtio-input device can hand events to the guest untouched. */
#define LX_EV_SYN 0
#define LX_EV_KEY 1
#define LX_EV_ABS 3
#define LX_ABS_X 0
#define LX_ABS_Y 1
#define LX_BTN_LEFT 0x110

typedef struct LxInputEvent {
    uint16_t type, code;
    int32_t value;
} LxInputEvent;

#define LX_INPUT_QUEUE 256 /* must be a power of two */

/* Lock-free single-producer (UI thread) / single-consumer (VM thread) ring. */
typedef struct LxInput {
    LxInputEvent ev[LX_INPUT_QUEUE];
    _Atomic uint32_t head; /* next slot to write */
    _Atomic uint32_t tail; /* next slot to read  */
} LxInput;

void lx_input_init(LxInput *q);
bool lx_input_push(LxInput *q, uint16_t type, uint16_t code, int32_t value); /* false if full */
bool lx_input_pop(LxInput *q, LxInputEvent *out);                            /* false if empty */
void lx_input_key(LxInput *q, uint16_t evdev_code, bool down);               /* KEY + SYN */
void lx_input_pointer(LxInput *q, int32_t x, int32_t y, bool left_down);     /* ABS_X/Y, BTN_LEFT, SYN */

#endif
