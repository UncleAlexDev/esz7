/**
    \file   main.c
    \author Aleksey Lekhtin
    \date   May 04, 2024
    \brief  Main procedure
*/

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "ads.h"
#include "atlas.h"
#include "cfg.h"
#include "core.h"
#include "dbg.h"
#include "dtr.h"
#include "epl.h"
#include "esz7_dev.h"
#include "heartbeat.h"
#include "log.h"
#include "sig.h"
#include "siu.h"
#include "thr.h"
#include "wsa.h"
#include "utils.h"
#include "aps_wits.h"
#include "wits.h"

enum { PROJECT_SHM_KEY = 0x0a5ebaca };

//---------------------------------------------------------
static int g_shmid = -1;
static void *g_shm_ptr = (void *) -1;

//---------------------------------------------------------
static void cleanup_shared_memory(void)
{
    if (g_shm_ptr != (void *) -1) {
        shmdt(g_shm_ptr);
    }
    if (g_shmid != -1) {
        struct shmid_ds shm_info;
        if (shmctl(g_shmid, IPC_STAT, &shm_info) == 0) {
            if (shm_info.shm_nattch == 0) {
                shmctl(g_shmid, IPC_RMID, NULL);
            }
        }
    }
}

//---------------------------------------------------------
static int ensure_single_instance(void)
{
    // 1. Первая попытка: атомарно создаем сегмент
    int shmid = shmget(PROJECT_SHM_KEY, sizeof(int), IPC_CREAT | IPC_EXCL | 0666);

    if (shmid == -1) {
        if (errno == EEXIST) {
            // Сегмент существует. Открываем его дескриптор ТОЛЬКО для чтения статуса
            int tmp_shmid = shmget(PROJECT_SHM_KEY, sizeof(int), 0666);
            if (tmp_shmid != -1) {
                struct shmid_ds shm_info;
                if (shmctl(tmp_shmid, IPC_STAT, &shm_info) == 0) {
                    if (shm_info.shm_nattch == 0) {
                        // nattch == 0! Прошлый процесс умер (kill -9).
                        // Удаляем зомби-сегмент
                        shmctl(tmp_shmid, IPC_RMID, NULL);

                        // Пробуем создаться заново начисто
                        shmid = shmget(PROJECT_SHM_KEY, sizeof(int), IPC_CREAT | IPC_EXCL | 0666);
                    }
                }
            }
        }
    }

    // Если shmid всё еще -1 — значит, оригинальное приложение РЕАЛЬНО запущено (nattch > 0)
    if (shmid == -1) {
        DBG(0, "ERROR: application is already started");
        exit(EXIT_FAILURE);
    }

    // 2. Только теперь, когда всё чисто, сохраняем ID в глобальную переменную
    g_shmid = shmid;

    // 3. Подключаемся (взводим nattch в 1)
    g_shm_ptr = shmat(g_shmid, NULL, 0);
    if (g_shm_ptr == (void *) -1) {
        DBG(0, "ERROR: shmat() failed");
        // Если привязка упала, зачищаем за собой созданный shmid перед выходом
        shmctl(g_shmid, IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }

    // 4. Регистрируем автозачистку при выходе
    atexit(cleanup_shared_memory);
    return 0;
}

//---------------------------------------------------------
static bool main_get_environment(int ac, char *av[])
{
    cfg_get_app_path(ac, av);
    log_open_file(cfg()->logfname);

    LOG(INFO, "run: %s/%s %s", cfg()->appdir, cfg()->appname, cfg()->args);
    LOG(INFO, "%s", version());
    check_byteorder();

    bool ok = cfg_read();
    if (!ok) {
        return FAIL;
    }
    ok = cfg_parse_args(ac, av);
    if (!ok) {
        return FAIL;
    }
    ok = cfg_read_esz7_status();
    if (!ok) {
        return FAIL;
    }

    cfg_check_defaults();
    cfg_print_configuration();

    LOG(INFO, "mode:  %s", cfg_mode_string(cfg()->mode));
    DBGF(1, "mode:  %s", cfg_mode_string(cfg()->mode));
    DBGF(1, "debug: %d", debug_level);
    return OK;
}

//---------------------------------------------------------
static void main_services_stop()
{
    epl_close();
    thr_stop_all();
    wsa_cleanup();
    ads_packet_free();
    siu_packet_free();
    dtr_packet_free();
    aps_wits_packet_free();
    atlas_stop();
    close_log();
}

//---------------------------------------------------------
static bool main_services_run()
{
    if (cfg()->test_mode > TEST_NONE) {
        return OK;
    }

    if (!heartbeat_run()) {
        return FAIL;
    }

    if (cfg()->mode == MODE_ATLAS) {
        if (!atlas_run()) {
            return FAIL;
        }
        if (!wits_run()) {
            return FAIL;
        }
        return OK;
    }

    if (cfg()->mode == MODE_APS) {
        siu_read_data_status();

        if (!ads_run()) {
            return FAIL;
        }
        if (!siu_run()) {
            return FAIL;
        }
        if (!dtr_run()) {
            return FAIL;
        }
        if (!aps_wits_run()) {
            return FAIL;
        }
        return OK;
    }

    LOG(ERROR, "========== UNKNOWN WORK MODE DETECTED ==========");
    DBG(0, "ERROR: ========== UNKNOWN WORK MODE DETECTED ==========\n");
    return FAIL;
}

//---------------------------------------------------------
/**
 * Start procedure
 * @param ac - number of command line arguments
 * @param av - list of command line arguments
 * @return 0 - no error
 * @return 1 - error
 */
int main(int ac, char *av[])
{
    ensure_single_instance();

    tzset();
    log_set_enabled(true);
#ifdef WIN32
    // SetConsoleOutputCP(CP_UTF8);
    SetConsoleOutputCP(1251);
    setbuf(stdout, 0);
#endif

    parse_help_args(ac, av);

    if (!main_get_environment(ac, av)) {
        return 1;
    }

    if (!epl_checkin()) {
        return 2;
    }

    if (!esz7_open()) {
        LOG(FATAL, "ERROR: Can't start ADC board");
        DBG(0, "ERROR: Can't start ADC board");
        return 3;
    }

    set_sig_action();
    wsa_startup();

    bool ok = main_services_run();
    if (ok) {
        ok = core_cmd_run(); // run command tcp-port
    }
    if (ok) {
        core_run(); // work with ADC of xme0724 board
    }

    //-----------------------------------
    DBG(1, "=== finishing ===");
    siu_save_data_status();
    main_services_stop();
    DBG(1, "===  exiting  ===");
    thr_wait_all_done(2);
    DBG(1, "done\n");
    return 0;
}
