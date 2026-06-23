#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

const int clkPin = 18;
const int dataPin = 23;
const int buzzerPin = 25; // Пін для бузера

// Ініціалізація 4 каналів матриць
MD_MAX72XX mx1(HARDWARE_TYPE, dataPin, clkPin, 5, 5);
MD_MAX72XX mx2(HARDWARE_TYPE, dataPin, clkPin, 14, 4);
MD_MAX72XX mx3(HARDWARE_TYPE, dataPin, clkPin, 27, 4);
MD_MAX72XX mx4(HARDWARE_TYPE, dataPin, clkPin, 4, 5);

MD_MAX72XX* matrices[] = {&mx1, &mx2, &mx3, &mx4};
const int moduleCount[] = {5, 4, 4, 5};

// Змінні для асинхронної роботи бузера
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDurationMs = 0;

AsyncWebServer server(80);

// HTML-сторінка сайту (додано блок керування звуком)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Matrix & Buzzer Control</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; background-color: #2c3e50; color: #ecf0f1; padding: 20px; }
        .card { background: #34495e; padding: 25px; border-radius: 12px; box-shadow: 0 8px 16px rgba(0,0,0,0.3); display: inline-block; max-width: 450px; width: 100%; margin-bottom: 20px; }
        h2 { margin-top: 0; color: #f1c40f; }
        .checkbox-group { text-align: left; background: #2c3e50; padding: 15px; border-radius: 8px; margin: 20px 0; }
        .checkbox-group label { display: block; margin: 10px 0; font-size: 18px; cursor: pointer; }
        input[type="text"], input[type="number"] { width: 90%; padding: 12px; font-size: 16px; border: none; border-radius: 6px; margin-bottom: 15px; background: #ecf0f1; color: #2c3e50; }
        button { color: white; border: none; padding: 14px 20px; font-size: 18px; border-radius: 6px; cursor: pointer; width: 100%; font-weight: bold; margin-top: 5px; }
        .btn-red { background-color: #e74c3c; }
        .btn-red:hover { background-color: #c0392b; }
        .btn-green { background-color: #2ecc71; }
        .btn-green:hover { background-color: #27ae60; }
        .btn-blue { background-color: #3498db; }
        .btn-blue:hover { background-color: #2980b9; }
    </style>
</head>
<body>

    <div class="card">
        <h2>Керування Екранами</h2>
        <form action="/update" method="GET">
            <div class="checkbox-group">
                <label><input type="checkbox" name="m0" value="1" checked> Канал 1 (5 модулів)</label>
                <label><input type="checkbox" name="m1" value="1" checked> Канал 2 (4 модулі)</label>
                <label><input type="checkbox" name="m2" value="1" checked> Канал 3 (4 модулі)</label>
                <label><input type="checkbox" name="m3" value="1" checked> Канал 4 (5 модулів)</label>
            </div>
            <input type="text" name="text" placeholder="Введіть текст..." required>
            <button type="submit" class="btn-red">Відпустити на матриці</button>
        </form>
    </div>

    <div class="card">
        <h2>Керування Звуком (Buzzer)</h2>
        <form action="/buzzer" method="GET">
            <input type="hidden" name="action" value="start">
            <input type="number" name="freq" placeholder="Частота (Гц), наприклад: 1000" min="100" max="10000" required>
            <input type="number" name="dur" placeholder="Час (секунди), наприклад: 2.5" step="0.1" min="0.1" max="60" required>
            <button type="submit" class="btn-green">СТАРТ</button>
        </form>
        <form action="/buzzer" method="GET">
            <input type="hidden" name="action" value="stop">
            <button type="submit" class="btn-blue">СТОП (Зупинити)</button>
        </form>
    </div>

</body>
</html>
)rawliteral";

// Функція для друку тексту
void printText(int ch, String text) {
  matrices[ch]->clear();
  matrices[ch]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF); 

  int currentCol = (moduleCount[ch] * 8) - 1; 

  for (int i = 0; i < text.length(); i++) {
    if (currentCol < 0) break; 
    int charWidth = matrices[ch]->setChar(currentCol, text[i]);
    currentCol -= (charWidth + 1); 
  }

  if (ch == 0 || ch == 1) {
    matrices[ch]->transform(MD_MAX72XX::TFUD); 
    matrices[ch]->transform(MD_MAX72XX::TFLR); 
  }

  matrices[ch]->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON); 
}

void stopBuzzer() {
  buzzerActive = false;
  noTone(buzzerPin);            // Вимикаємо генератор частоти
  pinMode(buzzerPin, OUTPUT);   // Знову перемикаємо пін у режим виходу
  digitalWrite(buzzerPin, LOW); // Жорстко садимо пін на GND, щоб прибрати наводки
}

void setup() {
  Serial.begin(115200);
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // Ініціалізація матриць
  for (int i = 0; i < 4; i++) {
    matrices[i]->begin();
    matrices[i]->control(MD_MAX72XX::INTENSITY, 2);
    matrices[i]->clear();
    printText(i, "CH " + String(i + 1)); 
  }

  // Запуск Wi-Fi
  WiFi.softAP("Matrix-WiFi", "12345678");
  Serial.print("Адреса сайту: ");
  Serial.println(WiFi.softAPIP());

  // Головна сторінка
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Обробка форми матриць
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("text")) {
      String inputTxt = request->getParam("text")->value();
      for (int i = 0; i < 4; i++) {
        String paramName = "m" + String(i);
        if (request->hasParam(paramName)) {
          printText(i, inputTxt);
        }
      }
    }
    request->redirect("/"); 
  });

  // ОБРОБКА ФОРМИ БУЗЕРА (Старт / Стоп)
server.on("/buzzer", HTTP_GET, [](AsyncWebServerRequest *request){
  if (request->hasParam("action")) {
    String action = request->getParam("action")->value();
    
    if (action == "start" && request->hasParam("freq") && request->hasParam("dur")) {
      int freq = request->getParam("freq")->value().toInt();
      float durationSec = request->getParam("dur")->value().toFloat();
      
      buzzerDurationMs = durationSec * 1000;
      buzzerStartTime = millis();
      buzzerActive = true;
      
      tone(buzzerPin, freq); // Вмикаємо звук
      Serial.printf("Buzzer START: %d Hz\n", freq);
    } 
    else if (action == "stop") {
      stopBuzzer(); // Викликаємо наше заземлення
      Serial.println("Buzzer STOP");
    }
  }
  request->redirect("/");
});

  server.begin();
}

void loop() {
  // Асинхронний контроль часу
  if (buzzerActive) {
    if (millis() - buzzerStartTime >= buzzerDurationMs) {
      stopBuzzer(); // Час вийшов — жорстко гасимо бузер
      Serial.println("Buzzer timeout");
    }
  }
}