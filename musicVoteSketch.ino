//#include <iarduino_RTC.h>
#include <Audio.h>
#include <AsyncTCP.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <ESPDateTime.h>
#include <ESPAsyncWebServer.h>
#include "esp_task_wdt.h"
#include "FS.h"
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include "SD_MMC.h"
#include <WiFiUdp.h>
#include <WiFiClient.h>

#include <config.h>
#include <ctime>
#include <iostream>
#include <string>
#include "time.h"
#include <vector>

using namespace std;
using namespace fs;

//standard config
int I2S_LRC = 0; 
int I2S_BCLK= 1;
int I2S_DOUT =3;
int DEVICE_VOLUME =15;

int DEVICE_ID =21;
String AP_NAME ="ESP32_21";
String AP_PASS ="ESP32_21";
String ssid = "SoraMM";
String password = "KahaEichi@";
String server_ip = "192.168.228.77";
int server_port = 3005;
String ntpServer = "3.ru.pool.ntp.org";
int ntpAddTime=10800;//+3 hours

//set up local time client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer.c_str(),ntpAddTime);
WiFiClient wificlient;

//Информация о подключении на странице точки доступа
String apInfo{""};

StaticJsonDocument<512> configBuffer;//Буффер для считывания конфиг-файла с карты памяти

File file;
AsyncWebServer server(80);//Локальный веб-сервер
vector<tm> timeSchedule{};//Расписание времени
vector<String> fileSchedule{};//Расписание файлов по времени
int nowPlaying{};//Играет ли сейчас плеер
int playTimeNext{};//Позиция следующих к проигрышу песни и времени в fileSchedule, timeSchedule
String whatPlaying{};//Полная директория проигрываемого файла
Audio audio;//Обьект для проигрывания аудио с помощью I2S 


volatile SemaphoreHandle_t timerSemaphore;//Семафор для проверки сработал ли хардверный таймер hw_timer_t
hw_timer_t * dateTimer = NULL;//Таймер для сверки времени с расписанием
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t isrCounter = 0;//Счетчик срабатываний таймера

string memoryJson{},currentPath{};//Информация об устройстве и временная переменная
int countFile{-1};//Сколько найдено полных директорий

//Сохраняет информацию о всех полных директориях в карте памяти в строку memoryJson
//в виде незаконченного JSON, законченным он становится после вызова функции memoryToJsonComplete
void memoryToJsonNotComplete(File dir) {
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      currentPath.pop_back();
      while(*(currentPath.end()-1)!='/'&&!currentPath.empty())currentPath.pop_back();
      break;
    }

    if (entry.isDirectory()) {
      currentPath+=entry.name();
      currentPath+="/";

      memoryToJsonNotComplete(entry);
    } 
    else {
      // memoryJson.append("\"file"+to_string(++countFile)+"\": \"");
      // memoryJson.append(currentPath+entry.name());
      // memoryJson.append("\",\n");
      countFile+=1;
      memoryJson.append("\""+currentPath+entry.name()+"\",\n");
    }
    entry.close();
  }
}

//Добавляет в memoryJson id,ip устройства, заканчивает JSON строку.
//Если карта памяти пустая, в разделе файлов будет только "file0":"/"
void memoryToJsonComplete(){
  File root=SD_MMC.open("/");
  
  countFile=-1;
  memoryJson="{\n \"files\":[\n";
  currentPath="/";
  root=SD_MMC.open("/");
  memoryToJsonNotComplete(root);
  if(countFile==-1){
    //memoryJson.append("\n \"file0\":\"/\",\n");
    memoryJson.append("\"/\"\n");
  }
  else {
    memoryJson.pop_back();//убрать последнюю запятую
    memoryJson[memoryJson.size()-1]='\n';
  }
  memoryJson.append("],\n");
  
  
  memoryJson.append("\"ID\": \""+to_string(DEVICE_ID)+"\",\n");
  memoryJson.append("\"IP\": \""+string(WiFi.localIP().toString().c_str())+"\",\n");
  //Пишем конфиг в JSON
  memoryJson.append("\"volume\": \""+to_string(DEVICE_VOLUME)+"\",\n");
  memoryJson.append("\"LRC\": \""+to_string(I2S_LRC)+"\",\n");
  memoryJson.append("\"BCLK\": \""+to_string(I2S_BCLK)+"\",\n");
  memoryJson.append("\"DOUT\": \""+to_string(I2S_DOUT)+"\",\n");
  memoryJson.append("\"ssid\": \"\",\n");
  memoryJson.append("\"password\": \"\",\n");
  memoryJson.append("\"serverIP\": \""+string(server_ip.c_str())+"\",\n");
  memoryJson.append("\"serverPort\": \""+to_string(server_port)+"\",\n");
  memoryJson.append("\"apName\": \""+string(AP_NAME.c_str())+"\",\n");
  memoryJson.append("\"apPass\": \""+string(AP_PASS.c_str())+"\",\n");
  memoryJson.append("\"ntpServer\": \""+string(ntpServer.c_str())+"\",\n");
  memoryJson.append("\"ntpAddTime\": \""+to_string(ntpAddTime)+"\"\n");
  memoryJson.append("}");
}

