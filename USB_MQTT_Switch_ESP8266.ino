
/*****************************************************************************************************************************************************************************

 ВНИМАНИЕ!!! питание гребенки VCC 3.3 V
****************************************************************************************************************************************************************************** 
******************************************************************************************************************************************************************************

 После заливки прошивки первый раз включаем модуль и ждем примерно минуту, так как настройки отсудствуют модуль перейдет в режим точки доступа.
 Ищем  точку ( сеть ) Wi-Fi с указаным именем и паролем в настройках кода, по умолчанию подключаемся к точке.
 В браузере заходим по адресу 192.168.4.1 вводим настройки и перегружаем модуль, после етого он будет работать согласно настройкам.
****************************************************************************************************************************************************************************** 

 Для возможности прошивки по сетевому порту OTA,
 необходимо установить последнюю версию python 
 Скачать по ссылке: https://www.python.org/downloads/
 Для коректной работы OTA надо чтоб в имени устройства отсутствовали такие "_" знаки. Пример ArduinoOTA.setHostname("gydota_esp8266_01") - так нельзя
 ArduinoOTA.setHostname("gydota-esp8266-01") - надо так﻿
******************************************************************************************************************************************************************************

*/

#include <pgmspace.h>   // Используем для PROGMEM - говорит компилятору «разместить информацию во flash-памяти», а не в SRAM, 
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <SimpleTimer.h>
#include <WiFiUdp.h>  // Нужна тоже для ОТА
#include <ArduinoOTA.h> // Библиотека для OTA-прошивки
#include "OneButton.h"    // Ссылка на библиотеку https://github.com/mathertel/OneButton

//***********************************************************************
// для того чтобы в сериал монитор выводилась информация нужно разкоментировать ниже строчку #define TestSerialPrint
//#define TestSerialPrint
    // #ifdef TestSerialPrint
    // Код который тут написан будет компелироваться при #define TestSerialPrint
    // #endif
//**********************************************************************    
// 192.168.4.1 Заходим по этому адресу когда в режиме точки доступа

const char* const ssidAP PROGMEM = "ESP_USB_Switch";     // Имя сети в режиме точка доступа
const char* const passwordAP PROGMEM = "1234567890";  // Пароль сети в режиме точка доступа

const uint32_t timeoutWiFi = 60L*1000L; // В течении указанного времени пытаемся соеденится с сетью если не получилось запускается режим точки доступа
const uint32_t timeoutMQTT = 30L*1000L; // Время попытки переподключения MQTT
const uint32_t APtoSTAreconnect = 3L*60L*1000L;    // Время когда в режиме точки доступа переодически пробивает наличие сети чтобы к ней подключиться

//===== Данные для авторизации на WEB странице ====================================================
String strAdminName = "admin";
String adminPassword = "PaSSword";

uint16_t mqttPort = 1883;   // Указываем порт по умолчанию
const uint8_t UsbPin = 4;    // Указываем порт светодиодная лента по умолчанию ( порт на которо весит светодиодная лента )
boolean UsbOnBoot = false;   // Указываем по умолчанию будет ли Led при старте вкл или выкл

const uint8_t pinMqttConectLed = 16; // свентодиод показывает соединение с сервером MQTT, GPIO 16
const uint8_t pinWiFiConectLed = 15; // свентодиод показывает соединение с WiFi, GPIO 15

boolean flagNoConnectMQTT = false;    // Флаг сигнализирующий есть подключение к брокеру или нет ( нужен для установки положение Led взависимости была ли нажата кнопкой механической или в браузере в момент отсудствия подключения к брокеру )
boolean flagPressButton = false;      // Флаг для определения была ли нажата кнопкой механической ( нужен для установки положение Led взависимости подключения к брокеру )
boolean firstStartUsb = true;       // Флаг для того чтобы при старте модуля первый раз сразу считать данные с топиков и неждать время переподключения

const uint8_t buttonSwitch = 14; // GIPIO 14 Пин к которому будет подключена кнопка ( при нажатии притягивается к минусу )
// SW используется как кнопка pin 14  D5 при нажатии выдает 0 при опущеном 1
// Создаем обьект второй кнопки подключенной к пину 
// Параметр true указывает что при нажатии на кнопку на пин пойдет LOW
// Работает через INPUT_PULLUP резистор подтягивать НЕ нужно.
OneButton button(buttonSwitch, true);

bool onOff = false;


