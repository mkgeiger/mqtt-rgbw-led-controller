#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <EEPROM.h>

// eeprom
#define MQTT_IP_OFFSET         0
#define MQTT_IP_LENGTH        16
#define MQTT_USER_OFFSET      16
#define MQTT_USER_LENGTH      32
#define MQTT_PASSWORD_OFFSET  48
#define MQTT_PASSWORD_LENGTH  32
#define RED_OFFSET            80
#define RED_LENGTH             1
#define GREEN_OFFSET          81
#define GREEN_LENGTH           1
#define BLUE_OFFSET           82
#define BLUE_LENGTH            1
#define WHITE_OFFSET          83
#define WHITE_LENGTH           1

// pins
#define RED_GPIO      5
#define GREEN_GPIO   14
#define BLUE_GPIO    12
#define WHITE_GPIO    0
#define BUTTON_GPIO  13

// access point
#define AP_NAME "ZX-2844"
#define AP_TIMEOUT 300
#define MQTT_PORT 1883

// topics
static char topic_rgb[30] = "/";
static char topic_wht[30] = "/";
static char topic_rgb_fb[30] = "/";
static char topic_wht_fb[30] = "/";

// channel values
static uint8_t red   = 0x00;
static uint8_t green = 0x00;
static uint8_t blue  = 0x00;
static uint8_t white = 0x00;
static uint8_t red_old   = 0x00;
static uint8_t green_old = 0x00;
static uint8_t blue_old  = 0x00;
static uint8_t white_old = 0x00;
static char rgb_str[7];
static char wht_str[4];

// mqtt
IPAddress mqtt_server;
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
char mqtt_ip[MQTT_IP_LENGTH] = "";
char mqtt_user[MQTT_USER_LENGTH] = "";
char mqtt_password[MQTT_PASSWORD_LENGTH] = "";

// wifi
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;
static char mac_str[13];

String readEEPROM(int offset, int len)
{
    String res = "";
    for (int i = 0; i < len; ++i)
    {
      res += char(EEPROM.read(i + offset));
    }
    return res;
}
  
void writeEEPROM(int offset, int len, String value)
{
    for (int i = 0; i < len; ++i)
    {
      if (i < value.length()) {
        EEPROM.write(i + offset, value[i]);
      } else {
        EEPROM.write(i + offset, 0x00);
      }
    }
}
  
