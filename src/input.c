#include "input.h"

#include <string.h>

void lx_input_init(LxInput *q)
{
    memset(q->ev, 0, sizeof q->ev);
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
}

static uint32_t space(LxInput *q)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    return LX_INPUT_QUEUE - (h - t);
}

bool lx_input_push(LxInput *q, uint16_t type, uint16_t code, int32_t value)
{
    if (!space(q))
        return false;
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    q->ev[h & (LX_INPUT_QUEUE - 1)] = (LxInputEvent){ type, code, value };
    atomic_store_explicit(&q->head, h + 1, memory_order_release);
    return true;
}

bool lx_input_pop(LxInput *q, LxInputEvent *out)
{
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (t == h)
        return false;
    *out = q->ev[t & (LX_INPUT_QUEUE - 1)];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);
    return true;
}

/* A burst is all-or-nothing, so the guest never sees half of a pointer update. */
void lx_input_key(LxInput *q, uint16_t evdev_code, bool down)
{
    if (space(q) < 2)
        return;
    lx_input_push(q, LX_EV_KEY, evdev_code, down ? 1 : 0);
    lx_input_push(q, LX_EV_SYN, 0, 0);
}

void lx_input_pointer(LxInput *q, int32_t x, int32_t y, bool left_down)
{
    if (space(q) < 4)
        return;
    lx_input_push(q, LX_EV_ABS, LX_ABS_X, x);
    lx_input_push(q, LX_EV_ABS, LX_ABS_Y, y);
    lx_input_push(q, LX_EV_KEY, LX_BTN_LEFT, left_down ? 1 : 0);
    lx_input_push(q, LX_EV_SYN, 0, 0);
}
