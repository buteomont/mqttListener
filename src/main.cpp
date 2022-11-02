/*
 * Program to sound a buzzer with a pattern assigned to different mqtt messages.
 * By David E. Powell 
 *
 * Subscribes to an MQTT topic and when the target message is received, 
 * activates the sounder for a predermined pattern.
 * 
 * Configuration is done via serial connection or by MQTT message.
 *  
 * **** Note to self: To erase the entire flash chip in PlatformIO, open
 * **** a terminal and type "pio run -t erase"
 */ 
#include <Arduino.h>
#include <PubSubClient.h> 
#include <EEPROM.h>
#include <pgmspace.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#ifdef ESP32
  #include <Tone32.h>
  #include <ESPAsync_WiFiManager.h>
  #endif

#include "mqttListener.h"

char *stack_start;// initial stack size

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// These are the settings that get stored in EEPROM.  They are all in one struct which
// makes it easier to store and retrieve.
typedef struct 
  {
  unsigned int validConfig=0; 
  char ssid[SSID_SIZE+1] = "";
  char wifiPassword[PASSWORD_SIZE+1] = "";
  char brokerAddress[ADDRESS_SIZE+1]="";
  int brokerPort=DEFAULT_MQTT_BROKER_PORT;
  char mqttUsername[USERNAME_SIZE+1]="";
  char mqttUserPassword[PASSWORD_SIZE+1]="";
  char mqttTopic1[MQTT_MAX_TOPIC_SIZE+1]="";
  char mqttTopic2[MQTT_MAX_TOPIC_SIZE+1]="";
  char mqttTopic3[MQTT_MAX_TOPIC_SIZE+1]="";
  char mqttTopic4[MQTT_MAX_TOPIC_SIZE+1]="";
  char mqttMessage1[MQTT_MAX_MESSAGE_SIZE+1]="";
  char mqttMessage2[MQTT_MAX_MESSAGE_SIZE+1]="";
  char mqttMessage3[MQTT_MAX_MESSAGE_SIZE+1]="";
  char mqttMessage4[MQTT_MAX_MESSAGE_SIZE+1]="";
  char soundPattern1[TONE_MAX_PATTERN_LENGTH+1]=TONE_SOUND_PATTERN_1;
  char soundPattern2[TONE_MAX_PATTERN_LENGTH+1]=TONE_SOUND_PATTERN_2;
  char soundPattern3[TONE_MAX_PATTERN_LENGTH+1]=TONE_SOUND_PATTERN_3;
  char soundPattern4[TONE_MAX_PATTERN_LENGTH+1]=TONE_SOUND_PATTERN_4;
  char mqttLWTMessage[MQTT_MAX_MESSAGE_SIZE+1]="";
  char commandTopic[MQTT_MAX_TOPIC_SIZE+1]=DEFAULT_MQTT_TOPIC;
  boolean debug=false;
  char mqttClientId[MQTT_CLIENTID_SIZE+1]=""; //will be the same across reboots
  unsigned int noteLengthMs=DEFAULT_NOTE_LENGTH_MS;
  int noteOctave=DEFAULT_NOTE_OCTAVE; //can be 0 though 8
  } conf;

conf settings; //all settings in one struct makes it easier to store in EEPROM
boolean settingsAreValid=false;

String commandString = "";     // a String to hold incoming commands from serial
bool commandComplete = false;  // goes true when enter is pressed

boolean messageWasReceived=false;

void printStackSize(char id)
  {
  char stack;
  Serial.print(id);
  Serial.print (F(": stack size "));
  Serial.println (stack_start - &stack);
  }

char* fixup(char* rawString, const char* field, const char* value)
  {
  String rs=String(rawString);
  rs.replace(field,String(value));
  strcpy(rawString,rs.c_str());
  printStackSize('F');
  return rawString;
  }

/************************
 * Do the MQTT thing
 ************************/

boolean publish(char* topic, const char* msg, boolean retain)
  {
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(msg);
  return mqttClient.publish(topic,msg,retain); 
  }


void beep(const char* pattern)
  {
  char* endPtr; //needed for strtol function, not used in code
  int patLen=(int)strlen(pattern);
  if (settings.debug)
    {
    Serial.print("Beeping ");
    Serial.println(pattern);
    Serial.print("Pattern has ");
    Serial.print(patLen);
    Serial.println(" notes");
    }
  for (int i=0;i<patLen;i++)
    {
    char tmp[2]="\0";
    strncpy(tmp,&pattern[i],1);
    int noteIndex=(int)strtol(tmp,&endPtr,16);
    if (noteIndex>0 && noteIndex<=12)
      {
      digitalWrite(FLASHLED_PORT,FLASHLED_ON);
      if (settings.debug)
        {
        Serial.print(noteIndex, HEX);
        Serial.print(" ");
        }
      tone(SOUNDER_PORT,notePitchHz[noteIndex][settings.noteOctave]);
      delay(settings.noteLengthMs);
      noTone(SOUNDER_PORT); //tone doesn't block, so we need to do it this way
      digitalWrite(FLASHLED_PORT,FLASHLED_OFF);
      }
    else
      {
      if (settings.debug)
        Serial.print(". ");
      delay(settings.noteLengthMs);
      }
    delay(settings.noteLengthMs/2);
    }
  if (settings.debug)
    Serial.println("");
  }


