#ifdef ESP32
  #define LED_BUILTIN 33
  #define FLASHLED_PORT 4
  #define SOUNDER_PORT 1
#else
  #define FLASHLED_PORT LED_BUILTIN
  #define SOUNDER_PORT D1
#endif

#define LED_ON LOW
#define LED_OFF HIGH
#define FLASHLED_ON HIGH
#define FLASHLED_OFF LOW
#define SOUNDER_ON HIGH
#define SOUNDER_OFF LOW
#define WIFI_CONNECTION_ATTEMPTS 150
#define VALID_SETTINGS_FLAG 0xDAB0
#define SSID_SIZE 100
#define PASSWORD_SIZE 50
#define ADDRESS_SIZE 30
#define USERNAME_SIZE 50

#define MQTT_CLIENTID_SIZE 25
#define DEFAULT_MQTT_BROKER_PORT 1883
#define MQTT_MAX_TOPIC_SIZE 50
#define MQTT_MAX_MESSAGE_SIZE 15
#define DEFAULT_MQTT_TOPIC "esp8266/mqttListener/"
#define MQTT_CLIENT_ID_ROOT "mqttListener"
#define MQTT_TOPIC_RSSI "rssi"
#define MQTT_TOPIC_STATUS "status"
#define DEFAULT_SOUND_PATTERN_1 B11000000
#define DEFAULT_SOUND_PATTERN_2 B11001100
#define DEFAULT_SOUND_PATTERN_3 B10101010
#define DEFAULT_MQTT_LWT_MESSAGE "stopped"
#define MQTT_TOPIC_COMMAND_REQUEST "command"
#define NOTE_LENGTH_MS 250
#define NOTE_PITCH_HZ 2048


//prototypes
void incomingMqttHandler(char* reqTopic, byte* payload, unsigned int length);
unsigned long myMillis();
bool processCommand(String cmd);
void checkForCommand();
bool connectToWiFi();
void showSettings();
void reconnect(); 
void showSub(char* topic, bool subgood);
void initializeSettings();
void loadSettings();
bool saveSettings();
void serialEvent(); 
void setup(); 
void loop();


