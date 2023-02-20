#include <ESP8266HTTPClient.h>
#include <iarduino_RTC.h>
#include "DFRobotDFPlayerMini.h"
#include <SoftwareSerial.h>  
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <GyverOLED.h>
#include <ArduinoJson.h>
#include "time.h"
#include <WiFiUdp.h>
#include <NTPClient.h>

const char* ssid = "Sora";
const char* password = "KahaEichi@";
const char* host = "bmstuvoting.herokuapp.com";
const int httpsPort = 443;

SoftwareSerial MS(13, 15); // uart для dfplayer mini

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
iarduino_RTC watch(RTC_DS3231);
DFRobotDFPlayerMini player;

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

void setup(){
    MS.begin(9600);
    Serial.begin(115200);
    oled.init();
    oled.clear(); 

    //draw lines
    oled.line(0, 20, 128, 20);
    oled.line(0, 41, 128, 41);

    //print "loading"
    oled.setScale(2);
    oled.setCursor(20, 3);
    oled.print("loading");

    WiFi.mode(WIFI_OFF);    //Prevents reconnection issue (taking too long to connect)
    WiFi.mode(WIFI_STA);    //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

    WiFi.begin(ssid, password);  //коннектимся к точке доступа
    while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }

    Serial.print("\nConnected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    timeClient.begin();
    timeClient.update();
    

    wificlient.connect(server_addr,40005);
    http.begin(wificlient,"http://62.122.196.47:40005/get/schedule");
    http.GET();
    String bytes = http.getString();
    http.end();
    
    deserializeJson(doc, bytes);
    timeArrSize = atoi(doc["size"]);
    if(timeArrSize>0){
      times = (tm*)calloc(timeArrSize,sizeof(tm));
      for(int i=0; i<timeArrSize; i++) strptime(doc["time"][i],"%H:%M:%S",&times[i]);
    }
    setNextTime(timeArrSize);
    
    if(playerready = player.begin(MS)){
      player.volume(3);
      player.EQ(DFPLAYER_EQ_NORMAL);
      player.outputDevice(DFPLAYER_DEVICE_SD);
    }

}
int upd=0;
//==============================================================================================
//                                       Основной цикл
//==============================================================================================
void loop(){

  delay(1000);
  if(upd==60){
    upd=0;
    timeClient.update();
  }
  if(upd%5==0){
    //if(timeClient.getHours()==testtime.tm_hour&&timeClient.getMinutes()>=testtime.tm_min){
    if(timeClient.getHours()==times[nexttime].tm_hour&&timeClient.getMinutes()>=times[nexttime].tm_min){
      nexttime = (nexttime+1)%timeArrSize;
      wificlient.connect(server_addr,40005);
      http.begin(wificlient,"http://62.122.196.47:40005/get/winner");
      http.GET();
      String bytes = http.getString();
      http.end();
      deserializeJson(doc, bytes);
      int win = atoi(doc["win"]);

      if (playerready){
        player.volume(3); 
        player.playMp3Folder(win);
        
      } 
    }
  }
  upd++;

  oled.setScale(2);
  oled.setCursor(20, 3);
  oled.print(timeClient.getFormattedTime());
  oled.setCursor(20, 6);
  oled.print(stringtime);
  
}