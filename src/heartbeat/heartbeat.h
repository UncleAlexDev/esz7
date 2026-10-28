//
// Created by LAV on 23.10.2026.
//

#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "thr_t.h"

#include <stdbool.h>

typedef enum {
    NONE,
    EXPORT,
    UNEXPORT,
} GPIO_EXPORT;

typedef struct heartbeat_t
{
    pthread_mutex_t mutex;
    int gpio_base;
    int gpio_pin;
    char gpio_pin_value_fname[64];
} heartbeat_t;

#define HEARTBEAT_PIN 13

bool heartbeat_run();
int heartbeat_thread_start(thr_t *thr);
int heartbeat_thread_stop(thr_t *thr);

#endif // HEARTBEAT_H
