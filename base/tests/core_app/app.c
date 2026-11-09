/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The plat_core test app: see app.h */
static volatile int32_t core_render = -1, core_job = -1, core_frame = -1, job_arg = -1;   /* -1: not asked yet */
static volatile int32_t jobs, renders, cpu2_set_rc = -9;

static void job(uint32_t arg)                      /* on the second core: the render's other half */
{
    core_job = (int32_t)plat_core();
    job_arg = (int32_t)arg;
    jobs++;
}

void app_init(void) { cpu2_set_rc = plat_cpu2_set(1); }
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    if (!plat_cpu2_run(job, n))
        job(n);                                    /* (one core: the job here) */
    core_render = (int32_t)plat_core();
    plat_cpu2_wait();
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    renders++;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void) { core_frame = (int32_t)plat_core(); }
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }
#ifdef PLAT_HOST                                   /* (the simulator's checks: not in the device build) */
int app_expect(const char *what, int *got)
{
    static const struct {
        const char *name;
        volatile int32_t *v;
    } k[] = {{"core_render", &core_render}, {"core_job", &core_job}, {"core_frame", &core_frame},
             {"job_arg", &job_arg},         {"jobs", &jobs},         {"renders", &renders},
             {"cpu2_set", &cpu2_set_rc}};
    uint32_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcmp(what, k[i].name)) {
            *got = (int)*k[i].v;
            return 1;
        }
    return 0;
}
#endif
