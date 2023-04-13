/*
 * Program to play an audio message assigned to different mqtt messages.
 * By David E. Powell 
 *
 * Subscribes to an MQTT topic and when the target message is received, 
 * activates the DF Robot DFPlayer Mini mp3 player and plays the associated sound.
 * 
 * Configuration is done via serial connection or by MQTT message.
 *  
 * **** Note to self: To erase the entire flash chip in PlatformIO, open
 * **** a terminal and type "pio run -t erase"
 */ 
#include <Arduino.h>
#include <string.h>
#include <PubSubClient.h> 
#include <EEPROM.h>
#include <pgmspace.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LiquidCrystal.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"

#ifdef ESP32
  #include <Tone32.h>
  #include <ESPAsync_WiFiManager.h>
  #endif

#include "mqttListener.h"

char *stack_start;// initial stack size

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

LiquidCrystal lcd(D0,D1,D2,D5,D6,D7); //RS, Enable, Data4, Data5, Data6, Data7 on display

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
  char description1[DISPLAY_COLUMNS+1]=""; //for the LCD
  char description2[DISPLAY_COLUMNS+1]="";
  char description3[DISPLAY_COLUMNS+1]="";
  char description4[DISPLAY_COLUMNS+1]="";
  char mqttLWTMessage[MQTT_MAX_MESSAGE_SIZE+1]="";
  char commandTopic[MQTT_MAX_TOPIC_SIZE+1]=DEFAULT_MQTT_TOPIC;
  boolean debug=false;
  char mqttClientId[MQTT_CLIENTID_SIZE+1]=""; //will be the same across reboots
  int gmtOffset=0; // -6 for CST
  int volume=DEFAULT_VOLUME;
  } conf;

conf settings; //all settings in one struct makes it easier to store in EEPROM
boolean settingsAreValid=false;
boolean setupOK=false;

//This structure is for the in-memory message history.  It will vanish when the 
//device is restarted. For now it only contains the topic number and the date code.
//A future change may be to add the actual topic and message received.
typedef struct
  {
  uint8 topicNumber=0;
  unsigned long timestamp=0;
  } histEntry;
histEntry history[HISTORY_BUFFER_SIZE]; //circular buffer for histEntry objects
uint8 histPointer=0;                    //points to next spot for history entry
uint16 histEntryCount=0;                //contains the total number of history entries

String commandString = "";     // a String to hold incoming commands from serial
bool commandComplete = false;  // goes true when enter is pressed

char clockTime[DISPLAY_COLUMNS+1]="";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

SoftwareSerial mySoftwareSerial(D4, D3); // RX, TX
DFRobotDFPlayerMini myDFPlayer;

char lastLastLine[DISPLAY_COLUMNS+1]="";

/// @brief Show a message on the LCD, with optional timestamp.
/// @param msg - message to display
/// @param showTimestamp - show the timestamp on line 0
/// @param clear - clear the display first
/// @param lineNumber - put message on this line (0=based)
void show(char* msg, boolean showTimestamp, boolean clear=false, int lineNumber=1)
  {
  if (clear) 
    {
    lcd.clear();
    lastLastLine[0]='\0'; // clear the last line buffer too
    delay(500);
    } 

  if (showTimestamp)
    {
    lcd.clear();
    lastLastLine[0]='\0'; // clear the last line buffer too    
    lcd.setCursor(0, 0);
    lcd.print(clockTime); //current timestamp
    }

  if (strlen(msg)>0)
    {
    char buf[DISPLAY_COLUMNS+1];
    strncpy(buf,msg,DISPLAY_COLUMNS); //make sure message is not too long
    buf[DISPLAY_COLUMNS]='\0';
    lcd.setCursor(0,lineNumber);
    lcd.print(buf);
    if (lineNumber==DISPLAY_ROWS-1)
      strcpy(lastLastLine,buf); //save the bottom line for scroll
    }
  }

void scrollDisplay()
  {
  char buf[DISPLAY_COLUMNS+1];
  strcpy(buf,lastLastLine); //show is pass by reference
  buf[DISPLAY_COLUMNS]='\0';
  show(buf,false,true,0);
  }

void addHistoryEntry(uint8 topicNumber, unsigned long timestamp)
  {
  history[histPointer]={topicNumber,timestamp};
  if (++histPointer >= HISTORY_BUFFER_SIZE)
    histPointer=0; //circular buffer
  if (++histEntryCount > HISTORY_BUFFER_SIZE)
    histEntryCount=HISTORY_BUFFER_SIZE; //it's the max we can hold
  }


