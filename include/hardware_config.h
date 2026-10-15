#pragma once
#include <Arduino.h>

// --- MODULE W5500 (ETHERNET) ---
#define W5500_SCK   18
#define W5500_MISO  19
#define W5500_MOSI  23
#define W5500_CS    22
#define W5500_RST   4
#define W5500_INT   39

// --- MODULE RTC DS3231 ---
#define I2C_SDA     26
#define I2C_SCL     5

// --- CƠ CẤU CHẤP HÀNH (ACTUATORS) ---
#define RELAY1_PIN  13
#define RELAY2_PIN  14
#define MOSFET1_PIN 16
#define MOSFET2_PIN 17
#define BUZZER_PIN  21
#define LED_LIFE    2

// --- CẢM BIẾN (SENSORS) ---
#define TEMP_PIN    25
#define DIG_IN_PIN  27
#define ADC_IN_PIN  32
#define MQ2_DIG_PIN 33
#define MQ2_ADC_PIN 36

// CHÂN DỰ PHÒNG: 34, 35