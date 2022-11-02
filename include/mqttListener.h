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
#define TONE_SOUND_PATTERN_1 "12300456"
#define TONE_SOUND_PATTERN_2 "604020406"
#define TONE_SOUND_PATTERN_3 "908070605"
#define TONE_SOUND_PATTERN_4 "91919191"
#define TONE_MAX_PATTERN_LENGTH 10
#define DEFAULT_MQTT_LWT_MESSAGE "stopped"
#define MQTT_TOPIC_COMMAND_REQUEST "command"
#define DEFAULT_NOTE_LENGTH_MS 100
#define DEFAULT_NOTE_OCTAVE 5 //0 through 8
#define NOTE_PITCH_HZ 2048
#define REPEAT_LIMIT_MS 4000  //won't process repeated messages unless this much time between them


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
// int notePitchHz[]= 
//   {
//   0,    //silence
//   220, //A   
//   233, //A#
//   247, //B
//   262, //C
//   277, //C#
//   294, //D
//   311, //D#
//   330, //E
//   349, //F
//   370, //F#
//   392, //G
//   415  //G#
//   };



int notePitchHz[12][9]={
//octave0 octave1 octave2 octave3 octave4 octave5 octave6 octave7 octave8
{ 16,     33,     65,     131,    262,    523,    1047,   2093,   4186},   //C   
{ 17,     35,     69,     139,    277,    554,    1109,   2217,   4435},   //C#  
{ 18,     37,     73,     147,    294,    587,    1175,   2349,   4699},   //D   
{ 19,     39,     78,     156,    311,    622,    1245,   2489,   4978},   //D#  
{ 21,     41,     82,     165,    330,    659,    1319,   2637,   5274},   //E   
{ 22,     44,     87,     175,    349,    698,    1397,   2794,   5588},   //F   
{ 23,     46,     93,     185,    370,    740,    1480,   2960,   5920},   //F#  
{ 25,     49,     98,     196,    392,    784,    1568,   3136,   6272},   //G   
{ 26,     52,    104,     208,    415,    831,    1661,   3322,   6645},   //G#  
{ 28,     55,    110,     220,    440,    880,    1760,   3520,   7040},   //A   
{ 29,     58,    117,     233,    466,    932,    1865,   3729,   7459},   //A#  
{ 31,     62,    123,     247,    494,    988,    1976,   3951,   7902}};  //B   

