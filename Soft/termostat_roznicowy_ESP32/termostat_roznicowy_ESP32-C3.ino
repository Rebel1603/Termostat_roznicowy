#include <Arduino.h>
#include <SuplaDevice.h>
#include <supla/network/esp_wifi.h>
#include <supla/storage/littlefs_config.h>
#include <supla/storage/storage.h>
#include <supla/network/html/device_info.h>
#include <supla/network/html/protocol_parameters.h>
#include <supla/network/html/status_led_parameters.h>
#include <supla/network/html/wifi_parameters.h>
#include <supla/network/html/custom_text_parameter.h>
#include <supla/network/html/custom_parameter.h>
#include <supla/network/html/select_cmd_input_parameter.h>
#include <supla/network/html/text_cmd_input_parameter.h>
#include <supla/network/html/select_input_parameter.h>
#include <supla/network/esp_web_server.h>
#include <supla/device/supla_ca_cert.h>
#include <supla/device/status_led.h>

#include <supla/sensor/ds18b20.h>
#include <supla/control/relay.h>
#include <supla/control/virtual_relay.h>
#include <supla/control/button.h>

// ========== PINY ==========
#define DS18B20_PIN_1 25
#define DS18B20_PIN_2 26
#define RELAY_PIN 27
#define LED_BUILTIN 2
#define CONFIG_BUTTON_PIN 0

// ========== OBIEKTY SUPLI ==========
Supla::ESPWifi wifi;
Supla::LittleFsConfig configSupla;
Supla::EspWebServer suplaServer;
Supla::Device::StatusLed statusLed(LED_BUILTIN, true); // inverted state po dodaniu tej linii miga LED builin

// ========== URZĄDZENIA ==========
Supla::Sensor::DS18B20 *sensorT1 = nullptr;
Supla::Sensor::DS18B20 *sensorT2 = nullptr;
Supla::Control::Relay *relayK1 = nullptr;
Supla::Control::VirtualRelay *tryb = nullptr;
Supla::Control::VirtualRelay *stan = nullptr;
Supla::Control::VirtualRelay *logika = nullptr;

const char PARAM1[] = "param1"; // histereza stopień C
const char PARAM2[] = "param2"; // interwał pomiaru sek

// ========== ZMIENNE ==========
double T1_temp = 0;
double T2_temp = 0;
int suplaStatus = 0;
float histereza_temp;
int32_t histereza = 0;
unsigned long meas_cycle = 1; //  czas w sekundach
bool lastRelayState1 = false;
bool lastRelayState2 = false;
bool lastRelayState3 = false;
bool currentState1 = false;
bool currentState2 = false;
bool currentState3 = false;
bool auto_man = false;
bool on_off = false;
bool lato_zima = false;

static unsigned long tempCheck = 0;
static unsigned long setRelay_time = 0;

// ========== STATUS SUPLA ==========
void status_func(int status, const char *msg) {
 suplaStatus = status;
}