/**
 * Handler for incoming MQTT messages.  The payload is the command to perform. 
 * The MQTT response message topic sent is the incoming topic plus the command.
 * The incoming message should be one of mqttMessage1, mqttMessage2, mqttMessage3,
 * or one of the implemented commands.
 * 
 * Some of the devices that send these MQTT messages do so rapid-fire with repeats,
 * I guess just to make sure at least one gets through.  This code filters out all
 * but the first one, with at least 5 seconds required between one and the previous
 * for it to be announced.
 */
void incomingMqttHandler(char* reqTopic, byte* payload, unsigned int length) 
  {
  static unsigned long noRepeat1=millis(); //these will be set to the current uptime counter plus 
  static unsigned long noRepeat2=millis();
  static unsigned long noRepeat3=millis();
  static unsigned long noRepeat4=millis();

  if (settings.debug)
    {
    Serial.println("====================================> Callback works.");
    }
  payload[length]='\0'; //this should have been done in the caller code, shouldn't have to do it here
  char charbuf[100];
  sprintf(charbuf,"%s",payload);
  const char* response;
  char settingsResp[1000];

  if (settings.debug)
    {
    Serial.print("========>Topic is \"");
    Serial.print(reqTopic);
    Serial.println("\"");
    Serial.print("========>Payload is \"");
    Serial.print(charbuf);
    Serial.println("\".");
    }

  boolean needRestart=false;
  if (strcmp(charbuf,"settings")==0 &&
      strcmp(reqTopic,settings.commandTopic)==0) //special case, send all settings
    {
    strcpy(settingsResp,"\nssid=");
    strcat(settingsResp,settings.ssid);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"wifipass=");
    strcat(settingsResp,settings.wifiPassword);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"broker=");
    strcat(settingsResp,settings.brokerAddress);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"brokerPort=");
    strcat(settingsResp,String(settings.brokerPort).c_str());
    strcat(settingsResp,"\n");
    strcat(settingsResp,"userName=");
    strcat(settingsResp,settings.mqttUsername);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"userPass=");
    strcat(settingsResp,settings.mqttUserPassword);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"topic1=");
    strcat(settingsResp,settings.mqttTopic1);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"topic2=");
    strcat(settingsResp,settings.mqttTopic2);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"topic3=");
    strcat(settingsResp,settings.mqttTopic3);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"topic4=");
    strcat(settingsResp,settings.mqttTopic4);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"lwtMessage=");
    strcat(settingsResp,settings.mqttLWTMessage);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"message1=");
    strcat(settingsResp,settings.mqttMessage1);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"message2=");
    strcat(settingsResp,settings.mqttMessage2);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"message3=");
    strcat(settingsResp,settings.mqttMessage3);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"message4=");
    strcat(settingsResp,settings.mqttMessage4);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"soundPattern1=");
    strcat(settingsResp,settings.soundPattern1);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"soundPattern2=");
    strcat(settingsResp,settings.soundPattern2);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"soundPattern3=");
    strcat(settingsResp,settings.soundPattern3);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"soundPattern4=");
    strcat(settingsResp,settings.soundPattern4);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"debug=");
    strcat(settingsResp,settings.debug?"true":"false");
    strcat(settingsResp,"\n");
    strcat(settingsResp,"commandTopic=");
    strcat(settingsResp,settings.commandTopic);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"noteLengthMs=");
    strcat(settingsResp,String(settings.noteLengthMs).c_str());
    strcat(settingsResp,"\n");
    strcat(settingsResp,"noteOctave=");
    strcat(settingsResp,String(settings.noteOctave).c_str());
    strcat(settingsResp,"\n");
    strcat(settingsResp,"MQTT client ID=");
    strcat(settingsResp,settings.mqttClientId);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"IP Address=");
    strcat(settingsResp,WiFi.localIP().toString().c_str());
    response=settingsResp;
    }
  else if (strcmp(charbuf,"status")==0 &&
      strcmp(reqTopic,settings.commandTopic)==0) //report that we're alive
    {
    strcpy(settingsResp,"Ready at ");
    strcat(settingsResp,WiFi.localIP().toString().c_str());
    response=settingsResp;
    }   //check for beep requests
  else if (strlen(settings.mqttMessage1)>0 
      && strcmp(charbuf,settings.mqttMessage1)==0
      && strcmp(reqTopic,settings.mqttTopic1)==0)
    {
    messageWasReceived=true;
    if (millis()>noRepeat1)
      beep(settings.soundPattern1);
    noRepeat1=millis()+REPEAT_LIMIT_MS; //can't do it again for 5 seconds
    response="OK";
    }
  else if (strlen(settings.mqttMessage2)>0 
      && strcmp(charbuf,settings.mqttMessage2)==0
      && strcmp(reqTopic,settings.mqttTopic2)==0)
    {
    messageWasReceived=true;
    if (millis()>noRepeat2)
      beep(settings.soundPattern2);
    noRepeat2=millis()+REPEAT_LIMIT_MS; //can't do it again for 5 seconds
    response="OK";
    }
  else if (strlen(settings.mqttMessage3)>0 
      && strcmp(charbuf,settings.mqttMessage3)==0
      && strcmp(reqTopic,settings.mqttTopic3)==0)
    {
    messageWasReceived=true;
    if (millis()>noRepeat3)
      beep(settings.soundPattern3);      
    noRepeat3=millis()+REPEAT_LIMIT_MS; //can't do it again for 5 seconds
    response="OK";
    }
  else if (strlen(settings.mqttMessage4)>0 
      && strcmp(charbuf,settings.mqttMessage4)==0
      && strcmp(reqTopic,settings.mqttTopic4)==0)
    {
    messageWasReceived=true;
    if (millis()>noRepeat4)
      beep(settings.soundPattern4);      
    noRepeat4=millis()+REPEAT_LIMIT_MS; //can't do it again for 5 seconds
    response="OK";
    }
  else if (strcmp(reqTopic,settings.commandTopic)==0 &&
          processCommand(charbuf))
    {
    response="OK, restarting";
    needRestart=true;
    }
  else
    {
    char badCmd[18];
    strcpy(badCmd,"(empty)");
    response=badCmd;
    }

  //prepare the response topic
  char topic[MQTT_MAX_TOPIC_SIZE];
  strcpy(topic,reqTopic);
  strcat(topic,"/");
  strcat(topic,charbuf); //the incoming command becomes the topic suffix

  if (!publish(topic,response,false)) //do not retain
    Serial.println("************ Failure when publishing status response!");

  if (needRestart)
    {
    delay(1000);
    ESP.restart();
    }
  }