const char configSign[4] PROGMEM = { '#', 'R', 'E', 'L' };  // Сигнатура для начала чтения памяти если начало есть то читаем все настройки если нету то не читаем.
const uint8_t maxStrParamLength = 32;    // Максимальная длина задаваемых параметров настроек

          const char* const ssidArg PROGMEM = "ssid";
          const char* const passwordArg PROGMEM = "password";
          const char* const domainArg PROGMEM = "domain";
          const char* const serverArg PROGMEM = "server";
          const char* const portArg PROGMEM = "port";
          const char* const userArg PROGMEM = "user";
          const char* const devName PROGMEM = "name";
          const char* const hostName PROGMEM = "host";
          const char* const mqttpswdArg PROGMEM = "mqttpswd";
          const char* const clientArg PROGMEM = "client";
          const char* const topicUsbArg PROGMEM = "topicUsb";
          const char* const onbootArg PROGMEM = "onboot";
          const char* const rebootArg PROGMEM = "reboot";
          const char* const wifimodeArg PROGMEM = "wifimode";
          const char* const mqttconnectedArg PROGMEM = "mqttconnected";
          const char* const uptimeArg PROGMEM = "uptime";
          const char* const statusUsb PROGMEM = "status";
          const char* const wifiRssi PROGMEM = "wifirssi";

String ssid, password, domain;
String mqttServer, mqttUser, mqttPassword, mqttClient = "ESP_USB", mqttTopicUsb = "/USB"; // Указываем параметры по умолчанию
String nameEspUsb = "ESP USB", aHostname = "ESP8266-USB";  // Указываем параметры по умолчанию

int32_t rssi = 0;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient espClient;
PubSubClient pubsubClient(espClient);

SimpleTimer timer;    // Таймер для подключения к Сети в качестве клиента
SimpleTimer timerRssi;  // Таймер который выводит в WEB интерфейс уровень сигнала Wi-Fi
SimpleTimer timerUptime;  // Таймер такт времени прошедшего от запуска

uint32_t timeValue = 0;  

// Переменная для хранения номера таймера, чтобы можно было его остановить
int16_t timerId;

//==== функция которая запрашивает авторизацию на WEB странице ====================================================
boolean adminAuthenticate() {
  if (adminPassword.length()) {
    if (! httpServer.authenticate(String (strAdminName).c_str(), adminPassword.c_str())) {
      httpServer.requestAuthentication();
      return false;
    }
  }
  return true;
}
//=================================================================================================

// Функция отображения времени от старта включения модуля
String timeStart(uint32_t t){
  
  uint32_t d = t/24/60/60;
  uint32_t h = (t/3600)%24;
  String H = h<10? "0" + String(h) + "h ": String(h) + "h ";
  uint32_t m = (t/60)%60;
  String M = m<10? "0" + String(m) + "m ": String(m) + "m " ;
  uint32_t s = t%60;
  String S = s<10? "0" + String(s) + "s": String(s) + "s";

  return String(d) + "d " + H + M + S;
}

//** EEPROM configuration functions*******************************************

// Фукнкция чтения данных из памяти*****************************
uint16_t readEEPROMString(uint16_t offset, String& str) {
  char buffer[maxStrParamLength + 1];

  buffer[maxStrParamLength] = 0;
  for (uint8_t i = 0; i < maxStrParamLength; i++) {
    if (! (buffer[i] = EEPROM.read(offset + i)))
      break;
  }
  str = String(buffer);

  return offset + maxStrParamLength;
}

// Функция записи данных в память*********************
uint16_t writeEEPROMString(uint16_t offset, const String& str) {
  for (uint8_t i = 0; i < maxStrParamLength; i++) {
    if (i < str.length())
      EEPROM.write(offset + i, str[i]);
    else
      EEPROM.write(offset + i, 0);
  }

  return offset + maxStrParamLength;
}

// Функция чтения конфигураций из памяти*******************
boolean readConfig() {
  uint16_t offset = 0;
  #ifdef TestSerialPrint
  Serial.println(F("Reading config from EEPROM"));
  #endif
  for (uint8_t i = 0; i < sizeof(configSign); i++) {
    char c = pgm_read_byte(configSign + i);
    if (EEPROM.read(offset + i) != c)
      return false;
  }
  offset += sizeof(configSign);
  offset = readEEPROMString(offset, ssid);
  offset = readEEPROMString(offset, password);
  offset = readEEPROMString(offset, domain);
  offset = readEEPROMString(offset, mqttServer);
  offset = readEEPROMString(offset, nameEspUsb);
  offset = readEEPROMString(offset, aHostname);
  EEPROM.get(offset, mqttPort);
  offset += sizeof(mqttPort);
  offset = readEEPROMString(offset, mqttUser);
  offset = readEEPROMString(offset, mqttPassword);
  offset = readEEPROMString(offset, mqttClient);
  offset = readEEPROMString(offset, mqttTopicUsb);
  EEPROM.get(offset, UsbOnBoot);

  return true;
}

