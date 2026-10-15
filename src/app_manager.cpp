#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "shared_types.h"

extern QueueHandle_t appManagerQueue;
extern QueueHandle_t hardwareQueue;  
extern QueueHandle_t networkQueue;    

// Khởi tạo mảng toàn cục chứa tối đa 10 mốc hẹn giờ
LocalTimer_t active_timers[10];

void AppManagerTask(void *pvParameters) {
    AppMessage_t incomingMsg;
    
    for(int i = 0; i < 10; i++) active_timers[i].is_active = false;
    
    while (1) {
        if (xQueueReceive(appManagerQueue, &incomingMsg, portMAX_DELAY) == pdTRUE) {
            
            switch (incomingMsg.command) {
                
                case CMD_TURN_ON_RELAY:
                case CMD_TURN_OFF_RELAY:
                case CMD_TURN_ON_MOSFET:
                case CMD_TURN_OFF_MOSFET:
                    if (incomingMsg.source == SRC_NETWORK) {
                        xQueueSend(hardwareQueue, &incomingMsg, 0); 
                    }
                    break;

                case CMD_UPDATE_TEMP:
                case CMD_UPDATE_HUMID:
                case CMD_MQ2_ANALOG: 
                case CMD_SMOKE_ALERT:
                case CMD_SYNC_RELAY: 
                case CMD_SYNC_MOSFET:
                case CMD_EXT_ADC: // BỔ SUNG LỆNH
                case CMD_EXT_DIG: 
                    if (incomingMsg.source == SRC_HARDWARE) {
                        xQueueSend(networkQueue, &incomingMsg, 0); 
                    }
                    break; 

                case CMD_ADD_TIMER:
                    for (int i = 0; i < 10; i++) {
                        if (!active_timers[i].is_active) {
                            active_timers[i].is_active = true;
                            active_timers[i].target_id = incomingMsg.target_id;
                            active_timers[i].is_relay = (incomingMsg.payload >> 24) & 0xFF;
                            active_timers[i].action = (incomingMsg.payload >> 16) & 0xFF;
                            active_timers[i].hour = (incomingMsg.payload >> 8) & 0xFF;
                            active_timers[i].minute = incomingMsg.payload & 0xFF;
                            break; 
                        }
                    }
                    break;
                
                case CMD_REMOVE_TIMER:
                    for (int i = 0; i < 10; i++) {
                        if (active_timers[i].is_active && 
                            active_timers[i].target_id == incomingMsg.target_id &&
                            active_timers[i].is_relay == incomingMsg.payload) {
                            active_timers[i].is_active = false;
                        }
                    }
                    break;
                    
                // XỬ LÝ KHI PHẦN CỨNG BÁO "ĐÃ ĐẾN GIỜ"
                case CMD_TIMER_TRIGGERED: {
                    // 1. Chuyển lệnh xuống Hardware để Bật/Tắt thật sự
                    AppMessage_t hwMsg;
                    hwMsg.source = SRC_NETWORK; 
                    uint8_t is_r = (incomingMsg.payload >> 24) & 0xFF;
                    uint8_t act  = (incomingMsg.payload >> 16) & 0xFF;
                    
                    if (is_r) hwMsg.command = act ? CMD_TURN_ON_RELAY : CMD_TURN_OFF_RELAY;
                    else      hwMsg.command = act ? CMD_TURN_ON_MOSFET : CMD_TURN_OFF_MOSFET;
                    
                    hwMsg.target_id = incomingMsg.target_id;
                    xQueueSend(hardwareQueue, &hwMsg, 0);
                    
                    // 2. Chuyển lệnh lên Network để Publish báo cáo cho Server
                    xQueueSend(networkQueue, &incomingMsg, 0);
                    break;
                }

                default:
                    break;
            }
        }
    }
}