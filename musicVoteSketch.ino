#include <iarduino_RTC.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include "FS.h"
#include "SD_MMC.h"

const char* ssid = "Sora";
const char* password = "KahaEichi@";
const char* host = "bmstuvoting.herokuapp.com";
const int httpsPort = 443;

//SoftwareSerial MS(13, 15); // uart для dfplayer mini

//GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
iarduino_RTC watch(RTC_DS3231);
//DFRobotDFPlayerMini player;

IPAddress server_addr(62,122,196,47);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "3.ru.pool.ntp.org", 10800);

WiFiClient wificlient;
HTTPClient http;
StaticJsonDocument<200> doc;
tm* times=NULL;
int timeArrSize=0;

int nexttime;
bool playerready=0;
String* tmm=NULL;

char* stringtime=(char*)calloc(9,sizeof(char));

void setStringTime(char* c, tm* Time ){
  c[2] = ':';
  c[5] = ':';
  c[0] = Time->tm_hour/10+48;
  c[1] = Time->tm_hour%10+48;
  c[3] = Time->tm_min/10+48;
  c[4] = Time->tm_min%10+48;
  c[6] = Time->tm_sec/10+48;
  c[7] = Time->tm_sec%10+48;
  c[8] = '\0';
}

void setNextTime(int timeArrSize){
  int hrs = timeClient.getHours();
  int min = timeClient.getMinutes();

  for(int i=0; i<timeArrSize; i++){
    //times[i].tm_min>=min&&
    if(times[i].tm_hour>=hrs&&!(times[i].tm_hour==hrs&&times[i].tm_min==min)){
      nexttime=i; break;
    }
  }
  setStringTime(stringtime,&times[nexttime]);
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if(!root){
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      
    }
    file = root.openNextFile();
  }
}

void getDir(fs::FS &fs, const char * dirname, uint8_t levels){
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if(!root){
    Serial.println("Failed to open directory");
    return;
  }
  if(!root.isDirectory()){
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if(levels){
        listDir(fs, file.name(), levels -1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
      
    }
    file = root.openNextFile();
  }
}

void setup(){
  Serial.begin(115200);
  if(!SD_MMC.begin()){
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);

  Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));

    // oled.init();
    // oled.clear(); 

    // //draw lines
    // oled.line(0, 20, 128, 20);
    // oled.line(0, 41, 128, 41);

    // //print "loading"
    // oled.setScale(2);
    // oled.setCursor(20, 3);
    // oled.print("loading");

  WiFi.mode(WIFI_OFF);    //Prevents reconnection issue (taking too long to connect)
  WiFi.mode(WIFI_STA);    //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

  WiFi.begin(ssid, password);  //коннектимся к точке доступа
  while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }

    // Serial.print("\nConnected to WiFi network with IP Address: ");
    // Serial.println(WiFi.localIP());

    // timeClient.begin();
    // timeClient.update();
    

    // wificlient.connect(server_addr,40005);
    // http.begin(wificlient,"http://62.122.196.47:40005/get/schedule");
    // http.GET();
    // String bytes = http.getString();
    // http.end();
    
    // deserializeJson(doc, bytes);
    // timeArrSize = atoi(doc["size"]);
    // if(timeArrSize>0){
    //   times = (tm*)calloc(timeArrSize,sizeof(tm));
    //   for(int i=0; i<timeArrSize; i++) strptime(doc["time"][i],"%H:%M:%S",&times[i]);
    // }
    // setNextTime(timeArrSize);
    
    // if(playerready = player.begin(MS)){
    //   player.volume(3);
    //   player.EQ(DFPLAYER_EQ_NORMAL);
    //   player.outputDevice(DFPLAYER_DEVICE_SD);
    // }
  listDir(SD_MMC, "/", 0);
  
}
int upd=0;
//==============================================================================================
//                                       Основной цикл
//==============================================================================================
void loop(){
  
  // delay(1000);
  // if(upd==60){
  //   upd=0;
  //   timeClient.update();
  // }
  // if(upd%5==0){
  //   //if(timeClient.getHours()==testtime.tm_hour&&timeClient.getMinutes()>=testtime.tm_min){
  //   if(timeClient.getHours()==times[nexttime].tm_hour&&timeClient.getMinutes()>=times[nexttime].tm_min){
  //     nexttime = (nexttime+1)%timeArrSize;
  //     wificlient.connect(server_addr,40005);
  //     http.begin(wificlient,"http://62.122.196.47:40005/get/winner");
  //     http.GET();
  //     String bytes = http.getString();
  //     http.end();
  //     deserializeJson(doc, bytes);
  //     int win = atoi(doc["win"]);

  //     if (playerready){
  //       player.volume(3); 
  //       player.playMp3Folder(win);
        
  //     } 
  //   }
  // }
  // upd++;

  // oled.setScale(2);
  // oled.setCursor(20, 3);
  // oled.print(timeClient.getFormattedTime());
  // oled.setCursor(20, 6);
  // oled.print(stringtime);
  
}