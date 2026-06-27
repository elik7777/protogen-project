#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

const int clkPin = 18;
const int dataPin = 23;
const int buzzerPin = 25; 

// АНАЛОГОВІ ПІНИ (ADC1 - працюють з Wi-Fi)
const int analogPin32 = 32; // Основний датчик
const int analogPin33 = 33; // Керуючий датчик

// Ініціалізація 4 каналів матриць
MD_MAX72XX mx1(HARDWARE_TYPE, dataPin, clkPin, 5, 5);
MD_MAX72XX mx2(HARDWARE_TYPE, dataPin, clkPin, 14, 4);
MD_MAX72XX mx3(HARDWARE_TYPE, dataPin, clkPin, 27, 4);
MD_MAX72XX mx4(HARDWARE_TYPE, dataPin, clkPin, 4, 5);

MD_MAX72XX* matrices[] = {&mx1, &mx2, &mx3, &mx4};
const int moduleCount[] = {5, 4, 4, 5};

// Змінні бузера
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDurationMs = 0;

// ЗМІННІ ДЛЯ ЛОГІКИ ІГНОРУВАННЯ (32 та 33 пін)
bool ignoreActive = false;       // Чи увімкнено ігнорування
int ignoreThreshold = 2048;      // Поріг спрацьовування 33 піна
bool ignorePlus = false;         // false = мінус (менше порогу), true = плюс (більше порогу)

AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Control Panel</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; background-color: #2c3e50; color: #ecf0f1; padding: 20px; }
        .card { background: #34495e; padding: 25px; border-radius: 12px; box-shadow: 0 8px 16px rgba(0,0,0,0.3); display: inline-block; max-width: 450px; width: 100%; margin-bottom: 20px; vertical-align: top; margin: 10px; }
        h2 { margin-top: 0; color: #f1c40f; }
        .checkbox-group { text-align: left; background: #2c3e50; padding: 15px; border-radius: 8px; margin: 10px 0; }
        .checkbox-group label { display: block; margin: 10px 0; font-size: 16px; cursor: pointer; }
        input[type="text"], input[type="number"] { width: 90%; padding: 12px; font-size: 16px; border: none; border-radius: 6px; margin-bottom: 15px; background: #ecf0f1; color: #2c3e50; }
        button { color: white; border: none; padding: 14px 20px; font-size: 18px; border-radius: 6px; cursor: pointer; width: 100%; font-weight: bold; margin-top: 5px; }
        .btn-red { background-color: #e74c3c; } .btn-red:hover { background-color: #c0392b; }
        .btn-green { background-color: #2ecc71; } .btn-green:hover { background-color: #27ae60; }
        .btn-blue { background-color: #3498db; } .btn-blue:hover { background-color: #2980b9; }
        
        /* Стилі для аналогової панелі */
        .analog-box { display: flex; justify-content: space-around; margin: 15px 0; }
        .sensor { background: #2c3e50; padding: 15px; border-radius: 8px; width: 40%; }
        .sensor-val { font-size: 32px; font-weight: bold; color: #2ecc71; }
        #val33 { color: #3498db; }
        .settings-panel { display: none; background: #2c3e50; padding: 15px; border-radius: 8px; text-align: left; margin-top: 15px; border: 1px solid #7f8c8d; }
        input[type=range] { width: 100%; margin: 10px 0; }
    </style>
</head>
<body>

    <div class="card">
        <h2>Дані з датчиків</h2>
        <div class="analog-box">
            <div class="sensor">Пін 32<br><span class="sensor-val" id="val32">0</span></div>
            <div class="sensor">Пін 33<br><span class="sensor-val" id="val33">0</span></div>
        </div>
        
        <div class="checkbox-group">
            <label>
                <input type="checkbox" id="chkIgnore" onchange="toggleSettings()"> 
                Ігнорувати дані (Пін 32) за допомогою Піна 33
            </label>
        </div>

        <div id="ignorePanel" class="settings-panel">
            <label>Поріг спрацювання: <span id="thLabel" style="font-weight:bold; color:#f1c40f;">2048</span></label>
            <input type="range" id="sliderTh" min="0" max="4095" value="2048" oninput="updateLabel()" onchange="sendConfig()">
            
            <p style="margin: 10px 0 5px 0;">Ігнорувати, якщо значення 33-го піна:</p>
            <label><input type="radio" name="dir" id="dirMinus" value="0" checked onchange="sendConfig()"> Менше порогу ( - )</label><br>
            <label><input type="radio" name="dir" id="dirPlus" value="1" onchange="sendConfig()"> Більше порогу ( + )</label>
            <p style="font-size: 12px; color: #bdc3c7; margin-top:10px;">*Якщо умова виконана, Пін 32 покаже 4095.</p>
        </div>
    </div>

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
        <h2>Керування Звуком</h2>
        <form action="/buzzer" method="GET">
            <input type="hidden" name="action" value="start">
            <input type="number" name="freq" placeholder="Частота (Гц)" min="100" max="10000" required>
            <input type="number" name="dur" placeholder="Час (секунди)" step="0.1" min="0.1" required>
            <button type="submit" class="btn-green">СТАРТ</button>
        </form>
        <form action="/buzzer" method="GET">
            <input type="hidden" name="action" value="stop">
            <button type="submit" class="btn-blue" style="margin-top: 10px;">СТОП</button>
        </form>
    </div>

    <script>
        // Показати/сховати панель повзунка
        function toggleSettings() {
            let chk = document.getElementById('chkIgnore').checked;
            document.getElementById('ignorePanel').style.display = chk ? 'block' : 'none';
            sendConfig();
        }

        // Оновлення тексту поруч із повзунком під час перетягування
        function updateLabel() {
            document.getElementById('thLabel').innerText = document.getElementById('sliderTh').value;
        }

        // Відправка налаштувань на ESP32 (працює без перезавантаження сторінки)
        function sendConfig() {
            let en = document.getElementById('chkIgnore').checked ? 1 : 0;
            let th = document.getElementById('sliderTh').value;
            let dir = document.getElementById('dirPlus').checked ? 1 : 0; // 0 = мінус, 1 = плюс
            
            fetch(`/setConfig?en=${en}&th=${th}&dir=${dir}`);
        }

        // Запит JSON-даних кожні 0.1 сек
        setInterval(function() {
            fetch('/readAnalog')
                .then(res => res.json())
                .then(data => {
                    document.getElementById('val32').innerText = data.p32;
                    document.getElementById('val33').innerText = data.p33;
                    
                    // Додаємо візуальний ефект: якщо 32 заблоковано (4095), робимо його червоним
                    if(data.p32 == 4095 && document.getElementById('chkIgnore').checked) {
                        document.getElementById('val32').style.color = '#e74c3c';
                    } else {
                        document.getElementById('val32').style.color = '#2ecc71';
                    }
                })
                .catch(err => console.error(err));
        }, 100);
    </script>
</body>
</html>
)rawliteral";


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
  noTone(buzzerPin); 
  pinMode(buzzerPin, OUTPUT); 
  digitalWrite(buzzerPin, LOW); 
}

void setup() {
  Serial.begin(115200);
  
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  for (int i = 0; i < 4; i++) {
    matrices[i]->begin();
    matrices[i]->control(MD_MAX72XX::INTENSITY, 2);
    matrices[i]->clear();
    printText(i, "CH " + String(i + 1)); 
  }

  WiFi.softAP("Matrix-WiFi", "12345678");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // НОВИЙ ЕНДПОЇНТ: Отримання налаштувань логіки ігнорування
  server.on("/setConfig", HTTP_GET, [](AsyncWebServerRequest *request){
    if(request->hasParam("en"))  ignoreActive = (request->getParam("en")->value() == "1");
    if(request->hasParam("th"))  ignoreThreshold = request->getParam("th")->value().toInt();
    if(request->hasParam("dir")) ignorePlus = (request->getParam("dir")->value() == "1");
    
    request->send(200, "text/plain", "OK");
  });

  // ОНОВЛЕНИЙ ЕНДПОЇНТ: Обробка датчиків і пакування в JSON
  server.on("/readAnalog", HTTP_GET, [](AsyncWebServerRequest *request){
    int v32 = analogRead(analogPin32);
    int v33 = analogRead(analogPin33);
    
    // ЛОГІКА ІГНОРУВАННЯ (На рівні мікроконтролера)
    if (ignoreActive) {
      if (ignorePlus) {
        // Ігнорувати якщо 33 БІЛЬШЕ порогу (+)
        if (v33 > ignoreThreshold) v32 = 4095;
      } else {
        // Ігнорувати якщо 33 МЕНШЕ порогу (-)
        if (v33 < ignoreThreshold) v32 = 4095;
      }
    }

    // Формуємо та відправляємо JSON: {"p32": 1234, "p33": 2048}
    String json = "{\"p32\":" + String(v32) + ",\"p33\":" + String(v33) + "}";
    request->send(200, "application/json", json);
  });

  // Матриці
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("text")) {
      String inputTxt = request->getParam("text")->value();
      for (int i = 0; i < 4; i++) {
        String paramName = "m" + String(i);
        if (request->hasParam(paramName)) printText(i, inputTxt);
      }
    }
    request->redirect("/"); 
  });

  // Бузер
  server.on("/buzzer", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("action")) {
      String action = request->getParam("action")->value();
      if (action == "start" && request->hasParam("freq") && request->hasParam("dur")) {
        int freq = request->getParam("freq")->value().toInt();
        float durationSec = request->getParam("dur")->value().toFloat();
        buzzerDurationMs = durationSec * 1000;
        buzzerStartTime = millis();
        buzzerActive = true;
        tone(buzzerPin, freq); 
      } else if (action == "stop") {
        stopBuzzer(); 
      }
    }
    request->redirect("/");
  });

  server.begin();
}

void loop() {
  if (buzzerActive) {
    if (millis() - buzzerStartTime >= buzzerDurationMs) stopBuzzer(); 
  }
}