//Отправляет информацию об этом устройстве на сервер
void sendMemoryInfo(){
  if(WiFi.getMode()==WIFI_AP_STA)return;
  memoryToJsonComplete();
  HTTPClient http;
  WiFiClient wificlient;
  wificlient.connect(server_ip.c_str(),server_port);
  http.begin(wificlient,"http://"+server_ip+":"+String(server_port)+"/device/sendInfo");
  http.addHeader("Content-Type", "application/JSON");
  http.POST(memoryJson.c_str());
  http.getString();//прочитать весь контент перед закрытием соединения
  http.end();
}

//Отправляет сообщение на сервер о том, что проигрывание закончено
void sendStopMusic(){
  if(WiFi.getMode()==WIFI_AP_STA)return;
  HTTPClient http;
  WiFiClient wificlient;
  wificlient.connect(server_ip.c_str(),server_port);
  http.begin(wificlient,"http://"+server_ip+":"+String(server_port)+"/device/PlayNow");
  http.addHeader("Content-Type", "text/plain");
  http.addHeader("id",to_string(DEVICE_ID).c_str());
  http.addHeader("stop","stop");
  http.GET();
  http.getString();
  http.end();
}

//Отправляет кастомное сообщение на сервер. Текст под заголовком "message" логгируется сервером.
void sendCustomMessage(string message){
  if(WiFi.getMode()==WIFI_AP_STA)return;
  HTTPClient http;
  WiFiClient wificlient;
  wificlient.connect(server_ip.c_str(),server_port);
  http.begin(wificlient,"http://"+server_ip+":"+String(server_port)+"/device/sendCustomMessage");
  http.addHeader("Content-Type", "text/plain");
  http.addHeader("message",message.c_str());
  http.GET();
  http.getString();
  http.end();
}

//Для управления загрузкой файла на устройство.
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    if(request->hasHeader("file")){
      AsyncWebHeader* h{request->getHeader("file")};
      cout<<h->value().c_str()<<endl;
      if(file)file.close();
      file=SD_MMC.open(h->value().c_str(),FILE_WRITE);
    }
  }
  for(size_t i=0; i<len; i++){
    file.write(data[i]);
  }
  if(final){
    file.close();
    request->send(200, "text/plain","Uploaded");
    sendMemoryInfo();
  }
}

//Выбор следующей позиции playTimeNext для проигрывания из векторов расписания timeSchedule, fileSchedule
void setNextPlayTime(){
  timeClient.update();
    time_t rawtime = timeClient.getEpochTime();
    struct tm * ti;
    ti=localtime(&rawtime);
    ti->tm_mon+=1;
    
    playTimeNext=timeSchedule.size();

    for(int i{}; i<timeSchedule.size();i++){
      if(timeSchedule[i].tm_year==ti->tm_year&&timeSchedule[i].tm_mon==ti->tm_mon&&timeSchedule[i].tm_mday==ti->tm_mday){//Если текущий год && текущий месяц && текущий день
        if((timeSchedule[i].tm_hour==ti->tm_hour&&(timeSchedule[i].tm_min>ti->tm_min))||timeSchedule[i].tm_hour>ti->tm_hour){//Если (текущий час && будущая минута) || будущий час
          playTimeNext=i;
          break;
        }
      }
      if(timeSchedule[i].tm_year>ti->tm_year||timeSchedule[i].tm_mon>ti->tm_mon&&timeSchedule[i].tm_mday>ti->tm_mday){//Если следующий год || следующий месяц || следующий день
        playTimeNext=i;
        break;
      }
    }
    sendCustomMessage("CHOOSE: "+to_string(playTimeNext));
}