// Функция записи конфигураций в память****************
void writeConfig() {
  uint16_t offset = 0;
  #ifdef TestSerialPrint
  Serial.println(F("Writing config to EEPROM"));
  #endif
  for (uint8_t i = 0; i < sizeof(configSign); i++) {
    char c = pgm_read_byte(configSign + i);
    EEPROM.write(offset + i, c);
  }
  offset += sizeof(configSign);
  offset = writeEEPROMString(offset, ssid);
  offset = writeEEPROMString(offset, password);
  offset = writeEEPROMString(offset, domain);
  offset = writeEEPROMString(offset, mqttServer);
  offset = writeEEPROMString(offset, nameEspUsb);
  offset = writeEEPROMString(offset, aHostname);
  EEPROM.put(offset, mqttPort);
  offset += sizeof(mqttPort);
  offset = writeEEPROMString(offset, mqttUser);
  offset = writeEEPROMString(offset, mqttPassword);
  offset = writeEEPROMString(offset, mqttClient);
  offset = writeEEPROMString(offset, mqttTopicUsb);
  EEPROM.put(offset, UsbOnBoot);
  EEPROM.commit();
}
//***********************************************************************************


//*********** Настройки WiFi**********************************

// Настройка подключения как клиент к роутеру*****************************
boolean setupWiFiAsStation() {

  digitalWrite(pinWiFiConectLed, LOW);
  uint32_t maxtime = millis() + timeoutWiFi;
  #ifdef TestSerialPrint
  Serial.print(F("Connecting to "));
  Serial.println(ssid);
  #endif
  WiFi.mode(WIFI_STA);    // Принудительно переводим в режим клиента
  WiFi.begin(ssid.c_str(), password.c_str());   // Подключаем к сети

  while (WiFi.status() != WL_CONNECTED) {   // Проверяем статус соеденения     
    delay(500); 
    #ifdef TestSerialPrint                                                      
    Serial.print(".");
    #endif
    if (millis() >= maxtime) {
      #ifdef TestSerialPrint
      Serial.println(F(" fail!"));
      #endif
      return false;
    }
  }
  digitalWrite(pinWiFiConectLed, HIGH);
  #ifdef TestSerialPrint
  Serial.println();
  Serial.println(F("WiFi connected"));
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  #endif
  
  timer.restartTimer(timerId); // Заставляет указанный таймер начинать отсчет с «сейчас» (Перезапуск таймера), то есть момента, когда вызывается restartTimer. Таймер Обратный вызов не запускается. 
  timer.disable(timerId); // Выключаем таймер по Id.
  
  return true;
}

// Настройка сети как точка доступа*********************************
void setupWiFiAsAP() {
  #ifdef TestSerialPrint
  Serial.print(F("Configuring access point "));
  Serial.println(ssidAP);
  #endif
  
  digitalWrite(pinWiFiConectLed, LOW);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssidAP, passwordAP);
  // Если хотим сделать скрытую сеть тв режиме точка доступа то ниже приведен пример такого подключения
                 // Параметры ssid, password, channel, hide
  // WiFi.softAP(ssidAP, passwordAP, 1, 1);
  #ifdef TestSerialPrint
  Serial.print(F("IP address: "));
  Serial.println(WiFi.softAPIP());
  #endif
}

// Настройки Wi-Fi **********************************************
void setupWiFi() {
  if ((! ssid.length()) || (! setupWiFiAsStation())) { // Если имя сети не указано или не можем подключится к сети, то  запускаем в режиме точки доступа.
    setupWiFiAsAP();
  }
  if (domain.length()) {  // Если определен домен мднс то попытка его зарегестрировать.
    if (MDNS.begin(domain.c_str())) {
      MDNS.addService("http", "tcp", 80);
      #ifdef TestSerialPrint
      Serial.println(F("mDNS responder started"));
      #endif
    } else {
      #ifdef TestSerialPrint
      Serial.println(F("Error setting up mDNS responder!"));
      #endif
    }
  }

  httpServer.begin();   // Запускаем сервер
  #ifdef TestSerialPrint
  Serial.println(F("HTTP server started (use '/update' url to OTA update)"));
  #endif
}
//*******************************************************************************



