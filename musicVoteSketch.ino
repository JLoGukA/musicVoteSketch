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

String winner;
//DynamicJsonDocument times(256); // json, который хранит массив дат, в которые проигрывается музыка и звонок
DynamicJsonDocument music(4096); // json, который хранит структуру голосами, для выявляния победителя
SoftwareSerial MS(13, 15); // uart для dfplayer mini
String beginArr[] = { "08:40:00", "10:25:00", "12:50:00", "14:35:00" };// начало пар

GyverOLED<SSD1306_128x64, OLED_NO_BUFFER> oled;
iarduino_RTC watch(RTC_DS3231);
DFRobotDFPlayerMini player;


//MySQL_Connection conn((Client *)&wificlient);

IPAddress server_addr(192,168,3,3);
char dbuser[]="root";
char dbpassword[]="0000";

String hostsite = "http://192.168.3.3:3005/get/schedule";


//================================================================================================
//                                  Установка времени
//================================================================================================
void setDateTime()
{
    const char* strM = "JanFebMarAprMayJunJulAugSepOctNovDec";    // Определяем массив всех вариантов текстового представления текущего месяца находящегося в предопределенном макросе __DATE__.
    const char* sysT = __TIME__;                                  // Получаем время компиляции скетча в формате "SS:MM:HH".
    const char* sysD = __DATE__;                                  // Получаем дату  компиляции скетча в формате "MMM:DD:YYYY", где МММ - текстовое представление текущего месяца, например: Jul.
    //  Парсим полученные значения в массив:                    // Определяем массив «i» из 6 элементов типа int, содержащий следующие значения: секунды, минуты, часы, день, месяц и год компиляции скетча.
    const int i[6]{ (sysT[6] - 48) * 10 + (sysT[7] - 48),
    (sysT[3] - 48) * 10 + (sysT[4] - 48),
    (sysT[0] - 48) * 10 + (sysT[1] - 48),
    (sysD[4] - 48) * 10 + (sysD[5] - 48),
    ((int)memmem(strM,36,sysD,3) + 3 - (int)&strM[0]) / 3,
    (sysD[9] - 48) * 10 + (sysD[10] - 48) };
    watch.settime(i[0] + 40, i[1], i[2], i[3], i[4], i[5]);              // Устанавливаем время
}
//================================================================================================
//                                  Запросы к серверу
//================================================================================================

void getMusic(String tm)
{
    WiFiClientSecure wificlient;
    
    wificlient.setInsecure();//пропустить верификацию (из-за https)
    if (wificlient.connect(host, httpsPort))
    {
        Serial.println("Get music at " + tm);
        // Make a HTTP request:
        wificlient.println("GET https://" + String(host) + "/api_form/music_list/" + tm + " HTTP/1.1");
        wificlient.println("Host: " + String(host));
        wificlient.println("Connection: close");
        wificlient.println();

        while (wificlient.connected())
            if (wificlient.readStringUntil('\n') == "\r") break; // пропускаем хедер

        deserializeJson(music, wificlient); // сохраняем json
    }
    else Serial.println("Connection failed!");
    wificlient.stop();
}

void getTime()
{
    WiFiClientSecure client;
    client.setInsecure();//пропустить верификацию(из-за https)
    if (client.connect(host, httpsPort))
    {
        Serial.println("Get time array");
        // Make a HTTP request:
        client.println("GET https://" + String(host) + "/api_form/time HTTP/1.1");
        client.println("Host: " + String(host));
        client.println("Connection: close");
        client.println();

        while (client.connected())
            if (client.readStringUntil('\n') == "\r") break; // пропускаем хедер

        //deserializeJson(times, client); // сохраняем json
    }
    else Serial.println("Connection failed!");
    client.stop();
}

void resetVotes(String tm) // метод обнуляет голоса в бд в определённый перерыв
{
    WiFiClientSecure client;
    client.setInsecure();//пропустить верификацию (из-за https)
    if (client.connect(host, httpsPort))
    {
        Serial.println("reset votes at " + tm);
        // Make a HTTP request:
        client.println("DELETE https://" + String(host) + "/api_form/erase/" + tm + " HTTP/1.1");
        client.println("Host: " + String(host));
        client.println("Connection: close");
        client.println();
    }
    else Serial.println("Connection failed!");
    client.stop();
}
//================================================================================================
//                                      Определение победителя
//================================================================================================

int Winner() // метод определяет победителя голосования
{
    int index = 0;
    int vote = 0;
    JsonArray Music = music["music"];
    for (int i = 0; i < Music.size(); i++)
    {
        if (Music[i]["vote"].as<int>() >= vote)
        {
            vote = Music[i]["vote"].as<int>();
            winner = Music[i]["name"].as<String>();
            index = i;
        }
    }
    return index + 1;
}

//================================================================================================
//                                        Работа с экраном
//================================================================================================