// ========== SETUP ==========
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Sensory
  sensorT1 = new Supla::Sensor::DS18B20(DS18B20_PIN_1); // temp zewnętrzna
  sensorT2 = new Supla::Sensor::DS18B20(DS18B20_PIN_2); // temp wewnętrzna

  // Przekaźnik
  relayK1 = new Supla::Control::Relay(RELAY_PIN, true);  // true = aktywny stan wysoki
  // Przekaźnik wirtualny
  tryb = new Supla::Control::VirtualRelay(); //auto <> manual
  tryb->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);
  tryb->setDefaultStateRestore();  // zapamiętuje stan po restarcie
  stan = new Supla::Control::VirtualRelay(); //Manual: OFF/ON
  stan->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);
  stan->setDefaultStateRestore();  // zapamiętuje stan po restarcie
  logika = new Supla::Control::VirtualRelay(); //odwrócona logika Lato/Zima grzanie/chłodzenie
  logika->setDefaultFunction(SUPLA_CHANNELFNC_POWERSWITCH);
  logika->setDefaultStateRestore();  // zapamiętuje stan po restarcie

  // Przycisk konfiguracyjny hold 
  /* 
  auto cfgButton = new Supla::Control::Button(CONFIG_BUTTON_PIN, true, true);
  cfgButton->setHoldTime(5000);
  cfgButton->addAction(Supla::TOGGLE_CONFIG_MODE, SuplaDevice, Supla::ON_HOLD);
  */

  // Przycisk konfiguracyjny 10x click
  auto cfgButton = new Supla::Control::Button(CONFIG_BUTTON_PIN, true, true);
  cfgButton->setMulticlickTime(800, false);
  cfgButton->addAction(Supla::TOGGLE_CONFIG_MODE, SuplaDevice, Supla::ON_CLICK_10);


  // Elementy konfiguracyjne w panelu SUPLA-CONFIG
  new Supla::Html::DeviceInfo(&SuplaDevice);
  new Supla::Html::WifiParameters;
  new Supla::Html::ProtocolParameters;
  new Supla::Html::StatusLedParameters;
  new Supla::Html::CustomParameter(PARAM1, "Histereza [°C] = 0,1 x ");
  new Supla::Html::CustomParameter(PARAM2, "Interwał pomiaru [sek]");

  // Inicjalizacja SUPLA
  SuplaDevice.setName("Termostat roznicowy ESP32-C3");
  SuplaDevice.setStatusFuncImpl(&status_func);
  SuplaDevice.setSuplaCACert(suplaCACert);
  SuplaDevice.setSupla3rdPartyCACert(supla3rdCACert);
  SuplaDevice.begin();

  int32_t val1 = 0;

  if (Supla::Storage::ConfigInstance()->getInt32(PARAM1, &val1))
  {
    SUPLA_LOG_DEBUG(" **** Param[%s]: %d", PARAM1, val1);
    histereza_temp = val1;       //histereza pomiędzy Temp_out a temp_in
    histereza_temp = histereza_temp * 0.1; //wartość podana w Config to 0.1°C x val1
    histereza =  histereza_temp;
  }
  else
  {
    SUPLA_LOG_DEBUG(" **** Param[%s] is not set", PARAM1);
  }

  int32_t val2 = 0;
  if (Supla::Storage::ConfigInstance()->getInt32(PARAM2, &val2))
  {
    SUPLA_LOG_DEBUG(" **** Param[%s]: %d", PARAM2, val2);
     meas_cycle = val2*1000;  //cykl przełaczania relay K1
  }
  else
  {
    SUPLA_LOG_DEBUG(" **** Param[%s] is not set", PARAM2);
  }
}

// ========== LOOP ==========
void loop() {
  SuplaDevice.iterate();

   // jeśli VR istnieje - sprawdź stan + reakcja
  if ( tryb != nullptr) {
    auto_man = tryb->isOn();
    if (currentState1 != lastRelayState1) {
      lastRelayState1 = currentState1; //tryb auto/manual
      if (currentState1) { auto_man=true; } 
      else { auto_man=false; }
    }
  }
  if (stan != nullptr) {
    currentState2 = stan->isOn();
    if (currentState2 != lastRelayState2) {
      lastRelayState2 = currentState2; //stan w manualu on/off
      if (currentState2) { on_off=true; } 
      else { on_off=false; }
    }
  }
   if (logika != nullptr) {
    currentState3 = logika->isOn();
    if (currentState3 != lastRelayState3) {
      lastRelayState3 = currentState3; //odwrócona logika lato/zima
      if (currentState3) { lato_zima=true; } 
      else { lato_zima=false; }
    }
   }
   // ===== Pomiar temperatury =====
  //static unsigned long tempCheck = 0;
  if (millis() - tempCheck > 3000) {  // co 5 sek odczyt temperatury
    tempCheck = millis();
    if (sensorT1) T1_temp = sensorT1->getValue();    // temp zewnetrzna
    if (sensorT2) T2_temp = sensorT2->getValue();    // temp wewnętrzna
  }
   // ===== Logika termostatu różnicowego =====
  //static unsigned long setRelay_time = 0;
  if(auto_man == false){            // tryb automatyczny zależnie od temperatury i logiki - false
    if (millis() - setRelay_time > meas_cycle) {  // co x sek sprawdź i przełącz - zapobiega niestabilności przekaźnika na granicy
      setRelay_time = millis();
      if(lato_zima == false){         //lato - chłodzenie 
        if (T1_temp < T2_temp - histereza) {    // włącz wentylację gdy na polu chłodniej
          relayK1->turnOn();
        } else {
          relayK1->turnOff();
        }
      }
      if(lato_zima == true){         //zima - grzanie
        if (T1_temp > T2_temp + histereza) {    // włącz wentylację gdy na polu cieplej
          relayK1->turnOn();
        } else {
          relayK1->turnOff();
        }
      }
    }
  }

    if(auto_man == true){   //tryb manualny - włącz lub wyłącz na twardo - true
      if(on_off == false){ relayK1->turnOff();} 
      if(on_off == true){ relayK1->turnOn();} 
    }
}