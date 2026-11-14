#include "../src/input.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static LxInput q;
#define N 200000

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++)
        while (!lx_input_push(&q, LX_EV_KEY, 1, i)) { /* spin until the consumer makes room */ }
    return NULL;
}

int main(void)
{
    LxInputEvent e;
    lx_input_init(&q);
    CHECK(!lx_input_pop(&q, &e));

    lx_input_key(&q, 30, true);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_KEY && e.code == 30 && e.value == 1);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_SYN);
    CHECK(!lx_input_pop(&q, &e));

    lx_input_pointer(&q, 640, 360, true);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_ABS && e.code == LX_ABS_X && e.value == 640);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_ABS && e.code == LX_ABS_Y && e.value == 360);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_KEY && e.code == LX_BTN_LEFT && e.value == 1);
    CHECK(lx_input_pop(&q, &e) && e.type == LX_EV_SYN);

    for (int i = 0; i < LX_INPUT_QUEUE; i++)
        CHECK(lx_input_push(&q, LX_EV_KEY, 1, i));
    CHECK(!lx_input_push(&q, LX_EV_KEY, 1, 999)); /* full */
    lx_input_pointer(&q, 1, 2, false);            /* a burst that does not fit is dropped whole */
    for (int i = 0; i < LX_INPUT_QUEUE; i++)
        CHECK(lx_input_pop(&q, &e) && e.value == i);
    CHECK(!lx_input_pop(&q, &e));

    pthread_t t;
    pthread_create(&t, NULL, producer, NULL);
    for (int i = 0; i < N;) {
        if (lx_input_pop(&q, &e)) {
            CHECK(e.value == i);
            i++;
        }
    }
    pthread_join(t, NULL);
    puts("input ok");
    return 0;
}