//Увеличение числа срабатываний таймера
void checkPlayTime(){
  portENTER_CRITICAL_ISR(&timerMux);
  isrCounter++;
  portEXIT_CRITICAL_ISR(&timerMux);
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
}

//Включает точку доступа если устройство отключено от wifi или не подключилось к серверу, возвращает 1
//Иначе возвращает 0
bool WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  if(WiFi.status()!=WL_CONNECTED){
    apInfo="Не удалось подключиться к WiFi";
    if(WiFi.getMode()!=WIFI_AP_STA){
      WiFi.softAP(AP_NAME,AP_PASS);
      IPAddress a(192,168,0,1);
      IPAddress g(192,168,0,254);
      IPAddress m(255,255,255,0);
      WiFi.softAPConfig(a,g,m);
      WiFi.mode(WIFI_AP_STA);
      WiFi.enableAP(1);
    }
  }
  else if(wificlient.connect(server_ip.c_str(),server_port)){
    // timeClient.update();
    // sendMemoryInfo();
    return 0;
  }
  else {
    apInfo="Не найден сервер по адресу: "+server_ip+":"+server_port;
    if(WiFi.getMode()!=WIFI_AP_STA){
      WiFi.softAP(AP_NAME,AP_PASS);
      IPAddress a(192,168,0,1);
      IPAddress g(192,168,0,254);
      IPAddress m(255,255,255,0);
      WiFi.softAPConfig(a,g,m);
      WiFi.mode(WIFI_AP_STA);
      WiFi.enableAP(1);
    }
  }
  return 1;
}

void updateConfig(){
  File configFile=SD_MMC.open("/config.txt","w");
  if(configFile){
    
    configBuffer["id"]=String(DEVICE_ID);
    configBuffer["volume"]=String(DEVICE_VOLUME);
    configBuffer["LRC"]=String(I2S_LRC);
    configBuffer["BCLK"]=String(I2S_BCLK);
    configBuffer["DOUT"]=String(I2S_DOUT);
    configBuffer["ssid"]=ssid;
    configBuffer["password"]=password;
    configBuffer["server_ip"]=server_ip;
    configBuffer["server_port"]=String(server_port);
    configBuffer["ap_name"]=AP_NAME;
    configBuffer["ap_pass"]=AP_PASS;
    configBuffer["ntpServer"]=ntpServer;
    configBuffer["ntpAddTime"]=String(ntpAddTime);
    serializeJsonPretty(configBuffer,configFile);
    file.close();
  }
}

