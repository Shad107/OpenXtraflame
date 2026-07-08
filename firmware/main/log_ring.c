/**
 * openextraflame - ring buffer capture of ESP_LOG output.
 */
#include "log_ring.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LOG_RING_SIZE 32768  /* ~32 KB, ~400 lignes, capte le boot complet cloud_bridge */

static char             s_buf[LOG_RING_SIZE];
static size_t           s_head;      /* next write index */
static size_t           s_used;      /* valid bytes stored */
static SemaphoreHandle_t s_mtx = NULL;
static vprintf_like_t   s_prev_vprintf = NULL;
static bool             s_installed = false;

static void ring_push(const char *data, size_t len)
{
    if (!s_mtx) return;
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
    for (size_t i = 0; i < len; i++) {
        s_buf[s_head] = data[i];
        s_head = (s_head + 1) % LOG_RING_SIZE;
        if (s_used < LOG_RING_SIZE) s_used++;
    }
    xSemaphoreGive(s_mtx);
}

static int log_intercept(const char *fmt, va_list args)
{
    /* First: pass through to the previous vprintf (=UART) so miniterm
     * users still see everything as before. Use a stack copy since
     * va_list is consumed. */
    va_list args_copy;
    va_copy(args_copy, args);
    int r = s_prev_vprintf ? s_prev_vprintf(fmt, args_copy) : 0;
    va_end(args_copy);

    /* Then: render into a local buffer and push into the ring. Keep the
     * buffer small so we don't blow the caller task's stack (=Wi-Fi
     * driver tasks and main_task hit this while their stacks are still
     * heavily used). Long lines get truncated, that's fine for humans. */
    char line[192];
    int n = vsnprintf(line, sizeof(line), fmt, args);
    if (n > 0) {
        if ((size_t)n > sizeof(line) - 1) n = sizeof(line) - 1;
        ring_push(line, (size_t)n);
    }
    return r;
}

esp_err_t log_ring_init(void)
{
    if (s_installed) return ESP_OK;
    s_mtx = xSemaphoreCreateMutex();
    if (!s_mtx) return ESP_ERR_NO_MEM;
    s_prev_vprintf = esp_log_set_vprintf(log_intercept);
    s_installed = true;
    return ESP_OK;
}

char *log_ring_dump(void)
{
    if (!s_mtx) return strdup("");
    if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(200)) != pdTRUE) {
        return strdup("");
    }
    char *out = malloc(s_used + 1);
    if (!out) {
        xSemaphoreGive(s_mtx);
        return NULL;
    }
    /* Copy from the oldest byte forward. */
    size_t tail = (s_used < LOG_RING_SIZE)
                    ? 0
                    : s_head;   /* buffer full, head IS the oldest */
    for (size_t i = 0; i < s_used; i++) {
        out[i] = s_buf[(tail + i) % LOG_RING_SIZE];
    }
    out[s_used] = '\0';
    xSemaphoreGive(s_mtx);
    return out;
}
