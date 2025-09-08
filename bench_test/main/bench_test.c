#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_cpu.h"

#define TAG "BENCH"
#define ITERATIONS 100

// --- Globals ---
static SemaphoreHandle_t test_sem;
static TaskHandle_t consumer_task = NULL;

static TaskHandle_t producer_task = NULL;

//static uint64_t sem_avg_cycles   = 0;
static uint64_t notif_avg_cycles = 0;
static volatile uint32_t notify_start = 0; // timestamp handoff

// --- Helper: read cycle counter ---
static inline uint32_t get_ccount(void)
{
    return esp_cpu_get_cycle_count();
}

// ================================================================
// SEMAPHORE BENCH
// ================================================================
static void consumer_semaphore(void *arg)
{
    uint64_t total_cycles = 0;
    for (int i = 0; i < ITERATIONS; i++) {
        uint32_t start = get_ccount();
        xSemaphoreTake(test_sem, portMAX_DELAY);
        uint32_t end = get_ccount();
        total_cycles += (end - start);
    }

    ESP_LOGI(TAG, "Semaphore: avg latency = %llu cycles (%.2f us at 240 MHz)",
             (unsigned long long)(total_cycles / ITERATIONS),
             (double)(total_cycles / (double)ITERATIONS) / 240.0);

    vTaskDelay(pdMS_TO_TICKS(2000));  // allow log to flush
    vTaskDelete(NULL);
}

static void producer_semaphore(void *arg)
{

    for (int i = 0; i < ITERATIONS; i++) {
        xSemaphoreGive(test_sem);
	taskYIELD();  // let consumer run

    }
    vTaskDelay(pdMS_TO_TICKS(3000));  // allow log to flush

    vTaskDelete(NULL);
}

// ================================================================
// NOTIFY BENCH
// ================================================================
static void consumer_notify(void *arg)
{
    uint64_t total_cycles = 0;

    for (int i = 0; i < ITERATIONS; i++) {
        // Wait for producer
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t end   = get_ccount();
        uint32_t start = notify_start;
        total_cycles  += (end - start);

        // ACK back to producer
        xTaskNotifyGive(producer_task);
    }

    notif_avg_cycles = total_cycles / ITERATIONS;
    ESP_LOGI(TAG, "TaskNotify: avg latency = %llu cycles (%.2f us at 240 MHz)",
             (unsigned long long)notif_avg_cycles,
             (double)notif_avg_cycles / 240.0);

    vTaskDelete(NULL);
}

static void producer_notify(void *arg)
{

    for (int i = 0; i < ITERATIONS; i++) {
        notify_start = get_ccount();

        // Notify consumer
        xTaskNotifyGive(consumer_task);

        // Wait for ACK from consumer
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    }

    ESP_LOGI(TAG, "producer_notify finished");
    vTaskDelete(NULL);
}

// ================================================================
// MAIN APP
// ================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "Starting semaphore benchmark...");
#if 1
    test_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(consumer_semaphore, "cons_sem", 4096, NULL, 5, &consumer_task, 0);

#if (CONFIG_IDF_TARGET_ESP32 == 1)
    xTaskCreatePinnedToCore(producer_semaphore, "prod_sem", 4096, NULL, 5, NULL, 1);
#else
    xTaskCreatePinnedToCore(producer_semaphore, "prod_sem", 4096, NULL, 5, NULL, 0);
#endif

#endif

// Wait for notify benchmark to finish
    vTaskDelay(pdMS_TO_TICKS(3000));

#if 1
    // Wait for semaphore benchmark to finish
    ESP_LOGI(TAG, "Starting task notification benchmark...");
    xTaskCreatePinnedToCore(consumer_notify, "cons_notify", 4096, NULL, 5, &consumer_task, 0);
    xTaskCreatePinnedToCore(producer_notify, "prod_notify", 4096, NULL, 5, &producer_task, 0);


    // Wait for notify benchmark to finish
    vTaskDelay(pdMS_TO_TICKS(13000));

#if 0
    // Final summary
    ESP_LOGI(TAG, "================= RESULTS =================");
    ESP_LOGI(TAG, "Semaphore:     %llu cycles (%.2f us)",
             (unsigned long long)sem_avg_cycles,
             (double)sem_avg_cycles / 240.0);
    ESP_LOGI(TAG, "TaskNotify:    %llu cycles (%.2f us)",
             (unsigned long long)notif_avg_cycles,
             (double)notif_avg_cycles / 240.0);
    ESP_LOGI(TAG, "===========================================");
#endif
#endif
}

