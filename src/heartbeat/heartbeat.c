//
// Created by LAV on 23.10.2026.
//


#include "dbg.h"
#include "utils.h"
#include "heartbeat.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "log.h"

enum
{
    DELAY_FLASH = 150,
    DELAY_BETWEEN = 150,
    DELAY_PAUSE = 1000 - DELAY_FLASH - DELAY_BETWEEN,
};

static heartbeat_t m_heartbeat = {
        .gpio_base = -1,
        .gpio_pin = -1,
};

//---------------------------------------------------------
static void heartbeat_clear(heartbeat_t *heartbeat)
{
    heartbeat->gpio_base = -1;
    heartbeat->gpio_pin = -1;
}

//---------------------------------------------------------
static int looking_for_gpio_base()
{
    struct dirent *entry;
    char path[1024];
    char label[64];
    int base = -1;
    const char *gpio_system_name = "/sys/class/gpio";
    DIR *dir = opendir(gpio_system_name);
    if (!dir) {
        DBGF(0, "ERROR: Can't open %s\n", gpio_system_name);
        LOG(ERROR, "Can't open %s\n", gpio_system_name);
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "gpiochip", 8) == 0) {
            snprintf(path, sizeof(path), "%s/%s/label", gpio_system_name, entry->d_name);
            FILE *fd = fopen(path, "r");
            if (fd) {
                if (fscanf(fd, "%63s", label) == 1) {
                    if (strcmp(label, "zynq_gpio") == 0) {
                        fclose(fd);
                        snprintf(path, sizeof(path), "%s/%s/base", gpio_system_name, entry->d_name);
                        fd = fopen(path, "r");
                        if (fd) {
                            // Читаем первую строку файла
                            char buf[64];
                            if (fgets(buf, sizeof(buf), fd) != NULL) {
                                char *endptr = {};
                                errno = 0;
                                const long val = strtol(buf, &endptr, 10);
                                if (endptr != buf && errno != ERANGE && val >= INT_MIN && val <= INT_MAX) {
                                    base = (int) val;
                                } else {
                                    base = -1;
                                    DBGF(0, "ERROR: Can't get gpio base address from %s\n", path);
                                    LOG(ERROR, "Can't get gpio base address from %s\n", path);
                                }
                            }
                            fclose(fd);
                            break;
                        }
                    }
                }
                fclose(fd);
            }
        }
    }
    closedir(dir);
    return base;
}

//---------------------------------------------------------
static bool export_gpio_led(heartbeat_t *heartbeat, const GPIO_EXPORT mode)
{
    if (heartbeat->gpio_base == -1) {
        heartbeat->gpio_base = looking_for_gpio_base();
        if (heartbeat->gpio_base == -1) {
            return FAIL;
        }
        heartbeat->gpio_pin = heartbeat->gpio_base + HEARTBEAT_PIN;
    }
    if (mode == EXPORT) {
        DBGF(2, "[tid:%08lX] --- gpio base pin number: %d, pin-%d, gpio: %d", pthread_self(), heartbeat->gpio_base,
             HEARTBEAT_PIN, heartbeat->gpio_pin);
    }

    char gpio_pin_name[32];
    char gpio_pin_fname[32];
    char gpio_pin_direction_fname[64];
    snprintf(gpio_pin_name, sizeof(gpio_pin_name), "%d", heartbeat->gpio_pin);
    snprintf(gpio_pin_fname, sizeof(gpio_pin_fname), "/sys/class/gpio/gpio%d", heartbeat->gpio_pin);
    snprintf(gpio_pin_direction_fname, sizeof(gpio_pin_direction_fname), "/sys/class/gpio/gpio%d/direction",
             heartbeat->gpio_pin);
    snprintf(heartbeat->gpio_pin_value_fname, sizeof(heartbeat->gpio_pin_value_fname), "/sys/class/gpio/gpio%d/value",
             heartbeat->gpio_pin);

    if (mode == EXPORT) {
        struct stat st;
        if (stat(gpio_pin_fname, &st) != 0) {
            const char *filename = "/sys/class/gpio/export";
            const int fd = open(filename, O_WRONLY);
            if (fd < 0) {
                DBGF(0, "ERROR: can't open gpio subsystem (%s)\n", filename);
                LOG(ERROR, "can't open gpio subsystem (%s)\n", filename);
                return FAIL;
            }
            write(fd, gpio_pin_name, strlen(gpio_pin_name));
            close(fd);
        }
        if (stat(gpio_pin_direction_fname, &st) != 0) {
            DBGF(0, "ERROR: gpio pin-%d filename not found (%s)\n", HEARTBEAT_PIN, gpio_pin_direction_fname);
            LOG(ERROR, "gpio pin-%d filename not found (%s)\n", heartbeat->gpio_pin, gpio_pin_direction_fname);
            return FAIL;
        }

        // set direction
        const int fd = open(gpio_pin_direction_fname, O_RDWR);
        if (fd < 0) {
            DBG(0, "ERROR: can't open gpio pin direction\n");
            LOG(ERROR, "can't open gpio pin direction\n");
            return FAIL;
        }

        char buf[32] = {};
        if (read(fd, buf, sizeof(buf)) == -1) {
            DBG(0, "ERROR: can't read current direction status\n");
            LOG(ERROR, "can't read current direction status\n");
            close(fd);
            return FAIL;
        }

        char *p = strchr(buf, '\r');
        if (p) *p = '\0';
        p = strchr(buf, '\n');
        if (p) *p = '\0';

        if (strcasecmp(buf, "out") != 0) {
            write(fd, "out", 3);
        }
        close(fd);
        return OK;
    }

    if (mode == UNEXPORT) {
        int fd = open(heartbeat->gpio_pin_value_fname, O_RDWR);
        if (fd != -1) {
            write(fd, "0\n", 2);
            close(fd);
        }

        const char *filename = "/sys/class/gpio/unexport";
        fd = open(filename, O_WRONLY);
        if (fd == -1) {
            DBGF(0, "ERROR: can't open gpio subsystem (%s)\n", filename);
            LOG(ERROR, "can't open gpio subsystem (%s)\n", filename);
            return FAIL;
        }
        const int ret = write(fd, gpio_pin_name, strlen(gpio_pin_name));
        if (ret == 1) {
            DBG(0, "ERROR: can't close gpio subsystem correct\n");
        } else {
            DBG(3, "gpio subsystem closed");
        }
        close(fd);
        return OK;
    }

    DBGF(0, "ERROR: invalid exporting mode %d\n", (int)mode);
    LOG(ERROR, "invalid exporting mode %d\n", (int)mode);
    return FAIL;
}

