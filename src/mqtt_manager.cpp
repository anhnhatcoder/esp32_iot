#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> 
#include <Ethernet.h>         
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>             
#include <RTClib.h>           
#include <esp_now.h>
#include <esp_wifi.h>          
#include "shared_types.h"

const char* hivemq_server = "8713ea0e01c24435884b3579227c125e.s1.eu.hivemq.cloud";
const int hivemq_port = 8883;
const char* hivemq_user = "admin"; 
const char* hivemq_password = "admin123"; 

WiFiClientSecure wifiClient;  
EthernetClient ethClient;     
PubSubClient mqttClient;      

extern RTC_DS3231 rtc; 
extern bool isEthernetActive; 

extern QueueHandle_t appManagerQueue;
extern QueueHandle_t networkQueue;
extern QueueHandle_t espnowQueue;
extern EventGroupHandle_t networkEventGroup;

// --- HÀM XIN GIỜ: GỬI LỆNH LÊN MQTT CHỜ SERVER DJANGO TRẢ LỜI ---
void syncNTPTime() {
    Serial.println("NTP: Dang gui yeu cau cap nhat gio len MQTT Server...");
    String time_req_topic = "factory/nodes/" + global_node_id + "/request_time";
    mqttClient.publish(time_req_topic.c_str(), "{\"get_time\":true}");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String messageTemp;
    for (int i = 0; i < length; i++) messageTemp += (char)payload[i];
    
    JsonDocument doc; 
    if (deserializeJson(doc, messageTemp)) return;
    JsonObject root = doc.as<JsonObject>();
    
    // --- 1. BẮT GÓI TIN NHẬN GIỜ TỪ DJANGO ---
    if (!root["sync_year"].isNull() && !root["sync_hour"].isNull()) {
        int year = root["sync_year"];
        int month = root["sync_month"];
        int day = root["sync_day"];
        int hour = root["sync_hour"];
        int minute = root["sync_minute"];
        int second = root["sync_second"];

        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        Serial.printf("MQTT: [OK] Da dong bo gio tu Server thanh cong! Gio: %02d:%02d:%02d\n", hour, minute, second);
        return; // Nhận giờ xong thì thoát luôn, không xử lý lệnh khác
    }

    if (!root["timer_triggered"].isNull()) return;

    String topicStr = String(topic);
    String targetNode = "";
    int secondSlash = topicStr.indexOf('/', 8); 
    int thirdSlash = topicStr.indexOf('/', secondSlash + 1); 
    
    if (secondSlash != -1 && thirdSlash != -1) {
        targetNode = topicStr.substring(secondSlash + 1, thirdSlash);
    }
    
    // --- 2. XỬ LÝ LỆNH THÊM HẸN GIỜ ---
    if (!root["timer_device"].isNull() && !root["timer_action"].isNull()) {
        String t_device = root["timer_device"].as<String>();
        uint8_t action = root["timer_action"].as<int>();
        uint8_t hh = root["hour"].as<int>();
        uint8_t mm = root["minute"].as<int>();
        
        uint8_t dev_id = t_device.endsWith("_1") ? 1 : 0;
        uint8_t is_relay = t_device.startsWith("relay") ? 1 : 0;
        
        if (targetNode == global_node_id) {
            AppMessage_t timerMsg;
            timerMsg.source = SRC_NETWORK;
            timerMsg.command = CMD_ADD_TIMER;
            timerMsg.target_id = dev_id;
            timerMsg.payload = ((uint32_t)is_relay << 24) | ((uint32_t)action << 16) | ((uint32_t)hh << 8) | (uint32_t)mm;
            xQueueSend(appManagerQueue, &timerMsg, 0);
        } 
        else if (targetNode != "") {
            EspNowTimerMsg_t timerMeshMsg;
            memset(&timerMeshMsg, 0, sizeof(timerMeshMsg));
            strncpy(timerMeshMsg.target_node, targetNode.c_str(), sizeof(timerMeshMsg.target_node)-1);
            timerMeshMsg.is_relay = is_relay;
            timerMeshMsg.device_index = dev_id;
            timerMeshMsg.is_add = 1; 
            timerMeshMsg.action = action;
            timerMeshMsg.hour = hh;
            timerMeshMsg.minute = mm;
            
            uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            for (int i = 0; i < 15; i++) {
                esp_now_send(broadcastMac, (uint8_t *) &timerMeshMsg, sizeof(EspNowTimerMsg_t));
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        return;
    }
    
    // --- 3. XỬ LÝ LỆNH HỦY HẸN GIỜ ---
    if (!root["cancel_timer"].isNull()) {
        String t_device = root["timer_device"].as<String>();
        uint8_t dev_id = t_device.endsWith("_1") ? 1 : 0;
        uint8_t is_relay = t_device.startsWith("relay") ? 1 : 0;
        
        if (targetNode == global_node_id) {
            AppMessage_t timerMsg;
            timerMsg.source = SRC_NETWORK;
            timerMsg.command = CMD_REMOVE_TIMER;
            timerMsg.target_id = dev_id;
            timerMsg.payload = is_relay;
            xQueueSend(appManagerQueue, &timerMsg, 0);
        }
        else if (targetNode != "") {
            EspNowTimerMsg_t timerMeshMsg;
            memset(&timerMeshMsg, 0, sizeof(timerMeshMsg)); 
            strncpy(timerMeshMsg.target_node, targetNode.c_str(), sizeof(timerMeshMsg.target_node)-1);
            timerMeshMsg.is_relay = is_relay;
            timerMeshMsg.device_index = dev_id;
            timerMeshMsg.is_add = 0; 
            
            uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            for (int i = 0; i < 15; i++) {
                esp_now_send(broadcastMac, (uint8_t *) &timerMeshMsg, sizeof(EspNowTimerMsg_t));
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        return;
    }

    // --- 4. XỬ LÝ LỆNH BẬT/TẮT RELAY TRỰC TIẾP ---
    for (JsonPair kv : root) {
        String key = kv.key().c_str();        
        if (key == "timer_device" || key == "timer_action" || key == "hour" || key == "minute" || key == "cancel_timer" || key == "timer_triggered") continue;

        int status_val = kv.value().as<int>(); 
        int id_index = key.lastIndexOf('_');
        uint8_t dev_id = (id_index != -1) ? key.substring(id_index + 1).toInt() : 0;
        uint8_t is_relay = key.startsWith("relay") ? 1 : 0;

        if (targetNode == global_node_id) {
            AppMessage_t cmdMsg;
            cmdMsg.source = SRC_NETWORK;
            cmdMsg.target_id = dev_id;
            if (is_relay == 1) cmdMsg.command = (status_val == 1) ? CMD_TURN_ON_RELAY : CMD_TURN_OFF_RELAY;
            else cmdMsg.command = (status_val == 1) ? CMD_TURN_ON_MOSFET : CMD_TURN_OFF_MOSFET;
            xQueueSend(appManagerQueue, &cmdMsg, 0); 
        }
        else if (targetNode != "") {
            EspNowControlMsg_t ctrlMsg;
            memset(&ctrlMsg, 0, sizeof(ctrlMsg)); 
            strncpy(ctrlMsg.target_node, targetNode.c_str(), sizeof(ctrlMsg.target_node)-1);
            ctrlMsg.is_relay = is_relay;
            ctrlMsg.device_index = dev_id;
            ctrlMsg.state = status_val;
            
            uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            for (int i = 0; i < 15; i++) {
                esp_now_send(broadcastMac, (uint8_t *) &ctrlMsg, sizeof(EspNowControlMsg_t));
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}

void MQTTTask(void *pvParameters) {
    AppMessage_t incomingMsg;
    EspNowData_t meshMsg; 
    unsigned long lastMqttReconnect = 0;
    bool timeSynced = false;
    float current_temp = 0.0, current_humid = 0.0;
    int current_smoke = 0;
    int current_ext_adc = 0; 
    int current_ext_dig = 0; 
    
    mqttClient.setBufferSize(1024); 
    mqttClient.setCallback(mqttCallback);
    wifiClient.setInsecure(); wifiClient.setHandshakeTimeout(30); 

    while (1) {
        EventBits_t bits = xEventGroupGetBits(networkEventGroup);
        bool hasNetwork = (bits & WIFI_CONNECTED_BIT) != 0;

        if (hasNetwork) {
            String sub_topic = "factory/nodes/+/control";
            String sub_time_topic = "factory/nodes/" + global_node_id + "/control"; 

            if (!mqttClient.connected()) {
                if (millis() - lastMqttReconnect > 5000) {
                    lastMqttReconnect = millis();
                    timeSynced = false; // Mất mạng thì reset lại trạng thái xin giờ

                    if (isEthernetActive) {
                        mqttClient.setClient(ethClient); 
                        mqttClient.setServer(global_mqtt_server.c_str(), 1883); 
                        if (mqttClient.connect(global_node_id.c_str())) { 
                            mqttClient.subscribe(sub_topic.c_str(), 1); 
                            mqttClient.subscribe(sub_time_topic.c_str(), 1); 
                        }
                    } else {
                        mqttClient.setClient(wifiClient); 
                        mqttClient.setServer(hivemq_server, hivemq_port);
                        if (mqttClient.connect(global_node_id.c_str(), hivemq_user, hivemq_password)) { 
                            mqttClient.subscribe(sub_topic.c_str(), 1); 
                            mqttClient.subscribe(sub_time_topic.c_str(), 1); 
                        }
                    }
                }
            } else {
                mqttClient.loop(); 

                // --- GỌI HÀM XIN GIỜ NGAY KHI VỪA VÀO MQTT ---
                if (!timeSynced) {
                    syncNTPTime();
                    timeSynced = true; // Chỉ xin 1 lần để tránh Spam Server
                }
                
                if (xQueueReceive(espnowQueue, &meshMsg, 0) == pdTRUE) {
                    JsonDocument doc;
                    doc["node_id"] = String(meshMsg.node_id); 
                    doc["api_key"] = String(meshMsg.api_key); 
                    doc["temperature"] = meshMsg.temperature;
                    doc["humidity"] = meshMsg.humidity;
                    doc["smoke"] = meshMsg.smoke;
                    doc["ext_adc"] = meshMsg.ext_adc; 
                    doc["ext_dig"] = meshMsg.ext_dig; 
                    doc["connection"] = "ESP-NOW"; 
                    
                    char jsonBuffer[300];
                    serializeJson(doc, jsonBuffer);
                    String forward_topic = "factory/nodes/" + String(meshMsg.node_id);
                    mqttClient.publish(forward_topic.c_str(), jsonBuffer);
                }
            }
        }

        if (xQueueReceive(networkQueue, &incomingMsg, 0) == pdTRUE) {
            if (incomingMsg.command == CMD_MQ2_ANALOG) current_smoke = incomingMsg.payload;
            else if (incomingMsg.command == CMD_EXT_ADC) current_ext_adc = incomingMsg.payload;
            else if (incomingMsg.command == CMD_EXT_DIG) current_ext_dig = incomingMsg.payload;
            else if (incomingMsg.command == CMD_UPDATE_TEMP) current_temp = (float)incomingMsg.payload / 10.0;
            else if (incomingMsg.command == CMD_UPDATE_HUMID) {
                current_humid = (float)incomingMsg.payload / 10.0;

                if (hasNetwork && mqttClient.connected()) {
                    JsonDocument doc;
                    doc["node_id"] = global_node_id; 
                    doc["api_key"] = global_api_key; 
                    doc["temperature"] = current_temp; 
                    doc["humidity"] = current_humid; 
                    doc["smoke"] = current_smoke;
                    doc["ext_adc"] = current_ext_adc; 
                    doc["ext_dig"] = current_ext_dig; 
                    doc["connection"] = isEthernetActive ? "Ethernet" : "WiFi";
                    
                    char jsonBuffer[300]; 
                    serializeJson(doc, jsonBuffer);
                    String pub_topic = "factory/nodes/" + global_node_id;
                    mqttClient.publish(pub_topic.c_str(), jsonBuffer);
                } else {
                    EspNowData_t outMsg;
                    memset(&outMsg, 0, sizeof(outMsg)); 
                    strncpy(outMsg.node_id, global_node_id.c_str(), sizeof(outMsg.node_id)-1);
                    strncpy(outMsg.api_key, global_api_key.c_str(), sizeof(outMsg.api_key)-1);
                    outMsg.temperature = current_temp; 
                    outMsg.humidity = current_humid; 
                    outMsg.smoke = current_smoke;
                    outMsg.ext_adc = current_ext_adc; 
                    outMsg.ext_dig = current_ext_dig; 
                    
                    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                    WiFi.mode(WIFI_STA); 
                    for (uint8_t ch = 1; ch <= 13; ch++) {
                        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE); 
                        esp_now_send(broadcastMac, (uint8_t *) &outMsg, sizeof(EspNowData_t));
                        vTaskDelay(pdMS_TO_TICKS(50)); 
                    }
                }
            }
            else if (incomingMsg.command == CMD_TIMER_TRIGGERED) {
                if (hasNetwork && mqttClient.connected()) {
                    uint8_t is_r = (incomingMsg.payload >> 24) & 0xFF;
                    uint8_t act  = (incomingMsg.payload >> 16) & 0xFF;
                    String dev_name = (is_r ? "relay_" : "mosfet_") + String(incomingMsg.target_id);
                    
                    JsonDocument doc;
                    doc[dev_name] = act;             
                    doc["timer_triggered"] = true;   
                    
                    char jsonBuffer[128];
                    serializeJson(doc, jsonBuffer);
                    String pub_topic = "factory/nodes/" + global_node_id + "/control";
                    mqttClient.publish(pub_topic.c_str(), jsonBuffer);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}