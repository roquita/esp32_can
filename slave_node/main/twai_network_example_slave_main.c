#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

/* --------------------- Definitions and static variables ------------------ */
//Example Configuration
#define TIMER_TASK_TIMEOUT_MS pdMS_TO_TICKS(1000)
#define TX_TIMEOUT_MS pdMS_TO_TICKS(1000)
#define TX_GPIO_NUM CONFIG_EXAMPLE_TX_GPIO_NUM
#define RX_GPIO_NUM CONFIG_EXAMPLE_RX_GPIO_NUM

#define ID_MASTER_STOP_CMD 0x0A0
#define ID_MASTER_START_CMD 0x0A1
#define ID_MASTER_PING 0x0A2
#define ID_SLAVE_STOP_RESP 0x0B0
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_PING_RESP 0x0B2

#define TAG "CAN BUS"

static const twai_message_t ping_resp = {.identifier = ID_SLAVE_PING_RESP, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t stop_resp = {.identifier = ID_SLAVE_STOP_RESP, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static twai_message_t data_message = {.identifier = ID_SLAVE_DATA, .data_length_code = 4, .data = {0, 0, 0, 0, 0, 0, 0, 0}};

/* --------------------------- Tasks and Functions -------------------------- */
static void config_can_bus()
{
    const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
    const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "Driver installed");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "Driver started");
}

static void can_slave_rx_task(void *arg)
{
    //Install TWAI driver, trigger tasks to start
    config_can_bus();

    while (1)
    {
        twai_message_t rx_msg;
        twai_receive(&rx_msg, portMAX_DELAY);

        if (rx_msg.identifier == ID_MASTER_PING)
        {
            //Transmit ping response to master
            if (twai_transmit(&ping_resp, TX_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGI(TAG, "Transmitted ping response");
            }
            else
            {
                ESP_LOGE(TAG, "Ping response failed");
            }
        }
        else if (rx_msg.identifier == ID_MASTER_START_CMD)
        {
            //Transmit data to master
            //FreeRTOS tick count used to simulate sensor data
            uint32_t sensor_data = xTaskGetTickCount();
            for (int i = 0; i < 4; i++)
            {
                data_message.data[i] = (sensor_data >> (i * 8)) & 0xFF;
            }

            if (twai_transmit(&data_message, TX_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGI(TAG, "Transmitted data value %d", sensor_data);
            }
            else
            {
                ESP_LOGE(TAG, "Transmition of data failed");
            }
        }
        else if (rx_msg.identifier == ID_MASTER_STOP_CMD)
        {
            //Transmit stop response to master
            bool stop_msg_sended = twai_transmit(&stop_resp, TX_TIMEOUT_MS) == ESP_OK;
            if (stop_msg_sended)
            {
                ESP_LOGI(TAG, "Transmitted stop response");
            }
            else
            {
                ESP_LOGE(TAG, "Stop response failed");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Invalid identifier");
        }
    }
}

void app_main(void)
{
    //Create tasks
    xTaskCreatePinnedToCore(can_slave_rx_task, "can_slave_rx_task", 4 * 1024, NULL, 1, NULL, 0);
}
