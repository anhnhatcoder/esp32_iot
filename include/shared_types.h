#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h> 

#define WIFI_CONNECTED_BIT (1 << 0) 

#define SRC_NETWORK  1
#define SRC_HARDWARE 2

#define CMD_TURN_ON_RELAY   10
#define CMD_TURN_OFF_RELAY  11
#define CMD_TURN_ON_MOSFET  12
#define CMD_TURN_OFF_MOSFET 13

#define CMD_UPDATE_TEMP     20
#define CMD_UPDATE_HUMID    21
#define CMD_MQ2_ANALOG      22
#define CMD_SMOKE_ALERT     23
#define CMD_SYNC_RELAY      24
#define CMD_SYNC_MOSFET     25

#define CMD_EXT_ADC         30
#define CMD_EXT_DIG         31

#define CMD_ADD_TIMER       70
#define CMD_REMOVE_TIMER    71
#define CMD_TIMER_TRIGGERED 72 

typedef struct {
    uint8_t source;
    uint8_t command;
    uint8_t target_id;
    uint32_t payload;
} AppMessage_t;

typedef struct {
    bool is_active;
    uint8_t target_id;
    uint8_t is_relay;
    uint8_t action;
    uint8_t hour;
    uint8_t minute;
} LocalTimer_t;

// --- KHÓA CỨNG BỘ NHỚ CÁC GÓI TIN ESP-NOW BẰNG PACKED ---
typedef struct __attribute__((packed)) {
    char node_id[15];
    char api_key[33];
    float temperature;
    float humidity;
    int smoke;
    int ext_adc; 
    int ext_dig; 
} EspNowData_t;

typedef struct __attribute__((packed)) {
    char target_node[15];
    uint8_t is_relay;
    uint8_t device_index;
    uint8_t state;
} EspNowControlMsg_t;

typedef struct __attribute__((packed)) {
    char target_node[15];
    uint8_t is_relay;       
    uint8_t device_index;   
    uint8_t is_add;         
    uint8_t action;         
    uint8_t hour;           
    uint8_t minute;         
} EspNowTimerMsg_t;

extern LocalTimer_t active_timers[10];
extern String global_node_id;
extern String global_api_key;
extern String global_mqtt_server;

extern QueueHandle_t appManagerQueue;
extern QueueHandle_t hardwareQueue;
extern QueueHandle_t networkQueue;
extern QueueHandle_t espnowQueue;
extern EventGroupHandle_t networkEventGroup;

#endif