boolean refreshTime()
  {
  boolean ok=true;
  Serial.print(timeClient.getFormattedTime());
  Serial.print("\tRefreshing time...");
  if (WiFi.status() != WL_CONNECTED)
    {
    Serial.println("Abort, WiFi not connected.");
    ok=false;
    }
  else
    {
    connectToWiFi(); //may need to connect to the wifi
    if (settingsAreValid && WiFi.status() == WL_CONNECTED)
      {
      bool timeGood=timeClient.update();
      if (timeGood)
        {
        timeClient.setTimeOffset(settings.gmtOffset*3600);
        Serial.println("done.");
        Serial.print("Time is ");
        Serial.println(timeClient.getFormattedTime());
        }
      else
        {
        Serial.println("***** Unable to refresh time *****");
        ok=false;
        }
      }
    }
  return ok;
  } 


boolean updateClock()
  {
  static unsigned long lastTime=0;
  unsigned long currentTime=timeClient.getEpochTime();
  boolean ok=true;

  if (settingsAreValid && currentTime != lastTime)
    {
    //Update the time every 12 hours, or if we just powered up
    if (currentTime%43200 ==0 || timeClient.getEpochTime() < 100000)
      {
      ok=refreshTime();
      }
    lastTime=currentTime;

    unsigned long today=timeClient.getEpochTime();
    char datebuff[32];
    sprintf(datebuff,"%02d/%02d %s",month(today),day(today),timeClient.getFormattedTime().c_str());
    strncpy(clockTime,datebuff,DISPLAY_COLUMNS);
    clockTime[DISPLAY_COLUMNS]='\0';
    }
  return ok;
  }

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

/* Get and print the details of any messages from the mp3 player. */
void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}


/// @brief Compare two char strings, with allowances for '+' and '#'
/// @param preciseTopic The incoming mqtt topic
/// @param mqttTopic The stored topic, possibly with wildcard characters
/// @return true if they match
boolean mqttCompare(char* preciseTopic, char* mqttTopic)
  {
  char mTopic[MQTT_MAX_TOPIC_SIZE]; 
  char pTopic[MQTT_MAX_TOPIC_SIZE];
  char* mqttRest=mTopic;
  char* preciseRest=pTopic;
  char plus[]="+";
  char pound[]="#";

  strcpy(mTopic,mqttTopic); //must work on a copy, strtok_r messes it up
  strcpy(pTopic,preciseTopic);
  char* mqttParsed=strtok_r(mTopic,"/",&mqttRest);
  char* preciseParsed=strtok_r(pTopic,"/",&preciseRest);
  while (mqttParsed!=NULL || preciseParsed!=NULL)
    {
    if (settings.debug) 
      {
      Serial.print("Comparing part \"");
      Serial.print(mqttParsed);
      Serial.print("\" with \"");
      Serial.print(preciseParsed);
      Serial.print("\"...");
      }
    if (strcmp(mqttParsed,"#")==0)
      {
      if (settings.debug)
        Serial.println("# found at end of topic, we're done.");
      break; // # can only appear at the end of a topic. We're done.
      }

    else if (strcmp(mqttParsed,plus)!=0 && strcmp(mqttParsed,pound)!=0
      && strcmp(preciseParsed,mqttParsed)!=0)
      {
      if (settings.debug)
        Serial.println("Not a match.");
      return false;
      }
    else if (settings.debug)
      Serial.println("matched.");
    mqttParsed=strtok_r(NULL,"/",&mqttRest);    //next segments
    preciseParsed=strtok_r(NULL,"/",&preciseRest);
    }
  if (settings.debug)
    {
    Serial.print("\"");
    Serial.print(preciseTopic);
    Serial.print("\" matched against \"");
    Serial.print(mqttTopic);
    Serial.println("\".");
    }
  return true; //everything matched
  }

