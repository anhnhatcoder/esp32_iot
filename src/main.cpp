#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <Wire.h> 
#include <SPI.h>  
#include "shared_types.h"
#include "hardware_config.h" 

void AppManagerTask(void *pvParameters);
void HardwareTask(void *pvParameters); 
void NetworkTask(void *pvParameters); 
void MQTTTask(void *pvParameters);
void setupEspNow(); // Khai báo hàm khởi tạo từ espnow_manager.cpp

QueueHandle_t appManagerQueue;
QueueHandle_t hardwareQueue; 
QueueHandle_t networkQueue;
QueueHandle_t espnowQueue; // Hàng đợi mới dành riêng cho trung chuyển ESP-NOW
EventGroupHandle_t networkEventGroup;

void setup() {
    Serial.begin(115200);

    // 1. Khởi tạo hàng đợi
    appManagerQueue = xQueueCreate(20, sizeof(AppMessage_t));
    hardwareQueue = xQueueCreate(20, sizeof(AppMessage_t)); 
    networkQueue = xQueueCreate(20, sizeof(AppMessage_t));
    espnowQueue = xQueueCreate(10, sizeof(EspNowData_t)); // Chứa tối đa 10 gói tin vệ tinh chờ xử lý
    networkEventGroup = xEventGroupCreate();

    // 2. Khởi tạo ESP-NOW làm trạm thu sóng
    setupEspNow();

    // 3. Phân bổ Core 0 cho mạng, Core 1 cho điều khiển phần cứng
    xTaskCreatePinnedToCore(AppManagerTask, "AppManagerTask", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(HardwareTask, "HardwareTask", 8192, NULL, 3, NULL, 1); 
    xTaskCreatePinnedToCore(NetworkTask, "NetworkTask", 8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(MQTTTask, "MQTTTask", 8192, NULL, 2, NULL, 0);
}

void loop() {
    vTaskDelete(NULL); 
}