//**** Настройки HTTP server functions*********************************************
String quoteEscape(const String& str) {
  String result = "";
  int16_t start = 0, pos;

  while (start < str.length()) {
    pos = str.indexOf('"', start);
    if (pos != -1) {
      result += str.substring(start, pos) + F("&quot;");
      start = pos + 1;
    } else {
      result += str.substring(start);
      break;
    }
  }

  return result;
}

// Главная страница *****************************************************
void handleRoot() {

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
 
  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>ESP USB Switch</title>\n\
  <script type=\"text/javascript\">\n\
    function openUrl(url) {\n\
      var request = new XMLHttpRequest();\n\
      request.open('GET', url, true);\n\
      request.send(null);\n\
    }\n\
    function refreshData() {\n\
      var request = new XMLHttpRequest();\n\
      request.open('GET', '/data', true);\n\
      request.onreadystatechange = function() {\n\
        if (request.readyState == 4) {\n\
          var data = JSON.parse(request.responseText);\n\
          document.getElementById('");
  message += FPSTR(wifimodeArg);
  message += F("').innerHTML = data.");
  message += FPSTR(wifimodeArg);
  message += F(";\n\
          document.getElementById('");
  message += FPSTR(wifiRssi);
  message += F("').innerHTML = data.");
  message += FPSTR(wifiRssi);
  message += F(";\n\
          document.getElementById('");
  message += FPSTR(mqttconnectedArg);
  message += F("').innerHTML = (data.");
  message += FPSTR(mqttconnectedArg);
  message += F(" != true ? \"not \" : \"\") + \"connected\";\n\
          document.getElementById('");
  message += FPSTR(uptimeArg);
  message += F("').innerHTML = data.");
  message += FPSTR(uptimeArg);
  message += F(";\n\
          document.getElementById('");
  message += FPSTR(statusUsb);
  message += F("').innerHTML = data.");
  message += FPSTR(statusUsb);
  message += F(";\n\
        }\n\
      }\n\
      request.send(null);\n\
    }\n\
    setInterval(refreshData, 500);\n\
  </script>\n\
</head>\n\
<body>\n\
  <form>\n\
    <h3>"); 
  message += nameEspUsb;
  message += F("</h3>\n\
    <p>\n\
    WiFi mode: <span id=\"");
  message += FPSTR(wifimodeArg);
  message += F("\">?</span><br/>\n\
    WiFi Connection: <span id=\"");
  message += FPSTR(wifiRssi);
  message += F("\"> ?</span> dBm<br/>\n\
    MQTT broker: <span id=\"");
  message += FPSTR(mqttconnectedArg);
  message += F("\">?</span><br/>\n\
    Uptime: <span id=\"");
  message += FPSTR(uptimeArg);
  message += F("\">0</span><br/>\n\
  USB Switch (current status): <span id=\"");
  message += FPSTR(statusUsb);
  message += F("\">?</span><br/>\</p>\n\
    <p>\n\
    <input type=\"button\" value=\"WiFi Setup\" onclick=\"location.href='/wifi';\" />\n\
    <input type=\"button\" value=\"MQTT Setup\" onclick=\"location.href='/mqtt';\" />\n\
    <input type=\"button\" value=\"USB Setup\" onclick=\"location.href='/Usb';\" />\n\
    <input type=\"button\" value=\"Device name\" onclick=\"location.href='/name';\" />\n\
    <input type=\"button\" value=\"Reboot!\" onclick=\"if (confirm('Are you sure to reboot?')) location.href='/reboot';\" />\n\
  </form>\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}


// Страница настройки Wi-Fi *****************************************************************
void handleWiFiConfig() {

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
 
  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>WiFi Setup</title>\n\
</head>\n\
<body>\n\
  <form name=\"wifi\" method=\"get\" action=\"/store\">\n\
    <h3>WiFi Setup</h3>\n\
    SSID:<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(ssidArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(ssid);
  message += F("\" />\n\
    <br/>\n\
    Password:<br/>\n\
    <input type=\"password\" name=\"");
  message += FPSTR(passwordArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(password);
  message += F("\" />\n\
    <br/>\n\
    mDNS domain:<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(domainArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(domain);
  message += F("\" />\n\
    .local (leave blank to ignore mDNS)\n\
    <p>\n\
    <input type=\"submit\" value=\"Save\" />\n\
    <input type=\"hidden\" name=\"");
  message += FPSTR(rebootArg);
  message += F("\" value=\"1\" />\n\
  </form>\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}

// Страница настройки имени устройства ************************************************
void deviceName(){

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
 
    String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>Device name</title>\n\
</head>\n\
<body>\n\
  <form name=\"name\" method=\"get\" action=\"/store\">\n\
    <h3>Device name</h3>\n\
    Device name:<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(devName);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(nameEspUsb);
  message += F("\" />\n\ 
    <br/>\n\
    Host name (device name on the network):<br/>(For OTA to work, you can not use the character \"_\" in the name)<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(hostName);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(aHostname);
  message += F("\" />\n\         
    <p>\n\
    <input type=\"submit\" value=\"Save\" />\n\
    <input type=\"hidden\" name=\"");
  message += FPSTR(rebootArg);
  message += F("\" value=\"0\" />\n\
  </form>\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}

// Страница настройки MQTT ******************************************************************
void handleMQTTConfig() {

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
 
  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>MQTT Setup</title>\n\
</head>\n\
<body>\n\
  <form name=\"mqtt\" method=\"get\" action=\"/store\">\n\
    <h3>MQTT Setup</h3>\n\
    Server:<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(serverArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(mqttServer);
  message += F("\" onchange=\"document.mqtt.reboot.value=1;\" />\n\
    (leave blank to ignore MQTT)\n\
    <br/>\n\
    Port (default 1883):<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(portArg);
  message += F("\" maxlength=5 value=\"");
  message += String(mqttPort);
  message += F("\" onchange=\"document.mqtt.reboot.value=1;\" />\n\
    <br/>\n\
    User (if authorization is required on MQTT server):<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(userArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(mqttUser);
  message += F("\" />\n\
    (leave blank to ignore MQTT authorization)\n\
    <br/>\n\
    Password:<br/>\n\
    <input type=\"password\" name=\"");
  message += FPSTR(mqttpswdArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(mqttPassword);
  message += F("\" />\n\
    <br/>\n\
    Client (device name):<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(clientArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(mqttClient);
  message += F("\" />\n\
    <br/>\n\
    Topic USB Switch (format MQTT => /Topic USB):<br/>\n\
    <input type=\"text\" name=\"");
  message += FPSTR(topicUsbArg);
  message += F("\" maxlength=");
  message += String(maxStrParamLength);
  message += F(" value=\"");
  message += quoteEscape(mqttTopicUsb);
  message += F("\" />\n\
      <br/>\n\                          
    <p>\n\
    <input type=\"submit\" value=\"Save\" />\n\
    <input type=\"hidden\" name=\"");
  message += FPSTR(rebootArg);
  message += F("\" value=\"0\" />\n\
  </form>\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}

// Страница настройки Led *********************************************************
void handleUsbConfig() {

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
 
  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>USB Setup</title>\n\
</head>\n\
<body>\n\
  <form name=\"Usb\" method=\"get\" action=\"/store\">\n\
    <h3>USB Setup</h3>\n\
    State on boot:<br/>\n\
    <input type=\"radio\" name=\"");
  message += FPSTR(onbootArg);
  message += F("\" value=\"1\" ");
  if (UsbOnBoot)
    message += F("checked");
  message += F("/>On\n\
    <input type=\"radio\" name=\"");
  message += FPSTR(onbootArg);
  message += F("\" value=\"0\" ");
  if (! UsbOnBoot)
    message += F("checked");
  message += F("/>Off\n\
    <p>\n\
    <input type=\"submit\" value=\"Save\" />\n\
    <input type=\"hidden\" name=\"");
  message += FPSTR(rebootArg);
  message += F("\" value=\"1\" />\n\
  </form>\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}

// Считываем в переменные если заполнились или изменились поля и нажали кнопку сохронить*****************************
void handleStoreConfig() {
  String argName, argValue;

  #ifdef TestSerialPrint
  Serial.print(F("/store("));
  #endif
  for (uint8_t i = 0; i < httpServer.args(); i++) {
    #ifdef TestSerialPrint
    if (i){
      Serial.print(F(", "));
    }
    #endif
    argName = httpServer.argName(i);
    #ifdef TestSerialPrint
    Serial.print(argName);
    Serial.print(F("=\""));
    #endif
    argValue = httpServer.arg(i);
    #ifdef TestSerialPrint
    Serial.print(argValue);
    Serial.print(F("\""));
    #endif

    if (argName.equals(FPSTR(ssidArg))) {
      ssid = argValue;
    } else if (argName.equals(FPSTR(passwordArg))) {
      password = argValue;
    } else if (argName.equals(FPSTR(domainArg))) {
      domain = argValue;
    } else if (argName.equals(FPSTR(serverArg))) {
      mqttServer = argValue;
    } else if (argName.equals(FPSTR(portArg))) {
      mqttPort = argValue.toInt();
    } else if (argName.equals(FPSTR(userArg))) {
      mqttUser = argValue;
    } else if (argName.equals(FPSTR(devName))) {
      nameEspUsb = argValue;
    } else if (argName.equals(FPSTR(hostName))) {
      aHostname = argValue;
    } else if (argName.equals(FPSTR(mqttpswdArg))) {
      mqttPassword = argValue;
    } else if (argName.equals(FPSTR(clientArg))) {
      mqttClient = argValue;
    } else if (argName.equals(FPSTR(topicUsbArg))) {
      mqttTopicUsb = argValue;
    } else if (argName.equals(FPSTR(onbootArg))) {
      UsbOnBoot = argValue.toInt();
    }
  }
  #ifdef TestSerialPrint
  Serial.println(F(")"));
  #endif

  writeConfig(); // Сохраняем данные в память

// Сообщение о том что все сохранено и через 5 сек возвращаемся на главную страницу 

  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>Store Setup</title>\n\
  <meta http-equiv=\"refresh\" content=\"5; /index.html\">\n\
</head>\n\
<body>\n\
  Configuration stored successfully.\n");
  if (httpServer.arg(rebootArg) == "1")
    message += F("  <br/>\n\
  <i>You must reboot module to apply new configuration!</i>\n");
  message += F("  <p>\n\
  Wait for 5 sec. or click <a href=\"/index.html\">this</a> to return to main page.\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);
}

//  проверяем нажатие перезагрузка. **************************************************
void handleReboot() {

 //=== Проверка авторизвции на WEB странице ==========
  if (! adminAuthenticate()) return;
 //===================================================
  #ifdef TestSerialPrint
  Serial.println(F("/reboot()"));
  #endif

  String message =
F("<!DOCTYPE html>\n\
<html>\n\
<head>\n\
  <title>Rebooting</title>\n\
  <meta http-equiv=\"refresh\" content=\"5; /index.html\">\n\
</head>\n\
<body>\n\
  Rebooting...\n\
</body>\n\
</html>");

  httpServer.send(200, F("text/html"), message);

  ESP.restart();
}

// Обновление данных на главной странице ****************************************
void handleData() {
  String message = F("{\"");
  message += FPSTR(wifimodeArg);
  message += F("\":\"");
  switch (WiFi.getMode()) {
    case WIFI_OFF:
      message += F("OFF");
      break;
    case WIFI_STA:
      message += F("Station (Client)");
      break;
    case WIFI_AP:
      message += F("Access Point");
      break;
    case WIFI_AP_STA:
      message += F("Hybrid (AP+STA)");
      break;
    default:
      message += F("Unknown!");
  }
  message += F("\",\"");
  message += FPSTR(wifiRssi);
  message += F("\":\" -");  
  message += String(rssi);
  message += F("\",\"");
  message += FPSTR(mqttconnectedArg);
  message += F("\":");
  if (pubsubClient.connected())
    message += F("true");
  else
    message += F("false");
  message += F(",\"");
  message += FPSTR(uptimeArg);
  
  message += F("\":\"");  // начало для буквенного либо знакового поля
                 message += timeStart( timeValue );
  message += F("\",\"");
  message += FPSTR(statusUsb);
  message += F("\":\"");
  if (onOff)
    message += F("ON");
  else
    message += F("OFF");
    
  message += F("\"");  // конец для буквиного либо знакового поля

  message += F("}");

                      // Строка которую формируем для парсинга должна быть следующего характера:
                      // {"wifimode":"Station","mqttconnected":true,"uptime":"0d 00h 00m 02s","Led":false}
                      // Serial.println(message);  // Для отладки
                      
  httpServer.send(200, F("text/html"), message);
}


// *********** Функции для работы с MQTT functions  **********************************************************

// Функция Калбек *****************************
void mqttCallback(char* topic, uint8_t* payload, uint32_t length) {
  #ifdef TestSerialPrint
  Serial.print(F("MQTT message arrived ["));
  Serial.print(topic);
  Serial.print(F("] "));
  for (int16_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  #endif
  
   char* topicBody = topic; // Skip "/ClientName" from topic
  
  if (! strncmp(topicBody, mqttTopicUsb.c_str(), mqttTopicUsb.length())) {
    switch ((char)payload[0]) {
      
      case '0':
        digitalWrite(UsbPin, LOW);
        onOff=false;
        break;
      case '1':
      
        digitalWrite(UsbPin, HIGH);
        onOff=true;
        break;
        
      case 'T':
        if( onOff ){
             digitalWrite(UsbPin, LOW);
             onOff=false;
        }
        else{
             digitalWrite(UsbPin, HIGH);
             onOff=true;
        }
         mqtt_publish(pubsubClient, mqttTopicUsb, String(onOff), true);  
         break;
                
      default:
        boolean Usb = false;
        mqtt_publish(pubsubClient, String(topic), String(Usb), true);
        
    }
  } 
  else {
    #ifdef TestSerialPrint
    Serial.println(F("Unexpected topic!"));
    #endif
  }
}

// Переподключение  *******************************************

bool mqttReconnect() {
  
  static uint32_t lastTime;
  boolean result = false;
  
  if ( (millis() > lastTime + timeoutMQTT) || firstStartUsb ) {
    firstStartUsb = false;
    #ifdef TestSerialPrint
    Serial.print(F("Attempting MQTT connection..."));
    #endif
    digitalWrite(pinMqttConectLed, HIGH);
            if (mqttUser.length())
              result = pubsubClient.connect(mqttClient.c_str(), mqttUser.c_str(), mqttPassword.c_str());
            else
              result = pubsubClient.connect(mqttClient.c_str());
    digitalWrite(pinMqttConectLed, LOW);
    if (result) {
      #ifdef TestSerialPrint
      Serial.println(F(" connected"));
      #endif
      digitalWrite(pinMqttConectLed, HIGH);  // При соеденении с брокером включаем светодиод индикатор
      // Resubscribe

              // При востановлении связи с брокером
              // Если при потере связи или отсудствии связи с брокером менялось положение Led кнопкой механической то тогда записываем в брокер текущее состояние Led.
              // Если при отсудствии связи кнопкой механической положение Led не менялось тогда подтягиваем с брокера последнее положение Led
              if( flagNoConnectMQTT && flagPressButton ){
                  // Отправляем на брокер текущее состояние Led ( независимо было оно вкл или выкл на последнем соеденении с брокером )
                  boolean Usb = onOff;
                    String topicUsb;
                    topicUsb += mqttTopicUsb;
                  mqtt_publish(pubsubClient, topicUsb, String(Usb), true);
              }
      flagPressButton = false;
      flagNoConnectMQTT = false; 
              
      String topicUsb;
      topicUsb += mqttTopicUsb;
      bool resultUsb = mqtt_subscribe(pubsubClient, topicUsb);

      result=(resultUsb);
      
    } else {
      digitalWrite(pinMqttConectLed, LOW);   // При отсудствии соеденения с брокером выключаем светодиод индикатор
      #ifdef TestSerialPrint
      Serial.print(F(" failed, rc="));
      Serial.println(pubsubClient.state());
      #endif
      flagNoConnectMQTT = true;
    }
    lastTime = millis();
  }

  return result;
}

 // Подписываемся на топик ***********************************************
 
bool mqtt_subscribe(PubSubClient& client, const String& topic) {
  #ifdef TestSerialPrint
  Serial.print(F("Subscribing to "));
  Serial.println(topic);
  #endif

  return client.subscribe(topic.c_str());
}

// Публикуем топик ****************************************************

bool mqtt_publish(PubSubClient& client, const String& topic, const String& value, boolean saveValueServer) {
  #ifdef TestSerialPrint
  Serial.print(F("Publishing topic "));
  Serial.print(topic);
  Serial.print(F(" = "));
  Serial.println(value);
  #endif

  return client.publish(topic.c_str(), value.c_str(), saveValueServer);  // Третий пораметр сохранять на сервере даные или нет ( истина сохранять ложь нет )
}

// Переключение Led **************************************************

void buttonPush() {

  flagPressButton = true; // При отсудствии связи с брокером если меняли положение Led кнопкой механической  
  onOff=!onOff;
   onOff ? digitalWrite(UsbPin, HIGH) : digitalWrite(UsbPin, LOW);
    if (mqttServer.length() && pubsubClient.connected()) {
        String topicUsb;
        topicUsb += mqttTopicUsb;
      mqtt_publish(pubsubClient, topicUsb, String(onOff), true);
    }
}

void wifiRssiNow(){
  //======= Определения уровня сигнала сети =============================================================================================================================      
     // Уровень сигнала к подключенному роутеру ( возвращает данные в dBm отрицательное значение )
     if (((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) && (WiFi.status() == WL_CONNECTED)){  
         rssi = WiFi.RSSI() * (-1);
     }
     else rssi = 0;
     //  Идеальным Wi-Fi сигналом считается уровень от-60 dBm до -65 dBm. Все что выше -60 dBm (например -45 dBm) — это слишком мощный сигнал, все что ниже -80 dBm (например -87 dBm) — слишком слабый сигнал.      
  //======================================================================================================================================================================
}
void TimeUpStart(){
  timeValue++;
}  

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
  #ifdef TestSerialPrint
    Serial.begin(115200);
  #endif
  
  timerId = timer.setInterval( APtoSTAreconnect, setupWiFi );
  timerRssi.setInterval( 1L*1000L, wifiRssiNow );
  timerUptime.setInterval(1000L, TimeUpStart );
  
  
    // Действия для первой кнопки вызывает функцию указаную в качестве параметра.
  button.attachClick(buttonPush);                   // При кратковременном нажатии вызывает функцию которая описана ниже
  //button.attachDoubleClick(buttonPush);             // При двойном нажатии вызывает функцию которая описана ниже 
  button.attachLongPressStart(buttonPush);     // При зажатии кнопки ( при старте ) вызывается функция которая описана ниже
  //button.attachLongPressStop(buttonPush);       // При зажатии кнопки ( в конце когда ее отпустим ) вызывается функция которая описана ниже
  //button.attachDuringLongPress(buttonPush);         // При зажатии кнопки ( между стартом и того когда отпустим ) вызывается функция которая описана ниже
  
  pinMode(pinMqttConectLed, OUTPUT);
  digitalWrite(pinMqttConectLed, LOW);

    pinMode(pinWiFiConectLed, OUTPUT);
  digitalWrite(pinWiFiConectLed, LOW);
  
  //EEPROM.begin(4096);
    EEPROM.begin(1024);
    
  if (! readConfig()){ // Читаем из памяти EEPROM все настройки и данные.
    #ifdef TestSerialPrint
      Serial.println(F("EEPROM is empty!"));
    #endif
  }
  
  ArduinoOTA.setHostname( aHostname.c_str() ); // Задаем имя сетевого порта для OTA-прошивки
  ArduinoOTA.setPassword((const char *)"0000"); // Задаем пароль доступа для удаленной прошивки
  ArduinoOTA.begin(); // Инициализируем OTA
  
  WiFi.hostname (aHostname);    // Задаем имя хоста.
  

  pinMode( UsbPin, OUTPUT );
  
  digitalWrite(UsbPin, UsbOnBoot);
  
  setupWiFi();

  httpUpdater.setup(&httpServer);
  httpServer.onNotFound([]() {
    httpServer.send(404, F("text/plain"), F("FileNotFound"));
  });
  httpServer.on("/", handleRoot);
  httpServer.on("/index.html", handleRoot);
  httpServer.on("/wifi", handleWiFiConfig);
  httpServer.on("/mqtt", handleMQTTConfig);
  httpServer.on("/Usb", handleUsbConfig);
  httpServer.on("/store", handleStoreConfig);
  httpServer.on("/reboot", handleReboot);
  httpServer.on("/data", handleData);
  httpServer.on("/name", deviceName);

  if (mqttServer.length()) {
    pubsubClient.setServer(mqttServer.c_str(), mqttPort);
    pubsubClient.setCallback(mqttCallback);
  }

}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void loop() {

        yield();  // Вызов этой функции передает управление другим задачам. В идеале, вызов yield() должен осуществляться в функциях, выполнение которых занимает некоторое время.
         
        ArduinoOTA.handle(); // Всегда готовы к OTA-прошивки
        
        timer.run(); 
        timerRssi.run();
        timerUptime.run();
        
        
        // Опрос кнопок:
        button.tick();
           
  if (((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) && (WiFi.status() != WL_CONNECTED)) { 
    setupWiFi();
  }
  if ((WiFi.getMode() == WIFI_AP) ){
    digitalWrite(pinWiFiConectLed, LOW);
    timer.enable(timerId);  // Включаем таймер по Id.
  }
  
  httpServer.handleClient();

  if (mqttServer.length() && ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA))) { 
    if (! pubsubClient.connected())
      mqttReconnect();
    if (pubsubClient.connected())
      pubsubClient.loop();
  }
  else digitalWrite(pinMqttConectLed, LOW);
  
  /////////////////////////////////////////////
delay(5);   
//////////////////////////////////////////////////////////////////////////////////////////////////
}
