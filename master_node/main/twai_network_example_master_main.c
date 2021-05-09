#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"
#include "driver/gpio.h"

#define TAG "CAN BUS"

/* --------------------- Definitions and static variables ------------------ */
//Example Configuration
#define TX_TIMEOUT_MS pdMS_TO_TICKS(1000)
#define TX_GPIO_NUM CONFIG_EXAMPLE_TX_GPIO_NUM
#define RX_GPIO_NUM CONFIG_EXAMPLE_RX_GPIO_NUM

#define ID_MASTER_STOP_CMD 0x0A0
#define ID_MASTER_START_CMD 0x0A1
#define ID_MASTER_PING 0x0A2
#define ID_SLAVE_STOP_RESP 0x0B0
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_PING_RESP 0x0B2

typedef enum
{
    TX_SEND_PINGS,
    TX_SEND_START_CMD,
    TX_SEND_STOP_CMD,
    TX_TASK_EXIT,
} tx_task_action_t;

static const twai_message_t ping_message = {.identifier = ID_MASTER_PING, .data_length_code = 0, .ss = 1, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t start_message = {.identifier = ID_MASTER_START_CMD, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t stop_message = {.identifier = ID_MASTER_STOP_CMD, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}};

static QueueHandle_t tx_task_queue;

/* --------------------------- Tasks and Functions -------------------------- */

static void maxter_rx_task(void *arg)
{
    while (1)
    {
        twai_message_t rx_msg;
        twai_receive(&rx_msg, portMAX_DELAY);

        if (rx_msg.identifier == ID_SLAVE_PING_RESP)
        {
            //Ping response from slave
            ESP_LOGI(TAG, "Pin response");
        }
        else if (rx_msg.identifier == ID_SLAVE_DATA)
        {
            //Print received data from slave
            uint32_t data = 0;
            for (int i = 0; i < rx_msg.data_length_code; i++)
            {
                data |= (rx_msg.data[i] << (i * 8));
            }
            ESP_LOGI(TAG, "Received data value %d", data);
        }
        else if (rx_msg.identifier == ID_SLAVE_STOP_RESP)
        {
            //Stop response from slave
            ESP_LOGI(TAG, "Stop response");
        }
        else
        {
            ESP_LOGE(TAG, "Invalid identifier");
        }
    }
}

static void master_tx_task(void *arg)
{
    //Create tasks, queues, and semaphores
    tx_task_queue = xQueueCreate(1, sizeof(tx_task_action_t));

    while (1)
    {
        tx_task_action_t action;
        xQueueReceive(tx_task_queue, &action, portMAX_DELAY);

        if (action == TX_SEND_PINGS)
        {
            //Repeatedly transmit pings
            if (twai_transmit(&ping_message, TX_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGI(TAG, "Tx ping transmitted");
            }
        }
        else if (action == TX_SEND_START_CMD)
        {
            //Transmit start command to slave
            if (twai_transmit(&start_message, TX_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGI(TAG, "Tx start cmd transmitted");
            }
        }
        else if (action == TX_SEND_STOP_CMD)
        {
            //Transmit stop command to slave
            if (twai_transmit(&stop_message, TX_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGI(TAG, "Tx stop cmd transmitted");
            }
        }
    }
}

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
void gpio_isr(void *arg)
{
    tx_task_action_t action = TX_SEND_START_CMD;
    xQueueSendFromISR(tx_task_queue, &action, NULL);
}
static void config_builtin_button()
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << 0,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&config);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(0, gpio_isr, NULL);
}

void app_main(void)
{
    //Install TWAI driver, trigger tasks to start
    config_can_bus();
    config_builtin_button();

    xTaskCreatePinnedToCore(maxter_rx_task, "maxter_rx_task", 4 * 1024, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(master_tx_task, "master_tx_task", 4 * 1024, NULL, 5, NULL, 0);
}