void setup(){
  //Serial.begin(115200);
  // uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  // Serial.printf("SD Card Size: %lluMB\n", cardSize);

  // Serial.printf("Total space: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  // Serial.printf("Used space: %lluMB\n", SD_MMC.usedBytes() / (1024 * 1024));
  if(!SD_MMC.begin()){
    return;
  }

  String str{};
  //Считывание конфиг файла
  File configFile=SD_MMC.open("/config.txt");
  if(configFile){
    //читаем файл до конца
    // while(configFile.available()){
    //   str+=(char)configFile.read();
    // }
    //парсим как JSON
    DeserializationError err = deserializeJson(configBuffer,configFile);
    configFile.close();
    if(err==0){
      DEVICE_ID=atoi((const char*)configBuffer["id"]);
      DEVICE_VOLUME=atoi((const char*)configBuffer["volume"]);
      I2S_LRC=atoi((const char*)configBuffer["LRC"]);
      I2S_BCLK=atoi((const char*)configBuffer["BCLK"]);
      I2S_DOUT=atoi((const char*)configBuffer["DOUT"]);
      ssid= (const char*)configBuffer["ssid"];
      password=(const char*)(configBuffer["password"]);
      server_ip=(const char*)(configBuffer["server_ip"]);
      server_port=atoi(configBuffer["server_port"]);
      AP_NAME=(const char*)(configBuffer["ap_name"]);
      AP_PASS=(const char*)(configBuffer["ap_pass"]);
      ntpServer=(const char*)(configBuffer["ntpServer"]);
      ntpAddTime=atoi(configBuffer["ntpAddTime"]);
      str.clear();
    }
    else{
      File errFile = SD_MMC.open("/workLog.txt","a");
      if(errFile){
        String serr="Config file read error: "+String(err.c_str());
        errFile.println(serr);
        errFile.close();
      }
    }
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(to_string(DEVICE_ID).c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int wait{};
  while (WiFi.status() != WL_CONNECTED) { 
    delay(90); 
    wait+=90;
    if(wait>=18000){
      arduino_event_id_t e{ARDUINO_EVENT_WIFI_STA_LOST_IP};
      arduino_event_info_t info{};
      //WiFiStationDisconnected(e,info);
      break;
    }
  }
  if(!wificlient.connect(server_ip.c_str(),server_port)){
    arduino_event_id_t e{ARDUINO_EVENT_WIFI_STA_LOST_IP};
    arduino_event_info_t info{};
    //WiFiStationDisconnected(e,info);
  }
  
  timeClient.begin();
  audio.setPinout(I2S_BCLK,I2S_LRC,I2S_DOUT);
  audio.setVolume(DEVICE_VOLUME);

  //For playing files on demand. If "file" header =="STOP" or nowPlaying==1 then stops playing.
  //"repeat" header set if need to repeat file.
  server.on("/playNow",HTTP_GET,[](AsyncWebServerRequest *request){

    if(nowPlaying==1){
      nowPlaying=0;
      whatPlaying="";
      audio.stopSong();
      request->send(200,"text/plain","stop");
    }
    else if(request->hasHeader("file")){
      AsyncWebHeader* h{request->getHeader("file")};
      if(h->value()=="STOP"){
        nowPlaying=0;
        whatPlaying="";
        audio.stopSong();
        request->send(200,"text/plain","stop");
        
      }
      else{
        int repeat{};
        
        if(request->hasHeader("repeat"))repeat = atoi(request->getHeader("repeat")->value().c_str());
        nowPlaying=1;
        whatPlaying=h->value();
        audio.connecttoFS(SD_MMC,h->value().c_str());
        audio.loop();
        
        request->send(200, "text/plain",h->value());
      }
    }
    else{
      request->send(400);
    }
  });

  server.on("/hasFile",HTTP_GET,[](AsyncWebServerRequest *request){
    if(request->hasHeader("file")){
      AsyncWebHeader* h{request->getHeader("file")};
      File hasFile=SD_MMC.open(h->value().c_str(),FILE_READ);
      if(hasFile){
        hasFile.close();
        request->send(200,"text/plain","1");
      }
      else{
        hasFile.close();
        request->send(200,"text/plain","0");
      }
    }
    request->send(400);
  });

  //send name of file playing, empty string if player stopped
  server.on("/whatPlaying",HTTP_GET,[](AsyncWebServerRequest *request){
    request->send(200,"text/plain",whatPlaying);
  });

  //handle uploading file to device
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
  },handleUpload);

  //Жив ли локальный вебсервер?
  server.on("/isAlive",HTTP_GET,[](AsyncWebServerRequest *request){request->send(200, "text/plain","Alive");});

  //Отправить на сервер информацию об этом устройстве
  server.on("/getResources",HTTP_GET,[](AsyncWebServerRequest *request){
    sendMemoryInfo();
    request->send(200);
  });

  //sends file if filename under "file" header can be opened, sends 404 otherwise.
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    
    if(request->hasHeader("file")){
      AsyncWebHeader* h{request->getHeader("file")};
      if(file)file.close();
      file=SD_MMC.open(h->value().c_str(),FILE_READ);
      
      if(!file){
        request->send(404);
      }
      else{
        AsyncWebServerResponse *response = request->beginResponse(SD_MMC,h->value().c_str(),String(),true); 
        request->send(response);
      }
    }
  });

  //deletes file if "dir" header is present, sends 404 otherwise
  server.on("/delete",HTTP_GET,[](AsyncWebServerRequest *request){
    if(request->hasHeader("dir")){
      AsyncWebHeader* h{request->getHeader("dir")};
      if(SD_MMC.exists(h->value())){
        File dr=SD_MMC.open(h->value());
        if(dr.isDirectory()){
          if(SD_MMC.rmdir(h->value()))request->send(200);
          else request->send(404);
        }
        else {
          if(SD_MMC.remove(h->value()))request->send(200);
          else request->send(404);
        }
      }
      else request->send(404);
    }
    sendMemoryInfo();
  });

  server.on("/apConfig",HTTP_GET,[](AsyncWebServerRequest *request){
    if(request->hasParam("input1")){
      apInfo="Логин принят";
      ssid=request->getParam("input1")->value();
    }
    if(request->hasParam("input2")){
      apInfo="Пароль принят";
      password=request->getParam("input2")->value();
    }
    if(request->hasParam("input3")){
      apInfo="Адрес сервера принят";
      server_ip=request->getParam("input3")->value();

    }
    if(request->hasParam("input4")){
      apInfo="Порт сервера принят";
      server_port=atoi(request->getParam("input4")->value().c_str());
    }
    if(request->hasParam("input5")){
      arduino_event_id_t e{ARDUINO_EVENT_WIFI_STA_LOST_IP};
      arduino_event_info_t info{};
      WiFi.disconnect();
      WiFi.begin(ssid.c_str(), password.c_str());
      delay(1000);
      
      if(!WiFiStationDisconnected(e,info)){
        updateConfig();
        apInfo="Устройство подключено к серверу, настройки сохранены. Точка доступа будет отключена.";
        String page = String("")+"<!DOCTYPE HTML><html><head>\n"+
        "<title>ESP Input Form</title>\n"+
        "<meta charset=\"UTF-8\" name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"+
        "</head><body>"+
        "<p style=\"color:red;\">Устройство подключено к серверу, настройки сохранены. Точка доступа будет отключена.</p>"+
        "</body></html>";
        request->send(200,"text/html",page);
        ESP.restart();
      }
    }
    String h="";
    String page = h+"<!DOCTYPE HTML><html><head>\n"+
      "<title>ESP Input Form</title>\n"+
      "<meta charset=\"UTF-8\" name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"+
      "</head><body>"+
      "<p style=\"color:red;\">"+apInfo+"</p>"+
      "<form action=\"/apConfig\">"+
        "Логин: <input type=\"text\" name=\"input1\" placeholder=\"" + ssid+"\">"+
        "<input type=\"submit\" value=\"Отправить\">"+
      "</form><br>"+
      "<form action=\"/apConfig\">"+
        "Пароль: <input type=\"text\" name=\"input2\" placeholder=\"" + password+"\">"+
        "<input type=\"submit\" value=\"Отправить\">"+
      "</form><br>"+
      "<form action=\"/apConfig\">"+
        "Адрес сервера: <input type=\"text\" name=\"input3\" placeholder=\"" + server_ip+"\">"+
        "<input type=\"submit\" value=\"Отправить\">"+
      "</form><br>"
      "<form action=\"/apConfig\">"+
        "Порт сервера: <input type=\"text\" name=\"input4\" placeholder=\"" + String(server_port)+"\">"+
        "<input type=\"submit\" value=\"Отправить\">"+
      "</form><br>"
      "<form action=\"/apConfig\">"+
        "<input type=\"hidden\" id=\"connect\" name=\"input5\">"+
        "<input type=\"submit\" value=\"Подключиться\">"+
      "</form><br>"+
      "</body></html>";
    request->send(200,"text/html",page);

  });

  //Получает JSON с датой и соответствующим файлом
  //Дата в формате %mm/%dd/%YYYY/%HH/%ii. Пример двух дат в JSON: ["03/25/2023/22/40","04/18/2023/13/44"].
  AsyncCallbackJsonWebHandler* scheduleHandler = new AsyncCallbackJsonWebHandler("/setSchedule", [](AsyncWebServerRequest *request, JsonVariant &json) {

    const JsonObject &jsonObj = json.as<JsonObject>();

    String dateStr = jsonObj["date"];
    String fileStr = jsonObj["file"];
    
    if(!dateStr.isEmpty()&&!fileStr.isEmpty()){
      tm time{};
      char* t = (char*)malloc(5*sizeof(char));
      timeSchedule.clear();
    
      //Расстояние между первыми символами каждой даты - 19 символов
      int ch{};
      for(int i{}; i<jsonObj["date"].size(); i++){
        ch=19*i;
        t[0]=dateStr[2+ch];
        t[1]=dateStr[3+ch];
        t[2]='\0';
        time.tm_mon=atoi(t);
        t[0]=dateStr[5+ch];t[1]=dateStr[6+ch];t[2]='\0';
        time.tm_mday=atoi(t);
        t[0]=dateStr[8+ch];t[1]=dateStr[9+ch];t[2]=dateStr[10+ch];t[3]=dateStr[11+ch];t[4]='\0';
        time.tm_year=atoi(t);
        t[0]=dateStr[13+ch];t[1]=dateStr[14+ch];t[2]='\0';
        time.tm_hour=atoi(t);
        t[0]=dateStr[16+ch];t[1]=dateStr[17+ch];
        time.tm_min=atoi(t);
        timeSchedule.push_back(time);
      }
      fileSchedule.clear();
      int pos{},pos2{};
      while(true){
        pos=fileStr.indexOf('"');
        if(pos!=std::string::npos)fileStr[pos]='{';
        else break;
        pos2=fileStr.indexOf('"');
        if(pos2!=std::string::npos){
          fileStr[pos2]='}';
          fileSchedule.push_back(fileStr.substring(pos+1,pos2));
        }
        else break;
      }
      free(t);

      setNextPlayTime();
      request->send(200);
    }
    else {
      request->send(400);
    }
  });
  server.addHandler(scheduleHandler);

  //Получает конфиг в виде JSON, пишет его в карту памяти по пути /config.txt
  AsyncCallbackJsonWebHandler* configHandler = new AsyncCallbackJsonWebHandler("/setConfig", [](AsyncWebServerRequest *request, JsonVariant &json) {
    
    if(json.containsKey("id")){
      DEVICE_ID=atoi(json["id"]);
      DEVICE_VOLUME=atoi(json["volume"]);
      I2S_LRC=atoi(json["LRC"]);
      I2S_BCLK=atoi(json["BCLK"]);
      I2S_DOUT=atoi(json["DOUT"]);
      ssid=(const char*)(json["ssid"]);
      password=(const char*)(json["password"]);
      server_ip=(const char*)(json["server_ip"]);
      server_port=atoi(json["server_port"]);
      AP_NAME=(const char*)(json["ap_name"]);
      AP_PASS=(const char*)(json["ap_pass"]);
      ntpServer=(const char*)(json["ntpServer"]);
      ntpAddTime=atoi(json["ntpAddTime"]);
      File configFile=SD_MMC.open("/config.txt","w");
      if(configFile){
        serializeJsonPretty(json,configFile);
        file.close();
      }
      request->send(200);
    }
    else request->send(400);
  });
  server.addHandler(configHandler);

  server.begin();

  sendMemoryInfo();

  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  //Установка таймера dateTimer на срабатывание каждую секунду
  timerSemaphore=xSemaphoreCreateBinary();
  dateTimer = timerBegin(0,80,true);
  timerAttachInterrupt(dateTimer,&checkPlayTime,true);
  timerAlarmWrite(dateTimer, 1000000, true);
  timerAlarmEnable(dateTimer);
}

void loop(){
  if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE){
    uint32_t isrCount = 0;
    portENTER_CRITICAL(&timerMux);
    if(isrCounter==30){
      isrCount = isrCounter;
      isrCounter=0;
    }
    portEXIT_CRITICAL(&timerMux);
    if(nowPlaying&&audio.getAudioFileDuration()!=0&&audio.getAudioCurrentTime()!=0){
      if(audio.getAudioFileDuration()<=audio.getAudioCurrentTime()){
        nowPlaying=0;
        sendStopMusic();
      }
    }
    if(isrCount==30&&playTimeNext<timeSchedule.size()){
      if(timeClient.getHours()==timeSchedule[playTimeNext].tm_hour&&timeClient.getMinutes()==timeSchedule[playTimeNext].tm_min){
        sendCustomMessage("TIME:"+to_string(timeSchedule[playTimeNext].tm_hour)+":"+to_string(timeSchedule[playTimeNext].tm_min));
        audio.setVolume(15);
        audio.connecttoFS(SD_MMC,fileSchedule[playTimeNext].c_str());
        nowPlaying=1;
        setNextPlayTime();
      }
    }
  }
  if(nowPlaying){
    audio.loop();
  }

}