boolean sendMessage(char* topic, char* value)
  { 
  boolean success=false;
  if (!mqttClient.connected())
    {
    Serial.println("Not connected to MQTT broker!");
    }
  else
    {
    char topicBuf[MQTT_MAX_TOPIC_SIZE+MQTT_MAX_MESSAGE_SIZE];
    char reading[18];
//    boolean success=false; //only for the incoming topic and value

    //publish the radio strength reading while we're at it
    strcpy(topicBuf,settings.mqttTopic1);
    strcat(topicBuf,MQTT_TOPIC_RSSI);
    sprintf(reading,"%d",WiFi.RSSI()); 
    success=publish(topicBuf,reading,true); //retain
    if (!success)
      Serial.println("************ Failed publishing rssi!");
    
    //publish the message
    strcpy(topicBuf,settings.mqttTopic1);
    strcat(topicBuf,topic);
    success=publish(topicBuf,value,true); //retain
    if (!success)
      Serial.println("************ Failed publishing "+String(topic)+"! ("+String(success)+")");
    }
  return success;
  }

void otaSetup()
  {
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() 
    {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) 
      {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  }

void setup() 
  {
  //init record of stack
  char stack;
  stack_start = &stack;  
  
  pinMode(LED_BUILTIN,OUTPUT);// The blue light on the board shows WiFi activity
  digitalWrite(LED_BUILTIN,LED_OFF);
  pinMode(SOUNDER_PORT,OUTPUT); // The port for the sounder device
  digitalWrite(SOUNDER_PORT,SOUNDER_OFF); //silence the sounder
  pinMode(FLASHLED_PORT,OUTPUT); // The port for the history LED
  digitalWrite(FLASHLED_PORT,FLASHLED_OFF); //turn off the LED until we receive a mqtt message

  Serial.begin(115200);
  Serial.setTimeout(10000);
  Serial.println();
  
  while (!Serial); // wait here for serial port to connect.
  Serial.println(F("Running."));

  EEPROM.begin(sizeof(settings)); //fire up the eeprom section of flash
  commandString.reserve(200); // reserve 200 bytes of serial buffer space for incoming command string

  if (settings.debug)
    Serial.println(F("Loading settings"));
  loadSettings(); //set the values from eeprom

  Serial.print("Performing settings sanity check...");
  if ((settings.validConfig!=0 && 
      settings.validConfig!=VALID_SETTINGS_FLAG) || //should always be one or the other
      settings.brokerPort<0 ||
      settings.brokerPort>65535 ||
      strlen(settings.mqttMessage1)>MQTT_MAX_MESSAGE_SIZE ||
      strlen(settings.mqttMessage2)>MQTT_MAX_MESSAGE_SIZE ||
      strlen(settings.mqttMessage3)>MQTT_MAX_MESSAGE_SIZE ||
      strlen(settings.mqttMessage4)>MQTT_MAX_MESSAGE_SIZE ||
      settings.noteLengthMs>10000  ||
      settings.noteOctave>8 || 
      settings.noteOctave<0)
    {
    Serial.println("\nSettings in eeprom failed sanity check, initializing.");
    initializeSettings(); //must be a new board or flash was erased
    }
  else
    Serial.println("passed.");
  
  messageWasReceived=false;

  if (settings.debug)
    Serial.println(F("Connecting to WiFi"));
  
  if (settings.validConfig==VALID_SETTINGS_FLAG)
    connectToWiFi(); //connect to the wifi
  
  if (WiFi.status() == WL_CONNECTED)
    {
    otaSetup(); //initialize the OTA stuff
    }
  else
    digitalWrite(LED_BUILTIN,LED_OFF);
  }

void loop()
  {
  if (settings.validConfig==VALID_SETTINGS_FLAG
      && WiFi.status() == WL_CONNECTED)
    {
    mqttReconnect(); //make sure we stay connected to the broker
    } 
  checkForCommand(); // Check for input in case something needs to be changed to work
  ArduinoOTA.handle(); //Check for new version

  // if (millis()>=timeoutCount && !timeoutMessageSent)
  //   {
  //   digitalWrite(RELAY_PORT,RELAY_OFF); //turn off the device
  //   digitalWrite(LED_PORT,LED_ON); //turn on the failure LED
  //   connectToWiFi(); //make sure we're connected to the broker
  //   timeoutMessageSent=sendMessage(MQTT_TOPIC_STATUS, settings.mqttTimeoutMessage);
  //   }

  // static unsigned long nextFlashTime=millis()+250;
  // if (millis()>=timeoutCount && millis()>nextFlashTime && timeoutMessageSent) //flash the led
  //   {
  //   static boolean warning_led_state=LED_ON;
  //   digitalWrite(LED_PORT,warning_led_state);
  //   warning_led_state=!warning_led_state;
  //   nextFlashTime=millis()+250; //half second flash rate
  //   }
  }


/*
 * If not connected to wifi, connect.
 */
boolean connectToWiFi()
  {
  yield();
  static boolean retval=true; //assume connection to wifi is ok
  if (WiFi.status() != WL_CONNECTED)
    {
    if (settings.debug)
      {
      Serial.print(F("Attempting to connect to WPA SSID \""));
      Serial.print(settings.ssid);
      Serial.print("\" with passphrase \"");
      Serial.print(settings.wifiPassword);
      Serial.println("\"");
      }

    WiFi.mode(WIFI_STA); //station mode, we are only a client in the wifi world
    
    // if (strlen(settings.hostName)>0)
    //   WiFi.hostname(settings.hostName); //else use the default

    WiFi.begin(settings.ssid, settings.wifiPassword);

    bool ledLit=true; //blink the LED when attempting to connect
    digitalWrite(LED_BUILTIN,LED_ON);

    //try for a few seconds to connect to wifi
    for (int i=0;i<WIFI_CONNECTION_ATTEMPTS;i++)  
      {
      if (WiFi.status() == WL_CONNECTED)
        {
        digitalWrite(LED_BUILTIN,LED_ON); //show we're connected
        break;  // got it
        }
      if (settings.debug)
        Serial.print(".");
      checkForCommand(); // Check for input in case something needs to be changed to work
      delay(500);
      if (ledLit)
        digitalWrite(LED_BUILTIN,LED_OFF);
      else
        digitalWrite(LED_BUILTIN,LED_ON);
      ledLit=!ledLit;
      }

    if (WiFi.status() == WL_CONNECTED)
      {
      digitalWrite(LED_BUILTIN,LED_ON); //show we're connected
      if (settings.debug)
        {
        Serial.println(F("Connected to network."));
        Serial.println();
        }
      //show the IP address
      Serial.println(WiFi.localIP());
      retval=true;
      }     
    else //can't connect to wifi, try again next time
      {
      retval=false;
      Serial.print("Wifi status is ");
      Serial.println(WiFi.status());
      Serial.println(F("WiFi connection unsuccessful. Rebooting..."));
      digitalWrite(LED_BUILTIN,LED_OFF); //stay off until we connect
      delay(5000); //time to read the message
      ESP.restart(); //reboot
      }
    }
  else
    digitalWrite(LED_BUILTIN,LED_OFF);

  if (WiFi.status() == WL_CONNECTED)
    {
    mqttReconnect(); // go ahead and connect to the MQTT broker
    }
  return retval;
  }

void showSub(char* topic, bool subgood)
  {
  Serial.print("++++++Subscribing to ");
  Serial.print(topic);
  Serial.print(":");
  Serial.println(subgood?"ok":"failed");
  }


/*
 * Reconnect to the MQTT broker
 */
void mqttReconnect() 
  {
  bool ledLit=true; //blink the LED when attempting to connect
  digitalWrite(LED_BUILTIN,LED_ON);
    
  // Loop until we're reconnected
  while (!mqttClient.connected() && settings.validConfig==VALID_SETTINGS_FLAG) 
    {  
    if (ledLit)
      digitalWrite(LED_BUILTIN,LED_OFF);
    else
      digitalWrite(LED_BUILTIN,LED_ON);
    ledLit=!ledLit;

    Serial.print("Attempting MQTT connection...");

    mqttClient.setBufferSize(1000); //default (256) isn't big enough
    mqttClient.setServer(settings.brokerAddress, settings.brokerPort);
    mqttClient.setCallback(incomingMqttHandler);
    
    // Attempt to connect
    char willTopic[MQTT_MAX_TOPIC_SIZE]="";
    strcpy(willTopic,settings.commandTopic);
    strcat(willTopic,"/");
    strcat(willTopic,MQTT_TOPIC_STATUS);


    if (mqttClient.connect(settings.mqttClientId,
                          settings.mqttUsername,
                          settings.mqttUserPassword,
                          willTopic,
                          0,                  //QOS
                          true,               //retain
                          settings.mqttLWTMessage))
      {
      Serial.println("connected to MQTT broker.");

      if (settings.debug)
        {
        Serial.print("Subscribing to topic \"");
        Serial.print(settings.commandTopic);
        Serial.println("\"");
        }
      bool subgood=mqttClient.subscribe(settings.commandTopic);
      showSub(settings.commandTopic,subgood);

      //resubscribe to the incoming message topic
      if (strcmp(settings.mqttTopic1,settings.commandTopic)!=0) //only subscribe once per topic
        {
        if (settings.debug)
          {
          Serial.print("Subscribing to topic \"");
          Serial.print(settings.mqttTopic1);
          Serial.println("\"");
          }
        subgood=mqttClient.subscribe(settings.mqttTopic1);
        showSub(settings.mqttTopic1,subgood);
        }

      if (strlen(settings.mqttTopic2)>0//only subscribe once per topic
          && strcmp(settings.mqttTopic2,settings.commandTopic)!=0 
          && strcmp(settings.mqttTopic2,settings.mqttTopic1)!=0)
        {
        if (settings.debug)
          {
          Serial.print("Subscribing to topic \"");
          Serial.print(settings.mqttTopic2);
          Serial.println("\"");
          }
        bool subgood=mqttClient.subscribe(settings.mqttTopic2);
        showSub(settings.mqttTopic2,subgood);
        }

      if (strlen(settings.mqttTopic3)>0//only subscribe once per topic
          && strcmp(settings.mqttTopic3,settings.commandTopic)!=0 
          && strcmp(settings.mqttTopic3,settings.mqttTopic1)!=0
          && strcmp(settings.mqttTopic3,settings.mqttTopic2)!=0)
        {
        if (settings.debug)
          {
          Serial.print("Subscribing to topic \"");
          Serial.print(settings.mqttTopic3);
          Serial.println("\"");
          }
        bool subgood=mqttClient.subscribe(settings.mqttTopic3);
        showSub(settings.mqttTopic3,subgood);
        }

      if (strlen(settings.mqttTopic4)>0//only subscribe once per topic
          && strcmp(settings.mqttTopic4,settings.commandTopic)!=0 
          && strcmp(settings.mqttTopic4,settings.mqttTopic1)!=0
          && strcmp(settings.mqttTopic4,settings.mqttTopic2)!=0
          && strcmp(settings.mqttTopic4,settings.mqttTopic3)!=0)
        {
        if (settings.debug)
          {
          Serial.print("Subscribing to topic \"");
          Serial.print(settings.mqttTopic4);
          Serial.println("\"");
          }
        bool subgood=mqttClient.subscribe(settings.mqttTopic4);
        showSub(settings.mqttTopic4,subgood);
        }
      digitalWrite(LED_BUILTIN,LED_ON);
      }
    else 
      {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      Serial.println("Will try again in a second");
      
      // Wait a second before retrying
      // In the meantime check for input in case something needs to be changed to make it work
      checkForCommand(); 
      
      delay(1000);
      }
    }
  mqttClient.loop(); //This has to happen every so often or we get disconnected for some reason
  }

//Generate an MQTT client ID.  This should not be necessary very often
char* generateMqttClientId(char* mqttId)
  {
  String ext=String(random(0xffff), HEX);
  const char* extc=ext.c_str();
  strcpy(mqttId,MQTT_CLIENT_ID_ROOT);
  strcat(mqttId,extc);
  if (settings.debug)
    {
    Serial.print("New MQTT userid is ");
    Serial.println(mqttId);
    }
  return mqttId;
  }

void showSettings()
  {
  Serial.print("ssid=<wifi ssid> (");
  Serial.print(settings.ssid);
  Serial.println(")");
  Serial.print("wifipass=<wifi password> (");
  Serial.print(settings.wifiPassword);
  Serial.println(")");
  Serial.print("broker=<address of MQTT broker> (");
  Serial.print(settings.brokerAddress);
  Serial.println(")");
  Serial.print("brokerPort=<port number MQTT broker> (");
  Serial.print(settings.brokerPort);
  Serial.println(")");
  Serial.print("userName=<user ID for MQTT broker> (");
  Serial.print(settings.mqttUsername);
  Serial.println(")");
  Serial.print("userPass=<user password for MQTT broker> (");
  Serial.print(settings.mqttUserPassword);
  Serial.println(")");
  Serial.print("topic1=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic1);
  Serial.println(")");
  Serial.print("message1=<a message for topic 1> (");
  Serial.print(settings.mqttMessage1);
  Serial.println(")");
  Serial.print("soundPattern1=<an 8-bit pattern describing the sound pattern when message1 is received> (");
  Serial.print(settings.soundPattern1);
  Serial.println(")");
  Serial.print("topic2=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic2);
  Serial.println(")");
  Serial.print("message2=<a message for topic 2> (");
  Serial.print(settings.mqttMessage2);
  Serial.println(")");
  Serial.print("soundPattern2=<an 8-bit pattern describing the sound pattern when message2 is received> (");
  Serial.print(settings.soundPattern2);
  Serial.println(")");
  Serial.print("topic3=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic3);
  Serial.println(")");
  Serial.print("message3=<a message for topic 3> (");
  Serial.print(settings.mqttMessage3);
  Serial.println(")");
  Serial.print("soundPattern3=<an 8-bit pattern describing the sound pattern when message3 is received> (");
  Serial.print(settings.soundPattern3);
  Serial.println(")");
  Serial.print("topic4=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic4);
  Serial.println(")");
  Serial.print("message4=<a message for topic 4> (");
  Serial.print(settings.mqttMessage4);
  Serial.println(")");
  Serial.print("soundPattern4=<up to 10 char pattern describing the sound pattern when message4 is received> (");
  Serial.print(settings.soundPattern4);
  Serial.println(")");
  Serial.print("lwtMessage=<status message to send when power is removed> (");
  Serial.print(settings.mqttLWTMessage);
  Serial.println(")");
  Serial.print("commandTopic=<mqtt message for commands to this device> (");
  Serial.print(settings.commandTopic);
  Serial.println(")");
  // Serial.print("hostName=<network name for this device> (");
  // Serial.print(settings.hostName);
  Serial.print("noteLengthMs=<length of notes in milliseconds> (");
  Serial.print(settings.noteLengthMs);
  Serial.println(")");
  Serial.print("noteOctave=<octave of note, 0 through 8> (");
  Serial.print(settings.noteOctave);
  Serial.println(")");
  Serial.print("debug=<print debug messages to serial port> (");
  Serial.print(settings.debug?"true":"false");
  Serial.println(")");
  Serial.print("MQTT client ID=<automatically generated client ID> (");
  Serial.print(settings.mqttClientId);
  Serial.println(") **Use \"resetmqttid=yes\" to regenerate");
  Serial.println("\n*** Use \"factorydefaults=yes\" to reset all settings ***");
  Serial.print("\nIP Address=");
  Serial.println(WiFi.localIP());
  }

/*
 * Check for configuration input via the serial port.  Return a null string 
 * if no input is available or return the complete line otherwise.
 */
String getConfigCommand()
  {
  if (commandComplete) 
    {
    String newCommand=commandString;

    commandString = "";
    commandComplete = false;
    return newCommand;
    }
  else return "";
  }

bool processCommand(String cmd)
  {
  const char *str=cmd.c_str();
  char *val=NULL;
  char *nme=strtok((char *)str,"=");
  if (nme!=NULL)
    val=strtok(NULL,"=");

  char zero[]=""; //zero length string

  //Get rid of the carriage return and/or linefeed. Twice because could have both.
  if (val!=NULL && strlen(val)>0 && (val[strlen(val)-1]==13 || val[strlen(val)-1]==10))
    val[strlen(val)-1]=0; 
  if (val!=NULL && strlen(val)>0 && (val[strlen(val)-1]==13 || val[strlen(val)-1]==10))
    val[strlen(val)-1]=0; 

  //do it for the command as well.  Might not even have a value.
  if (nme!=NULL && strlen(nme)>0 && (nme[strlen(nme)-1]==13 || nme[strlen(nme)-1]==10))
    nme[strlen(nme)-1]=0; 
  if (nme!=NULL && strlen(nme)>0 && (nme[strlen(nme)-1]==13 || nme[strlen(nme)-1]==10))
    nme[strlen(nme)-1]=0; 

  if (settings.debug)
    {
    Serial.print("Processing command \"");
    Serial.print(nme);
    Serial.println("\"");
    Serial.print("Length:");
    Serial.println(strlen(nme));
    Serial.print("Hex:");
    Serial.println(nme[0],HEX);
    Serial.print("Value is \"");
    Serial.print(val);
    Serial.println("\"\n");
    }

  if (val==NULL)
    val=zero;

  if (nme==NULL || val==NULL || strlen(nme)==0) //empty string is a valid val value
    {
    showSettings();
    return false;   //not a valid command, or it's missing
    }
  else if (strcmp(nme,"ssid")==0)
    {
    strncpy(settings.ssid,val,SSID_SIZE);
    settings.ssid[SSID_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"wifipass")==0)
    {
    strncpy(settings.wifiPassword,val,PASSWORD_SIZE);
    settings.wifiPassword[PASSWORD_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"broker")==0)
    {
    strncpy(settings.brokerAddress,val,ADDRESS_SIZE);
    settings.brokerAddress[ADDRESS_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"brokerPort")==0)
    {
    settings.brokerPort=atoi(val);
    saveSettings();
    }
  else if (strcmp(nme,"userName")==0)
    {
    strncpy(settings.mqttUsername,val,USERNAME_SIZE);
    settings.mqttUsername[USERNAME_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"userPass")==0)
    {
    strncpy(settings.mqttUserPassword,val,PASSWORD_SIZE);
    settings.mqttUserPassword[PASSWORD_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"lwtMessage")==0)
    {
    strncpy(settings.mqttLWTMessage,val,MQTT_MAX_MESSAGE_SIZE);
    settings.mqttLWTMessage[MQTT_MAX_MESSAGE_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"topic1")==0)
    {
    strncpy(settings.mqttTopic1,val,MQTT_MAX_TOPIC_SIZE);
    settings.mqttTopic1[MQTT_MAX_TOPIC_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"topic2")==0)
    {
    strncpy(settings.mqttTopic2,val,MQTT_MAX_TOPIC_SIZE);
    settings.mqttTopic2[MQTT_MAX_TOPIC_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"topic3")==0)
    {
    strncpy(settings.mqttTopic3,val,MQTT_MAX_TOPIC_SIZE);
    settings.mqttTopic3[MQTT_MAX_TOPIC_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"topic4")==0)
    {
    strncpy(settings.mqttTopic4,val,MQTT_MAX_TOPIC_SIZE);
    settings.mqttTopic4[MQTT_MAX_TOPIC_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"message1")==0)
    {
    strncpy(settings.mqttMessage1,val,MQTT_MAX_MESSAGE_SIZE);
    settings.mqttMessage1[MQTT_MAX_MESSAGE_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"message2")==0)
    {
    strncpy(settings.mqttMessage2,val,MQTT_MAX_MESSAGE_SIZE);
    settings.mqttMessage2[MQTT_MAX_MESSAGE_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"message3")==0)
    {
    strncpy(settings.mqttMessage3,val,MQTT_MAX_MESSAGE_SIZE);
    settings.mqttMessage3[MQTT_MAX_MESSAGE_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"message4")==0)
    {
    strncpy(settings.mqttMessage4,val,MQTT_MAX_MESSAGE_SIZE);
    settings.mqttMessage4[MQTT_MAX_MESSAGE_SIZE]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"soundPattern1")==0)
    {
    strncpy(settings.soundPattern1,val,TONE_MAX_PATTERN_LENGTH);
    settings.soundPattern1[TONE_MAX_PATTERN_LENGTH]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"soundPattern2")==0)
    {
    strncpy(settings.soundPattern2,val,TONE_MAX_PATTERN_LENGTH);
    settings.soundPattern2[TONE_MAX_PATTERN_LENGTH]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"soundPattern3")==0)
    {
    strncpy(settings.soundPattern3,val,TONE_MAX_PATTERN_LENGTH);
    settings.soundPattern3[TONE_MAX_PATTERN_LENGTH]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"soundPattern4")==0)
    {
    strncpy(settings.soundPattern4,val,TONE_MAX_PATTERN_LENGTH);
    settings.soundPattern4[TONE_MAX_PATTERN_LENGTH]='\0';
    saveSettings();
    }
  else if ((strcmp(nme,"resetmqttid")==0)&& (strcmp(val,"yes")==0))
    {
    generateMqttClientId(settings.mqttClientId);
    saveSettings();
    }
  else if (strcmp(nme,"commandTopic")==0)
    {
    strcpy(settings.commandTopic,val);
    saveSettings();
    }
  else if (strcmp(nme,"noteLengthMs")==0)
    {
    settings.noteLengthMs=atoi(val);
    saveSettings();
    }
  else if (strcmp(nme,"noteOctave")==0)
    {
    settings.noteOctave=atoi(val);
    saveSettings();
    }
  else if (strcmp(nme,"debug")==0)
    {
    settings.debug=strcmp(val,"false")==0?false:true;
    saveSettings();
    }
  else if ((strcmp(nme,"factorydefaults")==0) && (strcmp(val,"yes")==0)) //reset all eeprom settings
    {
    Serial.println("\n*********************** Resetting EEPROM Values ************************");
    initializeSettings();
    saveSettings();
    delay(2000);
    ESP.restart();
    }
  else if ((strcmp(nme,"reset")==0) && (strcmp(val,"yes")==0)) //reset the device
    {
    Serial.println("\n*********************** Resetting Device ************************");
    delay(1000);
    ESP.restart();
    }
  else
    {
    showSettings();
    return false; //command not found
    }
  return true;
  }

void initializeSettings()
  {
  settings.validConfig=0; 
  strcpy(settings.ssid,"");
  strcpy(settings.wifiPassword,"");
  strcpy(settings.brokerAddress,"");
  settings.brokerPort=DEFAULT_MQTT_BROKER_PORT;
  strcpy(settings.mqttLWTMessage,DEFAULT_MQTT_LWT_MESSAGE);
  strcpy(settings.mqttMessage1,"");
  strcpy(settings.mqttMessage2,"");
  strcpy(settings.mqttMessage3,"");
  strcpy(settings.mqttMessage4,"");
  strcpy(settings.soundPattern1,TONE_SOUND_PATTERN_1);
  strcpy(settings.soundPattern2,TONE_SOUND_PATTERN_2);
  strcpy(settings.soundPattern3,TONE_SOUND_PATTERN_3);
  strcpy(settings.soundPattern4,TONE_SOUND_PATTERN_4);
  strcpy(settings.mqttTopic1,DEFAULT_MQTT_TOPIC);
  strcpy(settings.mqttTopic2,"");
  strcpy(settings.mqttTopic3,"");
  strcpy(settings.mqttTopic4,"");
  strcpy(settings.mqttUsername,"");
  strcpy(settings.mqttUserPassword,"");
  strcpy(settings.commandTopic,DEFAULT_MQTT_TOPIC);
  generateMqttClientId(settings.mqttClientId);
  settings.debug=false;
  settings.noteLengthMs=DEFAULT_NOTE_LENGTH_MS;
  settings.noteOctave=DEFAULT_NOTE_OCTAVE;
  saveSettings();
  }

void checkForCommand()
  {
  serialEvent();
  String cmd=getConfigCommand();
  if (cmd.length()>0)
    {
    processCommand(cmd);
    }
  }
  
/*
*  Initialize the settings from eeprom and determine if they are valid
*/
void loadSettings()
  {
  EEPROM.get(0,settings);
  if (settings.validConfig==VALID_SETTINGS_FLAG)    //skip loading stuff if it's never been written
    {
    settingsAreValid=true;
    if (settings.debug)
      Serial.println("Loaded configuration values from EEPROM");
    }
  else
    {
    Serial.println("Skipping load from EEPROM, device not configured.");    
    settingsAreValid=false;
    }
  }

/*
 * Save the settings to EEPROM. Set the valid flag if everything is filled in.
 */
boolean saveSettings()
  {
  static boolean wasIncomplete=false;
  static boolean shouldReboot=false;
  if (strlen(settings.ssid)>0 &&
    strlen(settings.ssid)<=SSID_SIZE &&
    strlen(settings.wifiPassword)>0 &&
    strlen(settings.wifiPassword)<=PASSWORD_SIZE &&
    strlen(settings.brokerAddress)>0 &&
    strlen(settings.brokerAddress)<ADDRESS_SIZE &&
    strlen(settings.mqttLWTMessage)>0 &&
    strlen(settings.mqttLWTMessage)<MQTT_MAX_MESSAGE_SIZE &&
    strlen(settings.mqttMessage1)>0 &&
    strlen(settings.mqttMessage1)<MQTT_MAX_MESSAGE_SIZE &&
    strlen(settings.mqttMessage2)<MQTT_MAX_MESSAGE_SIZE &&
    strlen(settings.mqttMessage3)<MQTT_MAX_MESSAGE_SIZE &&
    strlen(settings.mqttMessage4)<MQTT_MAX_MESSAGE_SIZE &&
    strlen(settings.soundPattern1)<=TONE_MAX_PATTERN_LENGTH &&
    strlen(settings.soundPattern2)<=TONE_MAX_PATTERN_LENGTH &&
    strlen(settings.soundPattern3)<=TONE_MAX_PATTERN_LENGTH &&
    strlen(settings.soundPattern4)<=TONE_MAX_PATTERN_LENGTH &&
    strlen(settings.mqttTopic1)>0 &&
    strlen(settings.mqttTopic1)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic2)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic3)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic4)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.commandTopic)>0 &&
    strlen(settings.commandTopic)<MQTT_MAX_TOPIC_SIZE &&
    settings.brokerPort>0 && settings.brokerPort<65535)
    {
    Serial.println("Settings deemed complete");
    settings.validConfig=VALID_SETTINGS_FLAG;
    settingsAreValid=true;
    if (wasIncomplete)
      {
      wasIncomplete=false;
      shouldReboot=true;
      }
    }
  else
    {
    Serial.println("Settings still incomplete");
    settings.validConfig=0;
    settingsAreValid=false;
    wasIncomplete=true;
    }

  //The mqttClientId is not set by the user, but we need to make sure it's set  
  if (strlen(settings.mqttClientId)==0)
    {
    generateMqttClientId(settings.mqttClientId);
    }

  EEPROM.put(0,settings);
  return EEPROM.commit();

  if (shouldReboot)
    {
    Serial.println("Settings now valid, restarting.");
    delay(1000);
    shouldReboot=false;
    ESP.restart();
    }
  }

/*
  SerialEvent occurs whenever a new data comes in the hardware serial RX. This
  routine is run between each time loop() runs, so using delay inside loop can
  delay response. Multiple bytes of data may be available.
*/
void serialEvent() 
  {
  while (Serial.available()) 
    {
    // get the new byte
    char inChar = (char)Serial.read();
    Serial.print(inChar);

    // if the incoming character is a newline, set a flag so the main loop can
    // do something about it 
    if (inChar == '\n') 
      {
      commandComplete = true;
      }
    else
      {
      // add it to the inputString 
      commandString += inChar;
      }
    }
  }

