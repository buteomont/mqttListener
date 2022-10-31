#ifdef ESP32
  #define LED_BUILTIN 33
  #define FLASHLED_PORT 4
  #define SOUNDER_PORT 1
#else
  #define FLASHLED_PORT LED_BUILTIN
  #define SOUNDER_PORT D2
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
#define HOSTNAME_SIZE 25

#define MQTT_CLIENTID_SIZE 25
#define DEFAULT_MQTT_BROKER_PORT 1883
#define MQTT_MAX_TOPIC_SIZE 100
#define MQTT_MAX_MESSAGE_SIZE 15
#define DEFAULT_MQTT_TOPIC "esp8266/mqttListener"
#define MQTT_CLIENT_ID_ROOT "mqttListener"
#define MQTT_TOPIC_RSSI "rssi"
#define MQTT_TOPIC_STATUS "status"
#define DEFAULT_SOUND_PATTERN_1 B10100000
#define DEFAULT_SOUND_PATTERN_2 B11011000
#define DEFAULT_SOUND_PATTERN_3 B11101110
#define DEFAULT_SOUND_PATTERN_4 B11111111
#define TONAL_SOUND_PATTERN_1 "12300456"
#define TONAL_SOUND_PATTERN_2 "604020406"
#define TONAL_SOUND_PATTERN_3 "908070605"
#define TONAL_SOUND_PATTERN_4 "91919191"
#define TONAL_MAX_PATTERN_LENGTH 10
#define DEFAULT_MQTT_LWT_MESSAGE "stopped"
#define MQTT_TOPIC_COMMAND_REQUEST "command"
#define NOTE_LENGTH_MS 100
#define NOTE_PITCH_HZ 2048
#define REPEAT_LIMIT_MS 5000  //won't process repeated messages unless this much time between them


//prototypes
void incomingMqttHandler(char* reqTopic, byte* payload, unsigned int length);
unsigned long myMillis();
bool processCommand(String cmd);
void checkForCommand();
bool connectToWiFi();
void showSettings();
void mqttReconnect(); 
void showSub(char* topic, bool subgood);
void initializeSettings();
void loadSettings();
bool saveSettings();
void serialEvent(); 
void setup(); 
void loop();

//constants
int notePitchHz[]= 
  {
  0,    //silence
  1760, //A   
  1865, //A#
  1976, //B
  2093, //C
  2217, //C#
  2349, //D
  2489, //D#
  2637, //E
  2794, //F
  2960, //F#
  3136, //G
  3322  //G#
  };