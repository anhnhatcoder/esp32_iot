#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include "shared_types.h"
#include "hardware_config.h" 

extern QueueHandle_t appManagerQueue;
extern QueueHandle_t hardwareQueue;
extern EventGroupHandle_t networkEventGroup; 

RTC_DS3231 rtc;
#define DHTTYPE DHT22
DHT dht(TEMP_PIN, DHTTYPE); 

void syncDeviceStatus(uint8_t syncCmd, uint8_t devId, uint8_t status) {
    AppMessage_t syncMsg;
    syncMsg.source = SRC_HARDWARE;
    syncMsg.command = syncCmd;
    syncMsg.target_id = devId;
    syncMsg.payload = status;
    xQueueSend(appManagerQueue, &syncMsg, 0);
}

void HardwareTask(void *pvParameters) {
    pinMode(RELAY1_PIN, OUTPUT); digitalWrite(RELAY1_PIN, LOW);
    pinMode(RELAY2_PIN, OUTPUT); digitalWrite(RELAY2_PIN, LOW);
    pinMode(MOSFET1_PIN, OUTPUT); digitalWrite(MOSFET1_PIN, LOW);
    pinMode(MOSFET2_PIN, OUTPUT); digitalWrite(MOSFET2_PIN, LOW);
    pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
    pinMode(LED_LIFE, OUTPUT); digitalWrite(LED_LIFE, LOW);
    pinMode(MQ2_ADC_PIN, INPUT);
    pinMode(DIG_IN_PIN, INPUT);
    
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!rtc.begin()) {
        Serial.println("HW: Khong tim thay module RTC DS3231!");
    } else {
        if (rtc.lostPower()) {
            Serial.println("HW: RTC mat nguon, dang thiet lap lai thoi gian...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); 
        }
    }
    
    dht.begin(); 

    AppMessage_t cmdMsg;
    TickType_t lastSensorRead = xTaskGetTickCount();
    TickType_t lastTimerCheck = 0;
    unsigned long lastLedUpdate = 0;
    bool ledState = false;

    while (1) {
        if (millis() - lastLedUpdate >= 1000) { 
            lastLedUpdate = millis();
            EventBits_t netBits = xEventGroupGetBits(networkEventGroup);
            if ((netBits & WIFI_CONNECTED_BIT) != 0) {
                ledState = !ledState;
                digitalWrite(LED_LIFE, ledState ? HIGH : LOW);
            } else {
                digitalWrite(LED_LIFE, LOW);
            }
        }

        if (xTaskGetTickCount() - lastTimerCheck >= pdMS_TO_TICKS(10000)) {
            lastTimerCheck = xTaskGetTickCount();
            DateTime now = rtc.now();
            
            for (int i = 0; i < 10; i++) {
                if (active_timers[i].is_active) {
                    if (now.hour() == active_timers[i].hour && now.minute() == active_timers[i].minute) {
                        Serial.println("\n*** HW: KICH HOAT HEN GIO OFFLINE! ***");
                        
                        active_timers[i].is_active = false;
                        
                        AppMessage_t trigMsg;
                        trigMsg.source = SRC_HARDWARE; 
                        trigMsg.command = CMD_TIMER_TRIGGERED;
                        trigMsg.target_id = active_timers[i].target_id;
                        trigMsg.payload = ((uint32_t)active_timers[i].is_relay << 24) | 
                                          ((uint32_t)active_timers[i].action << 16) | 
                                          ((uint32_t)active_timers[i].hour << 8) | 
                                          (uint32_t)active_timers[i].minute;
                        
                        xQueueSend(appManagerQueue, &trigMsg, 0);
                    }
                }
            }
        }

        if (xQueueReceive(hardwareQueue, &cmdMsg, pdMS_TO_TICKS(10)) == pdTRUE) {
            bool state = (cmdMsg.command == CMD_TURN_ON_RELAY || cmdMsg.command == CMD_TURN_ON_MOSFET);
            uint8_t pinState = state ? HIGH : LOW;

            if (cmdMsg.command == CMD_TURN_ON_RELAY || cmdMsg.command == CMD_TURN_OFF_RELAY) {
                if (cmdMsg.target_id == 0) digitalWrite(RELAY1_PIN, pinState);
                else if (cmdMsg.target_id == 1) digitalWrite(RELAY2_PIN, pinState);
                syncDeviceStatus(CMD_SYNC_RELAY, cmdMsg.target_id, state ? 1 : 0);
            }
            else if (cmdMsg.command == CMD_TURN_ON_MOSFET || cmdMsg.command == CMD_TURN_OFF_MOSFET) {
                if (cmdMsg.target_id == 0) digitalWrite(MOSFET1_PIN, pinState);
                else if (cmdMsg.target_id == 1) digitalWrite(MOSFET2_PIN, pinState);
                syncDeviceStatus(CMD_SYNC_MOSFET, cmdMsg.target_id, state ? 1 : 0);
            }
        }

        if (xTaskGetTickCount() - lastSensorRead >= pdMS_TO_TICKS(30000)) {
            lastSensorRead = xTaskGetTickCount();
            
            Serial.println("\n------------------------------------");
            DateTime now = rtc.now();
            Serial.printf("[%02d/%02d/%04d %02d:%02d:%02d] (RTC OK)\n", 
                          now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second());

            // 1. ĐỌC MQ2 (Khói)
            uint16_t mq2_val = analogRead(MQ2_ADC_PIN);
            Serial.printf("-> MQ2 (Khoi): %d / 4095\n", mq2_val);
            AppMessage_t smokeMsg = {SRC_HARDWARE, CMD_MQ2_ANALOG, 0, mq2_val};
            xQueueSend(appManagerQueue, &smokeMsg, 0);

            if (mq2_val > 2500) { 
                digitalWrite(BUZZER_PIN, HIGH);
                AppMessage_t alertMsg = smokeMsg;
                alertMsg.command = CMD_SMOKE_ALERT;
                xQueueSend(appManagerQueue, &alertMsg, 0);
            } else {
                digitalWrite(BUZZER_PIN, LOW);
            }

            // 2. ĐỌC DHT22
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            if (isnan(t) || isnan(h)) {
                Serial.println("-> DHT22: LOI KHONG DOC DUOC DU LIEU!");
            } else {
                Serial.printf("-> DHT22: Nhiet do = %.1f*C | Do am = %.1f%%\n", t, h);
                AppMessage_t tempMsg = {SRC_HARDWARE, CMD_UPDATE_TEMP, 0, (uint32_t)(t * 10.0)};
                AppMessage_t humidMsg = {SRC_HARDWARE, CMD_UPDATE_HUMID, 0, (uint32_t)(h * 10.0)};
                xQueueSend(appManagerQueue, &tempMsg, 0);
                xQueueSend(appManagerQueue, &humidMsg, 0);
            }

            // 3. ĐỌC CẢM BIẾN DỰ PHÒNG (Đất/Mưa)
            // Đảo ngược logic Analog: Lấy 4095 trừ đi giá trị gốc
            uint16_t ext_adc_val = 4095 - analogRead(ADC_IN_PIN);
            uint8_t ext_dig_val = digitalRead(DIG_IN_PIN);
            Serial.printf("-> EXT SENSOR: Analog = %d | Digital = %d\n", ext_adc_val, ext_dig_val);
            
            AppMessage_t extAdcMsg = {SRC_HARDWARE, CMD_EXT_ADC, 0, ext_adc_val};
            AppMessage_t extDigMsg = {SRC_HARDWARE, CMD_EXT_DIG, 0, ext_dig_val};
            xQueueSend(appManagerQueue, &extAdcMsg, 0);
            xQueueSend(appManagerQueue, &extDigMsg, 0);
        }
    }
}