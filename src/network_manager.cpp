#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h> 
#include <Ethernet.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_wifi.h> 
#include "shared_types.h"
#include "hardware_config.h"

#define BOOT_PIN 0 

// ÉP KHAI BÁO TRỰC TIẾP TẠI ĐÂY ĐỂ TRÁNH LỖI SCOPE CỦA COMPILER
extern EventGroupHandle_t networkEventGroup;

String global_node_id = "NODE_407258"; 
String global_api_key = "175c184ad9553d4523fc4235a40a9739";
String global_mqtt_server = "192.168.40.1"; 

bool isEthernetActive = false; 
byte mac[6]; 

void openWiFiPortal() {
    Serial.println("\nNET: Dang mo diem phat WiFi de cau hinh...");
    WiFi.setTxPower(WIFI_POWER_8_5dBm); 
    
    WiFiManager wm;
    wm.setConfigPortalTimeout(180); // CHỈ CHỜ ĐÚNG 3 PHÚT

    // Mở điểm phát WiFi có tên ESP32_SmartNode
    if (wm.startConfigPortal("ESP32_SmartNode")) {
        Serial.println("NET: Da luu cau hinh WiFi moi! Dang khoi dong lai...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart(); 
    } else {
        Serial.println("NET: Het 3 phut khong ai cau hinh. Huy AP va khoi dong lai...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
    }
}

void NetworkTask(void *pvParameters) {
    pinMode(BOOT_PIN, INPUT_PULLUP);

    WiFi.macAddress(mac);
    mac[0] |= 0x02; 

    // --- 1. KHỞI TẠO LAN ---
    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW);
    vTaskDelay(pdMS_TO_TICKS(100)); 
    digitalWrite(W5500_RST, HIGH);
    vTaskDelay(pdMS_TO_TICKS(1000)); 
    Ethernet.init(W5500_CS);

    Serial.println("NET: Dang kiem tra cap LAN, cho Switch phan hoi (Max 5 giay)...");
    for(int i = 0; i < 50; i++) {
        if (Ethernet.linkStatus() == LinkON) break;
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }

    if (Ethernet.linkStatus() == LinkON) {
        Serial.println("NET: Phat hien cap LAN W5500!");
    } else {
        // --- BỎ WIFIMANAGER KHI KHỞI ĐỘNG - KẾT NỐI WIFI NGẦM CHỐNG ĐƠ MẠCH ---
        Serial.println("NET: Khong co cap LAN. Chay tien trinh ket noi WiFi ngam...");
        WiFi.mode(WIFI_STA);
        WiFi.begin(); // Hàm mặc định tự lục tìm lại tên/pass WiFi đã lưu trong NVS
    }

    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    bool lastNetStatus = false;
    int netCheckTimer = 0;
    int bootPressTimer = 0;
    unsigned long lastWiFiRetry = 0; // Biến nhớ để quét lại WiFi định kỳ

    while (1) {
        // --- 2. THEO DÕI NÚT NHẤN BOOT ---
        if (digitalRead(BOOT_PIN) == LOW) {
            bootPressTimer += 100; 
            if (bootPressTimer >= 3000) { 
                bootPressTimer = 0;
                openWiFiPortal(); // Gọi hàm phát WiFi cấu hình
            }
        } else {
            bootPressTimer = 0; 
        }

        // --- 3. THEO DÕI TRẠNG THÁI MẠNG LƯỚI ---
        netCheckTimer += 100;
        if (netCheckTimer >= 2000) {
            netCheckTimer = 0;
            bool currentNetStatus = false;

            if (Ethernet.linkStatus() == LinkON) {
                if (!isEthernetActive) {
                    Serial.println("NET: Dang xin IP tu DHCP W5500...");
                    if (Ethernet.begin(mac, 4000, 4000)) { 
                        Serial.print("NET: [OK] W5500 DA KET NOI! IP: ");
                        Serial.println(Ethernet.localIP());
                        isEthernetActive = true;
                        
                        WiFi.disconnect();    
                        WiFi.mode(WIFI_STA);  
                    } else {
                        isEthernetActive = false; 
                    }
                }
                currentNetStatus = isEthernetActive; 
            } 
            else {
                if (isEthernetActive) {
                    Serial.println("NET: Mat cap LAN W5500, fallback sang WiFi...");
                    isEthernetActive = false;
                    WiFi.mode(WIFI_STA);
                    WiFi.begin(); 
                }
                
                currentNetStatus = (WiFi.status() == WL_CONNECTED);

                // CƠ CHẾ LIÊN TỤC QUÉT LẠI WIFI: 
                // Nếu rớt mạng, cứ mỗi 30 giây sẽ thử kết nối lại 1 lần để không bao giờ bị "kẹt" mãi ở chế độ Offline
                if (!currentNetStatus) {
                    if (millis() - lastWiFiRetry > 30000) {
                        lastWiFiRetry = millis();
                        Serial.println("NET: [Offline] Dang quet va thu ket noi lai WiFi ngam...");
                        WiFi.disconnect();
                        WiFi.begin(); 
                    }
                }
            }

            if (currentNetStatus != lastNetStatus) {
                lastNetStatus = currentNetStatus;
                if (currentNetStatus) {
                    Serial.println("NET: TRUY CAP INTERNET THANH CONG!");
                    xEventGroupSetBits(networkEventGroup, WIFI_CONNECTED_BIT);
                } else {
                    Serial.println("NET: MAT KET NOI INTERNET!");
                    xEventGroupClearBits(networkEventGroup, WIFI_CONNECTED_BIT);
                }
            }
        }

        // --- 4. DÒ SÓNG MESH KHI MẤT MẠNG ---
        EventBits_t bits = xEventGroupGetBits(networkEventGroup);
        if ((bits & WIFI_CONNECTED_BIT) == 0) {
            static uint8_t listen_channel = 1;
            esp_wifi_set_channel(listen_channel, WIFI_SECOND_CHAN_NONE);
            listen_channel++;
            if (listen_channel > 13) listen_channel = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}