/*
Convert the history buffer from a binary format to something that
is readable by humans.  Parameter is a buffer that's big enough to
hold all of the text that could be generated.
*/
void buildReadableHistory(char* buffer)
  {
  if (histEntryCount==0)
    {
    strcpy(buffer, "No history yet.");
    }
  else
    {
    char datebuff[32];
    unsigned int tempPointer=0;
    if (histEntryCount>=HISTORY_BUFFER_SIZE)
      tempPointer=histPointer; //it's a circular buffer

    strcpy(buffer,"");
    for (int i=0;i<histEntryCount;i++)
      {
      unsigned long thisTime=history[tempPointer].timestamp;
      sprintf(datebuff,"%02d/%02d %02d:%02d:%02d ",month(thisTime),day(thisTime),hour(thisTime),minute(thisTime),second(thisTime));
      strcat(buffer,datebuff);

      switch (history[tempPointer].topicNumber)
        {
        case 1:
          strcat(buffer,settings.description1);
          break;
        
        case 2:
          strcat(buffer,settings.description2);
          break;
        
        case 3:
          strcat(buffer,settings.description3);
          break;
        
        case 4:
          strcat(buffer,settings.description4);
          break;
        
        default:
          strcat(buffer,"Unknown topic # ");
          strcat(buffer,String(history[tempPointer].topicNumber).c_str());
          break;
        }
      if (++tempPointer >= HISTORY_BUFFER_SIZE)
        tempPointer=0; //circular buffer
      }
    }
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
  static unsigned long noRepeat2=millis(); // the delay time, to keep multiple alerts from occurring.
  static unsigned long noRepeat3=millis();
  static unsigned long noRepeat4=millis();

  if (settings.debug)
    {
    Serial.println("====================================> Callback works.");
    }
  payload[length]='\0'; //this should have been done in the caller code, shouldn't have to do it here
  char charbuf[100];
  sprintf(charbuf,"%s",payload);
  const char* response="\0";

  //The array below was created as a buffer for the settings when responding by MQTT to the 
  //"settings" command (hence the variable name). It is also used to report the history via
  //MQTT, so the size was increased to what you see. It is static because it's too big for
  //the stack and must reside in the heap. The size can be reduced if necessary by making
  //the history buffer smaller.
  static char settingsResp[(DISPLAY_COLUMNS+1)*DISPLAY_ROWS*HISTORY_BUFFER_SIZE];

  settingsResp[0]='\0';

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
    if (settings.debug)
      Serial.println("Sending settings...");
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
    strcat(settingsResp,"description1=");
    strcat(settingsResp,settings.description1);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"description2=");
    strcat(settingsResp,settings.description2);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"description3=");
    strcat(settingsResp,settings.description3);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"description4=");
    strcat(settingsResp,settings.description4);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"gmtOffset=");
    strcat(settingsResp,String(settings.gmtOffset).c_str());
    strcat(settingsResp,"\n");
    strcat(settingsResp,"volume=");
    strcat(settingsResp,String(settings.volume).c_str());
    strcat(settingsResp,"\n");
    strcat(settingsResp,"debug=");
    strcat(settingsResp,settings.debug?"true":"false");
    strcat(settingsResp,"\n");
    strcat(settingsResp,"commandTopic=");
    strcat(settingsResp,settings.commandTopic);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"MQTT client ID=");
    strcat(settingsResp,settings.mqttClientId);
    strcat(settingsResp,"\n");
    strcat(settingsResp,"IP Address=");
    strcat(settingsResp,WiFi.localIP().toString().c_str());
    response=settingsResp;
    }
  else if (strcmp(charbuf,"history")==0 &&
      strcmp(reqTopic,settings.commandTopic)==0) //another special case, send message history
    {
    if (settings.debug)
      Serial.println("Sending history...");
    
    buildReadableHistory(settingsResp);
    response=settingsResp;
    }
  else if (strcmp(charbuf,"status")==0 &&
      strcmp(reqTopic,settings.commandTopic)==0) //report that we're alive
    {
    strcpy(settingsResp,"Ready at ");
    strcat(settingsResp,WiFi.localIP().toString().c_str());
    response=settingsResp;
    }   //check for target messages
  else if (strlen(settings.mqttMessage1)>0 
      && mqttCompare(reqTopic,settings.mqttTopic1)==true
      && (strcmp(charbuf,settings.mqttMessage1)==0 
        || strcmp(settings.mqttMessage1,"*")==0))
    {
    if (millis()>noRepeat1)
      {
      addHistoryEntry(1,timeClient.getEpochTime());
      show(settings.description1,true);
      myDFPlayer.play(1);
      }
    noRepeat1=millis()+REPEAT_LIMIT_MS; //can't do it again for a few seconds
//    response="OK";
    }
  else if (strlen(settings.mqttMessage2)>0 
      && mqttCompare(reqTopic,settings.mqttTopic2)==true
      && (strcmp(charbuf,settings.mqttMessage2)==0 
        || strcmp(settings.mqttMessage2,"*")==0))
    {
    if (millis()>noRepeat2)
      {
      addHistoryEntry(2,timeClient.getEpochTime());
      show(settings.description2,true);
      myDFPlayer.play(2);
      }
    noRepeat2=millis()+REPEAT_LIMIT_MS; //can't do it again for a few seconds
//    response="OK";
    }
  else if (strlen(settings.mqttMessage3)>0 
      && mqttCompare(reqTopic,settings.mqttTopic3)==true
      && (strcmp(charbuf,settings.mqttMessage3)==0 
        || strcmp(settings.mqttMessage3,"*")==0))
    {
    if (millis()>noRepeat3)
      {
      addHistoryEntry(3,timeClient.getEpochTime());
      show(settings.description3,true);
      myDFPlayer.play(3);
      }
    noRepeat3=millis()+REPEAT_LIMIT_MS; //can't do it again for a few seconds
//    response="OK";
    }
  else if (strlen(settings.mqttMessage4)>0 
      && mqttCompare(reqTopic,settings.mqttTopic4)==true
      && (strcmp(charbuf,settings.mqttMessage4)==0 
        || strcmp(settings.mqttMessage4,"*")==0))
    {
    if (millis()>noRepeat4)
      {
      addHistoryEntry(4,timeClient.getEpochTime());
      show(settings.description4,true);
      myDFPlayer.play(4);
      }
    noRepeat4=millis()+REPEAT_LIMIT_MS; //don't do it again for a few seconds
//    response="OK";
    }
  else if (strcmp(reqTopic,settings.commandTopic)==0)
    {
    needRestart=processCommand(charbuf);
    if (needRestart && settingsAreValid)
      response="OK, restarting";
//    else
//      response="OK";
    }
  else
    {
    // char badCmd[18];
    // strcpy(badCmd,"(empty)");
    // response=badCmd;
    }

  //prepare the response topic
  if (response[0]!='\0')
    { 
    char topic[MQTT_MAX_TOPIC_SIZE];
    strcpy(topic,reqTopic);
    strcat(topic,"/");
    strcat(topic,charbuf); //the incoming command becomes the topic suffix
    if (!publish(topic,response,false)) //do not retain
      Serial.println("************ Failure when publishing status response!");
    }
  if (needRestart)
    {
    delay(1000); //let all outgoing messages flush through
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


/// @brief Converts a 0-10 range for volume to 0-30 and sends it to the DFPlayer.
/// @param volume int
void adjustVolume(int volume)
  {
  if (volume>=0 && volume<=10)
    {
    int vol=volume*3;
    myDFPlayer.volume(vol);
    }
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
  
  setupOK=true; //will stay true unless a failure occurs somewhere

  pinMode(LED_BUILTIN,OUTPUT);// The blue light on the board shows WiFi activity
  digitalWrite(LED_BUILTIN,LED_OFF);
  pinMode(FLASHLED_PORT,OUTPUT); // The port for the history LED
  digitalWrite(FLASHLED_PORT,FLASHLED_OFF); //turn off the LED until we receive a mqtt message


  Serial.begin(115200);
  Serial.setTimeout(10000);
  Serial.println();
  
  while (!Serial); // wait here for serial port to connect.
  Serial.println(F("Running."));

  mySoftwareSerial.begin(9600); //for comm with mp3 player
  Serial.print(F("MP3 player baud rate is "));
  Serial.println(mySoftwareSerial.baudRate());

  lcd.begin(DISPLAY_COLUMNS, DISPLAY_ROWS); //16 chars x 2 rows
  show(const_cast<char*>("Starting..."),false,true);

  EEPROM.begin(sizeof(settings)); //fire up the eeprom section of flash
  commandString.reserve(200); // reserve 200 bytes of serial buffer space for incoming command string

  if (settings.debug)
    Serial.println(F("Loading settings"));
  scrollDisplay();
  show(const_cast<char*>("Loading settings"),false);
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
      strlen(settings.description1)>DISPLAY_COLUMNS ||
      strlen(settings.description2)>DISPLAY_COLUMNS ||
      strlen(settings.description3)>DISPLAY_COLUMNS ||
      strlen(settings.description4)>DISPLAY_COLUMNS)
    {
    Serial.println("\nSettings in eeprom failed sanity check, initializing.");
    initializeSettings(); //must be a new board or flash was erased
    }
  else
    Serial.println("passed.");
  
  if (settings.debug)
    Serial.println(F("Connecting to WiFi"));
  
  if (settings.validConfig==VALID_SETTINGS_FLAG)
    {
    scrollDisplay();
    show(const_cast<char*>("Connecting WiFi"),false);
    connectToWiFi(); //connect to the wifi
    }
  
  if (settingsAreValid)
    {
    if (WiFi.status() != WL_CONNECTED)
      {
      setupOK=false;
      digitalWrite(LED_BUILTIN,LED_OFF);
      scrollDisplay();
      show(const_cast<char*>("WiFi error."),false);
      }
    else
      {
      scrollDisplay();
      show(const_cast<char*>("Fetching time..."),false);
  
      if (setupOK && !refreshTime())
        {
        Serial.println(F("Couldn't refresh time."));
        scrollDisplay();
        show(const_cast<char*>("Time error."),false);
        setupOK=false;
        }
      if (setupOK)
        {
        scrollDisplay();
        show(const_cast<char*>("Updating Clock.."),false);
        }
      if (setupOK && !updateClock())
        {
        Serial.println(F("Couldn't update clock."));
        scrollDisplay();
        show(const_cast<char*>("Clock error."),false);
        setupOK=false;
        }
      otaSetup(); //initialize the OTA stuff
      mqttReconnect(); // go ahead and connect to the MQTT broker

      if (setupOK)
        {
        scrollDisplay();
        show(const_cast<char*>("Init MP3 Player"),false);
        }
      if (setupOK && !myDFPlayer.begin(mySoftwareSerial)) 
        {
        Serial.print("Files on SD card: ");
        Serial.println(myDFPlayer.readFileCounts());
        delay(2000);
        if (!myDFPlayer.begin(mySoftwareSerial))  //try again
          {
          Serial.print("Files on SD card: ");
          Serial.println(myDFPlayer.readFileCounts());
          Serial.println(F("MP3 player is borked."));
          scrollDisplay();
          show(const_cast<char*>("MP3 player error"),true);
          setupOK=false;
          delay(1000);
          ESP.restart(); //try rebooting
          }
        }
      myDFPlayer.setTimeOut(500); //Set serial communictaion time out 500ms
      myDFPlayer.EQ(DFPLAYER_EQ_NORMAL); //normal equalization
      myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD); // it's really the input device (sd card)
      adjustVolume(settings.volume);   //Set volume value (0~10).

      if (setupOK)
        {
        show(const_cast<char*>(WiFi.localIP().toString().c_str()),false,true,0);
        show(const_cast<char*>("Startup complete"),false,false,1);
        }
      }
    }
  else
    {
    setupOK=false;
    show(const_cast<char*>("Settings are"),true,false,0);
    show(const_cast<char*>("incomplete."),false,false,1);
    }
  }