void connectToWifi()
{
  Serial.println("Re-Connecting to Wi-Fi...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void onWifiConnect(const WiFiEventStationModeGotIP& event)
{
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event)
{
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  
  mqttClient.subscribe(topic_rgb, 2);
  mqttClient.subscribe(topic_wht, 2);

  mqttClient.publish(topic_rgb, 0, true, rgb_str);
  mqttClient.publish(topic_wht, 0, true, wht_str);
  mqttClient.publish(topic_rgb_fb, 0, false, rgb_str);
  mqttClient.publish(topic_wht_fb, 0, false, wht_str);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  char pl[7];

  strncpy(pl, payload, 6);
  pl[len] = 0;
  Serial.printf("Publish received. Topic: %s Payload: %s\n", topic, pl);

  if (0 == strcmp(topic, topic_rgb))
  {
    char val[3] = {0};

    val[0] = (char)payload[0];
    val[1] = (char)payload[1];
    red = (uint8_t)strtol(val, NULL, 16);
    if (red != red_old)
    {
      analogWrite(RED_GPIO, ((int)red) * 4);
      EEPROM.write(RED_OFFSET, red);   
    }
   
    val[0] = (char)payload[2];
    val[1] = (char)payload[3];
    green = (uint8_t)strtol(val, NULL, 16);
    if (green != green_old)
    {
      analogWrite(GREEN_GPIO, ((int)green) * 4);
      EEPROM.write(GREEN_OFFSET, green);
    }
  
    val[0] = (char)payload[4];
    val[1] = (char)payload[5];
    blue = (uint8_t)strtol(val, NULL, 16);
    if (blue != blue_old)
    {
      analogWrite(BLUE_GPIO, ((int)blue) * 4);
      EEPROM.write(BLUE_OFFSET, blue);
    }

    if ((red != red_old) || (green != green_old) || (blue != blue_old))
    {
      EEPROM.commit();
    }

    red_old = red;
    green_old = green;
    blue_old = blue;
  }
  
  if (0 == strcmp(topic, topic_wht))
  {
    char val[4];

    if (len <= 3)
    {
      strncpy(val, (char*)payload, 3);
      val[len] = 0;
      white = (uint8_t)strtol(val, NULL, 10);
      if (white != white_old)
      {
        analogWrite(WHITE_GPIO, ((int)white) * 4);
        EEPROM.write(WHITE_OFFSET, white);
        EEPROM.commit();
      }
      
      white_old = white;
    }
  }
}

void setup(void)
{
  uint8_t mac[6];
  
  // init UART
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // init EEPROM
  EEPROM.begin(128);

  WiFiManager wifiManager;

  // init button
  pinMode(BUTTON_GPIO, INPUT);

  // check if button is pressed
  if (LOW == digitalRead(BUTTON_GPIO))
  {
    Serial.println("reset wifi settings and restart.");
    wifiManager.resetSettings();
    delay(1000);
    ESP.restart();    
  }
  
  // init WIFI
  readEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH).toCharArray(mqtt_ip, MQTT_IP_LENGTH);
  readEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH).toCharArray(mqtt_user, MQTT_USER_LENGTH);
  readEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH).toCharArray(mqtt_password, MQTT_PASSWORD_LENGTH);
  
  WiFiManagerParameter custom_mqtt_ip("ip", "MQTT ip", mqtt_ip, MQTT_IP_LENGTH);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT user", mqtt_user, MQTT_USER_LENGTH);
  WiFiManagerParameter custom_mqtt_password("passord", "MQTT password", mqtt_password, MQTT_PASSWORD_LENGTH, "type=\"password\"");
  
  wifiManager.addParameter(&custom_mqtt_ip);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  wifiManager.setConfigPortalTimeout(AP_TIMEOUT);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,1,1), IPAddress(192,168,1,1), IPAddress(255,255,255,0));
  if (!wifiManager.autoConnect(AP_NAME))
  {
    Serial.println("failed to connect and restart.");
    delay(1000);
    // restart and try again
    ESP.restart();
  }

  strcpy(mqtt_ip, custom_mqtt_ip.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  
  writeEEPROM(MQTT_IP_OFFSET, MQTT_IP_LENGTH, mqtt_ip);
  writeEEPROM(MQTT_USER_OFFSET, MQTT_USER_LENGTH, mqtt_user);
  writeEEPROM(MQTT_PASSWORD_OFFSET, MQTT_PASSWORD_LENGTH, mqtt_password);
  
  EEPROM.commit();

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  // construct MQTT topics with MAC
  WiFi.macAddress(mac);
  sprintf(mac_str, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  strcat(topic_rgb, mac_str);
  strcat(topic_rgb, "/rgb");
  strcat(topic_wht, mac_str);
  strcat(topic_wht, "/wht"); 
  strcat(topic_rgb_fb, mac_str);
  strcat(topic_rgb_fb, "/rgb_fb");
  strcat(topic_wht_fb, mac_str);
  strcat(topic_wht_fb, "/wht_fb");   

  // read stored channels
  red   = EEPROM.read(RED_OFFSET);
  green = EEPROM.read(GREEN_OFFSET);
  blue  = EEPROM.read(BLUE_OFFSET);
  white = EEPROM.read(WHITE_OFFSET);
  red_old = red;
  green_old = green;
  blue_old = blue;
  white_old = white;

  // set PWM channels
  sprintf(rgb_str, "%02x%02x%02x", red, green, blue);
  sprintf(wht_str, "%d", white);
  analogWrite(RED_GPIO, ((int)red) * 4);
  analogWrite(GREEN_GPIO, ((int)green) * 4);
  analogWrite(BLUE_GPIO, ((int)blue) * 4);   
  analogWrite(WHITE_GPIO, ((int)white) * 4);

  if (mqtt_server.fromString(mqtt_ip))
  {
    char mqtt_id[30] = AP_NAME;

    strcat(mqtt_id, "-");
    strcat(mqtt_id, mac_str);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_server, MQTT_PORT);
    mqttClient.setCredentials(mqtt_user, mqtt_password);
    mqttClient.setClientId(mqtt_id);
  
    connectToMqtt();
  }
  else
  {
    Serial.println("invalid MQTT Broker IP.");
  }
}

void loop(void)
{
  
}
