#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "shared_types.h"

extern QueueHandle_t espnowQueue;
extern QueueHandle_t appManagerQueue;
extern EventGroupHandle_t networkEventGroup; 

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    EventBits_t netBits = xEventGroupGetBits(networkEventGroup);
    
    if (len == sizeof(EspNowData_t)) {
        if ((netBits & WIFI_CONNECTED_BIT) != 0) {
            EspNowData_t incomingMsg;
            memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));
            // KHÓA ĐUÔI CHUỖI KÝ TỰ ĐỂ CHỐNG TRÀN BỘ NHỚ
            incomingMsg.node_id[sizeof(incomingMsg.node_id)-1] = '\0';
            xQueueSend(espnowQueue, &incomingMsg, 0);
        }
    } 
    else if (len == sizeof(EspNowControlMsg_t)) {
        EspNowControlMsg_t ctrlMsg;
        memcpy(&ctrlMsg, incomingData, sizeof(ctrlMsg));
        ctrlMsg.target_node[sizeof(ctrlMsg.target_node)-1] = '\0'; // BẢO VỆ CHIP
        
        if (String(ctrlMsg.target_node) == global_node_id) {
            AppMessage_t cmdMsg;
            cmdMsg.source = SRC_NETWORK;
            cmdMsg.target_id = ctrlMsg.device_index;
            if (ctrlMsg.is_relay == 1) cmdMsg.command = (ctrlMsg.state == 1) ? CMD_TURN_ON_RELAY : CMD_TURN_OFF_RELAY;
            else cmdMsg.command = (ctrlMsg.state == 1) ? CMD_TURN_ON_MOSFET : CMD_TURN_OFF_MOSFET;
            xQueueSend(appManagerQueue, &cmdMsg, 0);
        }
    }
    else if (len == sizeof(EspNowTimerMsg_t)) {
        EspNowTimerMsg_t timerMsg;
        memcpy(&timerMsg, incomingData, sizeof(timerMsg));
        timerMsg.target_node[sizeof(timerMsg.target_node)-1] = '\0'; // BẢO VỆ CHIP

        if (String(timerMsg.target_node) == global_node_id) {
            AppMessage_t appMsg;
            appMsg.source = SRC_NETWORK; 
            appMsg.target_id = timerMsg.device_index;

            if (timerMsg.is_add == 1) { 
                appMsg.command = CMD_ADD_TIMER;
                appMsg.payload = ((uint32_t)timerMsg.is_relay << 24) | ((uint32_t)timerMsg.action << 16) | ((uint32_t)timerMsg.hour << 8) | (uint32_t)timerMsg.minute;
            } else { 
                appMsg.command = CMD_REMOVE_TIMER;
                appMsg.payload = timerMsg.is_relay;
            }
            xQueueSend(appManagerQueue, &appMsg, 0);
        }
    }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {}

void setupEspNow() {
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return;
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}