void drawLines()                 // рисует разметку
{
    oled.line(0, 20, 128, 20);
    oled.line(0, 41, 128, 41);
}
void printDate(String D)        // отображает дату
{
    oled.setScale(2);
    oled.setCursor(5, 0);
    oled.print(D);
}
void printTime(String T)       // отображает время
{
    oled.setScale(2);
    oled.setCursor(15, 3);
    oled.print(T);
}
void printLoading()            // отображает надпись loading
{
    oled.setScale(2);
    oled.setCursor(20, 3);
    oled.print("loading");
}
void printWinner()             // отображает победителя голосования
{
    int index = winner.indexOf(" - ");
    String au = winner.substring(0, index), comp = winner.substring(index + 3, winner.length());
    //строка парсится на автора и композицию и центрируется
    oled.setScale(1);
    oled.setCursor((128 - 6 * au.length()) / 2, 6);
    oled.print(au);
    oled.setCursor((128 - 6 * comp.length()) / 2, 7);
    oled.print(comp);
}
//================================================================================================
//                                      Инициализация прибора
//================================================================================================


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
    drawLines();
    printLoading();

    WiFi.mode(WIFI_OFF);    //Prevents reconnection issue (taking too long to connect)
    WiFi.mode(WIFI_STA);    //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

    WiFi.begin(ssid, password);  //коннектимся к точке доступа
    while (WiFi.status() != WL_CONNECTED) { delay(100); Serial.print("."); }

    Serial.print("\nConnected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    timeClient.begin();
    timeClient.update();
    

    wificlient.connect(server_addr,3005);
    http.begin(wificlient,"http://192.168.40.192:3005/get/schedule");
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
      player.volume(5);
      player.EQ(DFPLAYER_EQ_NORMAL);
      player.outputDevice(DFPLAYER_DEVICE_SD);
    }


    

    
    // setDateTime();  // выставляем время
    // if (!player.begin(MS))   // Проверяем, есть ли связь с плеером 
    // {
    //     Serial.println(F("Please check dfplayer"));
    //     while (true);
    // }
    // player.setTimeOut(500); //устанавливаем время отклика
    // player.volume(3);       //выставляем громкость (от 0 до 30)

    // WiFi.mode(WIFI_OFF);    //Prevents reconnection issue (taking too long to connect)
    // WiFi.mode(WIFI_STA);    //Only Station No AP, This line hides the viewing of ESP as wifi hotspot

    // WiFi.begin(ssid, password);  //коннектимся к точке доступа
    // Serial.println("Connecting");
    // while (WiFi.status() != WL_CONNECTED) { delay(10); Serial.print("."); }

    // Serial.print("\nConnected to WiFi network with IP Address: ");
    // Serial.println(WiFi.localIP());

    // getTime();      // получаем массив дат с БД, для голосования
    // oled.clear();   // очищаем экран  
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
      wificlient.connect(server_addr,3005);
      http.begin(wificlient,"http://192.168.40.192:3005/get/schedule");
      http.GET();
      String bytes = http.getString();
      http.end();
      deserializeJson(doc, bytes);
      int win = atoi(doc["win"]);

      if (playerready){
        player.volume(5); 
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
  

  // Serial.print(daysOfTheWeek[timeClient.getDay()]);
  // Serial.print(", ");
  // Serial.print(timeClient.getHours());
  // Serial.print(":");
  // Serial.print(timeClient.getMinutes());
  // Serial.print(":");
  // Serial.println(timeClient.getSeconds());
    //выводим дату, время и победителя предыдущего голосования на экран
    // drawLines();
    // printDate(watch.gettime("d-m-Y"));
    // printTime(watch.gettime("H:i:s"));
    // printWinner();
    

    // //звонок на пару
    // for (int i = 0; i < 4; i++)
    //     if (beginArr[i] == String(watch.gettime("H:i:s")) && (player.readState() == 2 || player.readState() == 0))
    //         player.play(6);

    // //пробегаемся по массиву перерывов. Если время настало, 
    // //то воспроизводим звонок, узнаём победителя голосования, воспроизводим музыку
    // //и обнуляем будущее голосование в БД
    // JsonArray Times = times["time"].as<JsonArray>();
    // for (int i = 0; i < Times.size(); i++)
    // {
    //     if (String(watch.gettime("s")) == "00" && String(watch.gettime("H:i")) == (Times[i].as<String>()))
    //     {
    //         player.play(6);
    //         oled.clear();
    //         drawLines();
    //         printLoading();

    //         if (i != Times.size() - 1)
    //             resetVotes(Times[i + 1].as<String>());
    //         else
    //             resetVotes(Times[0].as<String>());

    //         getMusic(Times[i].as<String>());
    //         while (player.readState() != 0); // ждём когда закончится звонок
    //         delay(5000);                    // задержка между звонком и композицией
    //         player.play(Winner());
    //     }
    // }
    // delay(1);
}