void loop()
  {
  if (settings.validConfig==VALID_SETTINGS_FLAG
      && WiFi.status() == WL_CONNECTED
      && setupOK)
    {
    mqttReconnect(); //make sure we stay connected to the broker
    } 
  checkForCommand(); // Check for input in case something needs to be changed to work
  ArduinoOTA.handle(); //Check for new version

  //update the realtime clock once per second
  if (millis()%1000==0 && setupOK)
    updateClock();

  if (setupOK && myDFPlayer.available()) //Print the detail message from DFPlayer for different errors and states.
    printDetail(myDFPlayer.readType(), myDFPlayer.read()); 
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
  Serial.print("description1=<what to display when message1 is received> (");
  Serial.print(settings.description1);
  Serial.println(")");
  Serial.print("topic2=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic2);
  Serial.println(")");
  Serial.print("message2=<a message for topic 2> (");
  Serial.print(settings.mqttMessage2);
  Serial.println(")");
  Serial.print("description2=<what to display when message2 is received> (");
  Serial.print(settings.description2);
  Serial.println(")");
  Serial.print("topic3=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic3);
  Serial.println(")");
  Serial.print("message3=<a message for topic 3> (");
  Serial.print(settings.mqttMessage3);
  Serial.println(")");
  Serial.print("description3=<what to display when message3 is received> (");
  Serial.print(settings.description3);
  Serial.println(")");
  Serial.print("topic4=<MQTT topic for which to subscribe> (");
  Serial.print(settings.mqttTopic4);
  Serial.println(")");
  Serial.print("message4=<a message for topic 4> (");
  Serial.print(settings.mqttMessage4);
  Serial.println(")");
  Serial.print("description4=<what to display when message4 is received> (");
  Serial.print(settings.description4);
  Serial.println(")");
  Serial.print("lwtMessage=<status message to send when power is removed> (");
  Serial.print(settings.mqttLWTMessage);
  Serial.println(")");
  Serial.print("commandTopic=<mqtt message for commands to this device> (");
  Serial.print(settings.commandTopic);
  Serial.println(")");
  Serial.print("gmtOffset=<Time offset from GMT> (");
  Serial.print(settings.gmtOffset);
  Serial.println(")");
  Serial.print("volume=<Speaker volume 0-10> (");
  Serial.print(settings.volume);
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

/// @brief Accepts a KV pair to change a setting or perform an action. Minimal input checking, be careful.
/// @param cmd 
/// @return true if a reset is needed to activate the change
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

  bool needRestart=true; //most changes will need a restart

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
  else if (strcmp(nme,"description1")==0)
    {
    strncpy(settings.description1,val,DISPLAY_COLUMNS);
    settings.description1[DISPLAY_COLUMNS]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"description2")==0)
    {
    strncpy(settings.description2,val,DISPLAY_COLUMNS);
    settings.description2[DISPLAY_COLUMNS]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"description3")==0)
    {
    strncpy(settings.description3,val,DISPLAY_COLUMNS);
    settings.description3[DISPLAY_COLUMNS]='\0';
    saveSettings();
    }
  else if (strcmp(nme,"description4")==0)
    {
    strncpy(settings.description4,val,DISPLAY_COLUMNS);
    settings.description4[DISPLAY_COLUMNS]='\0';
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
  else if (strcmp(nme,"gmtOffset")==0)
    {
    settings.gmtOffset=atoi(val);
    saveSettings();
    updateClock();
    needRestart=false;
    }
  else if (strcmp(nme,"volume")==0)
    {
    settings.volume=atoi(val);
    if (settings.volume>10) 
      settings.volume=10;
    if (settings.volume<0) 
      settings.volume=0;
    adjustVolume(settings.volume);
    saveSettings();
    needRestart=false;
    }
  else if (strcmp(nme,"debug")==0)
    {
    settings.debug=strcmp(val,"false")==0?false:true;
    saveSettings();
    needRestart=false;
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
    needRestart=false;
    }
  return needRestart;
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
  strcpy(settings.description1,"");
  strcpy(settings.description2,"");
  strcpy(settings.description3,"");
  strcpy(settings.description4,"");
  strcpy(settings.mqttTopic1,DEFAULT_MQTT_TOPIC);
  strcpy(settings.mqttTopic2,"");
  strcpy(settings.mqttTopic3,"");
  strcpy(settings.mqttTopic4,"");
  strcpy(settings.mqttUsername,"");
  strcpy(settings.mqttUserPassword,"");
  strcpy(settings.commandTopic,DEFAULT_MQTT_TOPIC);
  generateMqttClientId(settings.mqttClientId);
  settings.debug=false;
  settings.gmtOffset=DEFAULT_GMT_OFFSET;
  settings.volume=DEFAULT_VOLUME;
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
    strlen(settings.description1)<=DISPLAY_COLUMNS &&
    strlen(settings.description2)<=DISPLAY_COLUMNS &&
    strlen(settings.description3)<=DISPLAY_COLUMNS &&
    strlen(settings.description4)<=DISPLAY_COLUMNS &&
    strlen(settings.mqttTopic1)>0 &&
    strlen(settings.mqttTopic1)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic2)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic3)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.mqttTopic4)<MQTT_MAX_TOPIC_SIZE &&
    strlen(settings.commandTopic)>0 &&
    strlen(settings.commandTopic)<MQTT_MAX_TOPIC_SIZE &&
    settings.brokerPort>0 && settings.brokerPort<65535 &&
    settings.gmtOffset>-24 && settings.gmtOffset<24 &&
    settings.volume>=0 && settings.volume<=10)
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

