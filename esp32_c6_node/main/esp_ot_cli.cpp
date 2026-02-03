/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * OpenThread Command Line Example - AWS IoT Temperature Node
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/uart_types.h"
#include "nvs_flash.h"
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "openthread/logging.h"
#include "openthread/tasklet.h"
#include "esp_http_client.h"
#include "AWSSigner.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "driver/temperature_sensor.h" // Obsługa czujnika temperatury
#include "esp_crt_bundle.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif

#define TAG "ot_esp_cli"

// --- Funkcja pomocnicza: Odczyt temperatury CPU ---
float get_internal_temperature()
{
    float tsens_out;
    temperature_sensor_handle_t temp_sensor = NULL;
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);

    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_out));
    ESP_ERROR_CHECK(temperature_sensor_disable(temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_uninstall(temp_sensor));

    return tsens_out;
}

// --- Handler do czytania odpowiedzi z AWS ---
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            printf("%.*s", evt->data_len, (char *)evt->data);
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));
    return netif;
}

static void ot_task_worker(void *aContext)
{
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_openthread_init(&config));

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif
#if CONFIG_OPENTHREAD_CLI
    esp_openthread_cli_init();
#endif

    esp_netif_t *openthread_netif;
    openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif);

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif

#if CONFIG_OPENTHREAD_CLI
    esp_openthread_cli_create_task();
#endif
#if CONFIG_OPENTHREAD_AUTO_START
    otOperationalDatasetTlvs dataset;
    otError error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
    ESP_ERROR_CHECK(esp_openthread_auto_start((error == OT_ERROR_NONE) ? &dataset : NULL));
#endif
    esp_openthread_launch_mainloop();

    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);
    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

void send_signed_request()
{
    // 1. Konfiguracja (Upewnij się, że masz tu DOBRE klucze)
    AWSSigner::Config awsConfig;
    awsConfig.accessKey = "your access key";
    awsConfig.secretKey = "your secret access key";
    awsConfig.region = "your region";
    awsConfig.service = "lambda";
    awsConfig.host = "your lambda url without https:// prefix";

    AWSSigner signer(awsConfig);

    // 2. Pobranie temperatury i budowa JSON
    float current_temp = get_internal_temperature();
    char payload_buffer[128]; // Bufor na JSON

    // Tworzenie stringa np: {"deviceId":"esp32-c6", "temperature": 35.42}
    snprintf(payload_buffer, sizeof(payload_buffer),
             "{\"deviceId\":\"esp32-c6\", \"temperature\":%.2f}",
             current_temp);

    std::string payload = payload_buffer;
    std::string path = "/";

    // 3. Podpisanie requestu
    auto headers = signer.sign("PUT", path, payload);

    // 4. Setup HTTP Client
    std::string fullUrl = "https://" + awsConfig.host + path;

    esp_http_client_config_t config = {};
    config.url = fullUrl.c_str();
    config.method = HTTP_METHOD_PUT;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = _http_event_handler;
    // Ważne: Zwiększamy timeout, bo sieć Thread może mieć opóźnienia
    config.timeout_ms = 10000;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 5. Nagłówki
    for (const auto &header : headers)
    {
        esp_http_client_set_header(client, header.first.c_str(), header.second.c_str());
    }

    // 6. Body
    esp_http_client_set_post_field(client, payload.c_str(), payload.length());
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // 7. Wysłanie
    printf("Wysylanie: %s\n", payload.c_str()); // Log dla podglądu
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        printf("Status = %d\n", esp_http_client_get_status_code(client));
    }
    else
    {
        printf("Error: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void sync_time(void)
{
    ESP_LOGI("TIME", "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    time_t now = 0;
    struct tm timeinfo = {};
    int retry = 0;

    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < 15)
    {
        ESP_LOGI("TIME", "Waiting for system time... (%d/15)", retry);
    }

    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI("TIME", "Time synced: %s", asctime(&timeinfo));
}

void aws_http_task(void *pvParameters)
{
    ESP_LOGI("AWS_TASK", "Start zadania AWS...");

    // Czekanie na sieć Thread
    bool connected = false;
    while (!connected)
    {
        esp_openthread_lock_acquire(portMAX_DELAY);
        otInstance *instance = esp_openthread_get_instance();
        otDeviceRole role = otThreadGetDeviceRole(instance);
        esp_openthread_lock_release();

        if (role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER ||
            role == OT_DEVICE_ROLE_LEADER)
        {
            connected = true;
            ESP_LOGI("AWS_TASK", "Polaczono z siecia Thread (Rola: %d)", role);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Synchronizacja czasu (tylko raz na starcie)
    sync_time();

    // Nieskończona pętla wysyłania danych
    while (1)
    {
        send_signed_request();

        // Czekaj 10 sekund (10000 ms)
        ESP_LOGI("AWS_TASK", "Czekam 10 sekund...");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

extern "C" void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    xTaskCreate(ot_task_worker, "ot_cli_main", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);

    xTaskCreate(aws_http_task, "aws_http", 8192, NULL, 4, NULL);
}