//---------------------------------------------------------
static void heartbeat_led_toggle(const heartbeat_t *heartbeat)
{
    static int failed = 0;
    DBGF(5, "[tid:%08lX] --- pin value file: '%s' ", pthread_self(), heartbeat->gpio_pin_value_fname);
    const int fd = open(heartbeat->gpio_pin_value_fname, O_RDWR);
    if (fd == -1) {
        DBGF(1, "ERROR: can't open gpio subsystem %s - %s\n", heartbeat->gpio_pin_value_fname, strerror(errno));
        if (!failed) {
            LOG(ERROR, "can't open gpio subsystem %s - %s\n", heartbeat->gpio_pin_value_fname, strerror(errno));
            failed = 1;
        }
        return;
    }
    DBGF(5, "[tid:%08lX] --- pin value fd: %d", pthread_self(), fd);

    char buf[8] = {0};
    if (read(fd, buf, sizeof(buf)) == -1) {
        DBGF(1, "ERROR: can't read pin-%d status - %s\n", HEARTBEAT_PIN, strerror(errno));
        if (!failed) {
            LOG(ERROR, "can't read pin-%d status - %s\n", HEARTBEAT_PIN, strerror(errno));
            failed = 1;
        }
        return;
    }

    char *p = strpbrk(buf, "\r\n");
    if (p) *p = '\0';

    int ret;
    if (strncmp(buf, "1", 1) == 0) {
        ret = write(fd, "0\n", 2);
        DBGF(5, "[tid:%08lX] --- pin: %s -> write '0' to fd: %d", pthread_self(), buf, fd);
    } else {
        ret = write(fd, "1\n", 2);
        DBGF(5, "[tid:%08lX] --- pin: %s -> write '1' to fd: %d", pthread_self(), buf, fd);
    }
    if (ret == -1) {
        DBGF(1, "write error %d - %s\n", ret, strerror(ret));
        if (!failed) {
            LOG(ERROR, "write error %d - %s\n", ret, strerror(ret));
            failed = 1;
        }
    }
    close(fd);
    DBGNL(5);
}

//---------------------------------------------------------
int heartbeat_thread_start(thr_t *thr)
{
    DBGF(3, "[tid:%08lX] --- %s (%s) going to be started...",
         pthread_self(), thr->name, thr_type_to_string(thr->type));

    heartbeat_t *heartbeat = thr->payload;
    struct timespec next_beat;

    if (!export_gpio_led(heartbeat, EXPORT)) {
        export_gpio_led(heartbeat, UNEXPORT);
        return 1;
    }

    long cnt = 0;
    struct timespec ts, ts1, ts2;
    clock_gettime(CLOCK_MONOTONIC, &next_beat);
    clock_gettime(CLOCK_REALTIME, &ts1);

    do {
        heartbeat_led_toggle(heartbeat);
        if (thr_sleep_absolute(thr, &next_beat, DELAY_FLASH)) break;

        heartbeat_led_toggle(heartbeat);
        if (thr_sleep_absolute(thr, &next_beat, DELAY_BETWEEN)) break;

        heartbeat_led_toggle(heartbeat);
        if (thr_sleep_absolute(thr, &next_beat, DELAY_FLASH)) break;

        heartbeat_led_toggle(heartbeat);
        if (thr_sleep_absolute(thr, &next_beat, DELAY_PAUSE)) break;

        ++cnt;
    } while (!thr->shuttingdown);

    clock_gettime(CLOCK_REALTIME, &ts2);

    export_gpio_led(heartbeat, UNEXPORT);

    char buf[256] = {0};
    DBGF(3, "[tid:%08lX] --- finished %s %s:%d",
         pthread_self(), thr->name, basename(__FILE__), __LINE__);

    timespec_diff(&ts, &ts1, &ts2, NULL);
    DBGF(3, "cnt: %ld, ts: %s (%ld.%ld)", cnt, timespec_to_str_03(&ts, buf),
         ts.tv_sec / cnt, ts.tv_nsec / cnt);

    return 0;
}

//---------------------------------------------------------
int heartbeat_thread_stop(thr_t *thr)
{
    DBGF(3, "[tid:%08lX] %s going to stop", pthread_self(), thr->name);
    heartbeat_t *heartbeat = thr->payload;
    export_gpio_led(heartbeat, UNEXPORT);
    heartbeat_clear(heartbeat);
    return 0;
}

//---------------------------------------------------------
bool heartbeat_run()
{
    DBG(1);
    THR_START(thr, THR_DETACH, heartbeat_thread_start, heartbeat_thread_stop, &m_heartbeat, false, "Heartbeat");
    if (!thr) {
        return FAIL;
    }
    thr->type = THR_TYPE_HEARTBEAT;
    DBG_NSLEEP(TS_100MS);
    return OK;
}
