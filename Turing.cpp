#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <Wire.h>
#include "Adafruit_TCS34725.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==========================================
// CONFIGURACIÓN DE PANTALLA OLED
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
// Dirección I2C: 0x3C es lo normal para 128x64. Si no va, prueba 0x3D.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==========================================
// CONFIGURACIÓN DE ZONA (35 -> 9)
// ==========================================
#define LOGICAL_START_LED 35     

// --- MOTOR (A4988) ---
#define PIN_STEP    14
#define PIN_DIR     12
#define PIN_ENABLE  27
#define PIN_SWITCH  4 

#define STEPS_PER_MM 80    
#define VEL_HOMING   300   
#define VEL_VIAJE    200   

#define OFFSET_MM 0       
#define CELL_PITCH_MM 13.8 

// --- LEDS (WS2812B) ---
#define LED_PIN     23    
#define NUM_LEDS    142
#define CELLS       13    
#define BRIGHTNESS  30    
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// --- SENSOR ---
#define PIN_SENSOR_LED 33 

// ==========================================
// VARIABLES GLOBALES
// ==========================================
CRGB leds[NUM_LEDS];
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
WebServer server(80);

const char* ssid = "Nicowo";
const char* password = "12345678";

// --- ESTADOS DE LA MÁQUINA ---
int tape[CELLS];
int head = 0;
int state = 0;
bool running = false;
long currentMotorStep = 0;
String currentOpDesc = "ESPERANDO..."; // Texto para la pantalla

// --- GESTIÓN DE MODOS ---
enum Effect { FX_TURING, FX_RAINBOW, FX_FADE, FX_CONFETTI, FX_OFF };
Effect currentEffect = FX_TURING; 
uint8_t gHue = 0; 

CRGB cZero = CRGB::Blue;
CRGB cOne  = CRGB::Green;
CRGB cHead = CRGB::Red;

// ==========================================
// 0. GESTIÓN DE PANTALLA
// ==========================================

void updateScreen() {
  display.clearDisplay();
  
  // --- BARRA SUPERIOR (AMARILLA EN ALGUNAS PANTALLAS) ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("IP: "); 
  display.println(WiFi.softAPIP());
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // --- OPERACIÓN ---
  display.setCursor(0, 15);
  display.print("OP: ");
  display.println(currentOpDesc);

  // --- ESTADO GRANDE ---
  display.setCursor(0, 35);
  display.setTextSize(2); // Texto Grande
  if(state == 0) display.print("q0 Ready");
  else if(state == 99) display.print("qHalt");
  else {
    display.print("q"); 
    display.print(state);
    // Indicador de actividad
    if(running) display.print(" RUN"); else display.print(" PAUSE");
  }

  // --- CABEZAL ---
  display.setTextSize(1);
  display.setCursor(80, 55);
  display.print("Head: "); display.print(head);

  display.display();
}

// ==========================================
// 1. FUNCIONES DE EFECTOS VISUALES
// ==========================================

void runRainbow() {
  fill_rainbow(leds, NUM_LEDS, gHue, 7);
  FastLED.show();
  EVERY_N_MILLISECONDS(20) { gHue++; }
}

void runFade() {
  float breath = (exp(sin(millis()/2000.0*PI)) - 0.36787944)*108.0;
  fill_solid(leds, NUM_LEDS, CHSV(140, 255, breath)); 
  FastLED.show();
}

void runConfetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(gHue + random8(64), 200, 255);
  FastLED.show();
  gHue++;
}

// ==========================================
// 2. MOTOR
// ==========================================
void moveSteps(long steps) {
  if (steps == 0) return;
  
  bool direction = (steps < 0); 
  digitalWrite(PIN_DIR, direction ? HIGH : LOW);
  
  steps = abs(steps);
  for(long i=0; i<steps; i++) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(VEL_VIAJE);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(VEL_VIAJE);
    if(i % 1000 == 0) server.handleClient();
  }
}

void goToCell(int cellIndex) {
  if (cellIndex >= CELLS) cellIndex = CELLS - 1;
  if (cellIndex < 0) cellIndex = 0;

  float targetMM = OFFSET_MM + (cellIndex * CELL_PITCH_MM);
  long targetStep = targetMM * STEPS_PER_MM;
  
  long diff = targetStep - currentMotorStep;
  moveSteps(diff);
  currentMotorStep = targetStep;
}

void doHoming() {
  // Actualizar pantalla
  display.clearDisplay();
  display.setCursor(20, 20); display.setTextSize(2); display.print("HOMING...");
  display.display();

  Serial.println("🏠 Homing al Switch...");
  
  digitalWrite(PIN_DIR, HIGH); 
  while(digitalRead(PIN_SWITCH) == HIGH) { 
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(VEL_HOMING);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(VEL_HOMING);
    server.handleClient();
  }
  
  digitalWrite(PIN_DIR, LOW); 
  for(int i=0; i < (2 * STEPS_PER_MM); i++) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(VEL_HOMING);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(VEL_HOMING);
  }
  
  currentMotorStep = 0; 
  goToCell(0);
  updateScreen(); // Volver a info normal
}

// ==========================================
// 3. VISUALES TURING
// ==========================================
void updateStripVisuals(bool showHeadRed) {
  FastLED.clear();
  for(int i=0; i<CELLS; i++) {
    int ledIndexA = LOGICAL_START_LED - (i * 2);
    int ledIndexB = LOGICAL_START_LED - (i * 2) - 1;

    if(ledIndexB < 0) continue; 
    if(ledIndexA >= NUM_LEDS) continue;

    CRGB color;
    if(tape[i] == 1) color = cOne;
    else if(tape[i] == 2) { color = CRGB::DarkBlue; color.nscale8(50); } 
    else { color = cZero; color.nscale8(40); }

    if(i == head && showHeadRed) color = cHead;

    leds[ledIndexA] = color;
    leds[ledIndexB] = color;
  }
  FastLED.show();
}

// --- LECTURA MEJORADA ---
int readPhysicalColor() {
  if (tape[head] == 2) return 2;

  updateStripVisuals(false); 
  delay(250); 
  
  float rTotal=0, gTotal=0, bTotal=0;
  for(int i=0; i<3; i++) {
     float r, g, b;
     tcs.getRGB(&r, &g, &b);
     rTotal += r; gTotal += g; bTotal += b;
     delay(10);
  }
  float r = rTotal / 3.0; float g = gTotal / 3.0; float b = bTotal / 3.0;

  int detectedVal = -1;
  
  if(g > r + 8 && g > b + 8) detectedVal = 1;
  else if(b > r + 10 && b > g + 10) detectedVal = 0;
  else detectedVal = tape[head];

  if (detectedVal == 0 && tape[head] == 1) detectedVal = 1;
  
  updateStripVisuals(true);
  return detectedVal;
}

// ==========================================
// 4. LÓGICA TURING
// ==========================================

void runTuringCycle() {
  if (state == 99) { 
    running = false; 
    Serial.println("🏁 FIN");
    updateScreen(); // Actualizar pantalla final
    fill_solid(leds, NUM_LEDS, CRGB::Green); FastLED.show(); delay(300);
    fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); delay(300);
    updateStripVisuals(true);
    return; 
  }
  if (state == 0) { running = false; return; }

  goToCell(head);
  
  int val = readPhysicalColor();
  tape[head] = val; 

  int writeVal = val;
  int move = 0; 
  int nextState = state;

  // Lógica Suma
  if (state == 10) { if (val==1){move=1;nextState=10;} else if(val==2){writeVal=1;move=1;nextState=11;} else{move=1;} }
  else if (state == 11) { if(val==1){move=1;nextState=11;} else if(val==0){move=-1;nextState=12;} }
  else if (state == 12) { if(val==1){writeVal=0;move=0;nextState=99;} }
  
  // Lógica Resta
  else if (state == 20) { if(val==2){move=1;nextState=21;} else{move=1;} }
  else if (state == 21) { if(val==1){move=1;nextState=21;} else if(val==0){move=-1;nextState=22;} }
  else if (state == 22) { if(val==2){nextState=99;} else if(val==1){writeVal=0;move=-1;nextState=23;} else if(val==0){move=-1;} }
  else if (state == 23) { if(val==2){move=-1;nextState=24;} else{move=-1;} }
  else if (state == 24) { if(val==0){if(head==0) nextState=99; else {move=-1;nextState=24;}} else if(val==1){writeVal=0;move=1;nextState=20;} else if(val==2){move=-1;} }

  tape[head] = writeVal;
  state = nextState;
  
  updateStripVisuals(true);
  head += move;
  if(head < 0) head = 0; if(head >= CELLS) head = CELLS-1;

  // ACTUALIZAR PANTALLA AL FINAL DEL CICLO
  updateScreen();
}

// ==========================================
// 5. SETUP Y LOOP
// ==========================================

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STEP, OUTPUT); pinMode(PIN_DIR, OUTPUT); pinMode(PIN_ENABLE, OUTPUT); pinMode(PIN_SWITCH, INPUT_PULLUP);
  digitalWrite(PIN_ENABLE, LOW); 
  pinMode(PIN_SENSOR_LED, OUTPUT); digitalWrite(PIN_SENSOR_LED, LOW); 
  
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 800);
  if (!tcs.begin()) Serial.println("⚠️ Sensor OFF");

  // INICIAR PANTALLA
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Dir 0x3C
    Serial.println(F("SSD1306 fallo"));
  }
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 20); display.println("Iniciando...");
  display.display();

  WiFi.softAP(ssid, password);
  
  server.on("/", handleRoot); 
  server.on("/init", handleInit); 
  server.on("/status", handleStatus); 
  server.on("/control", handleControl);
  server.on("/mode", handleMode);
  
  server.begin();
  
  updateStripVisuals(true);
  doHoming(); // Esto ahora actualiza la pantalla también
}

void loop() {
  server.handleClient();
  
  if (currentEffect == FX_TURING) {
    if (running) {
      runTuringCycle();
      delay(100); 
    }
  } 
  else if (currentEffect == FX_RAINBOW) { runRainbow(); delay(10); }
  else if (currentEffect == FX_FADE) { runFade(); delay(10); }
  else if (currentEffect == FX_CONFETTI) { runConfetti(); delay(10); }
  else if (currentEffect == FX_OFF) {
    fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); delay(100);
  }
}

// ==========================================
// 6. MANEJADORES WEB
// ==========================================

void handleMode() {
  String m = server.arg("m");
  if (m == "rainbow") currentEffect = FX_RAINBOW;
  else if (m == "fade") currentEffect = FX_FADE;
  else if (m == "confetti") currentEffect = FX_CONFETTI;
  else if (m == "off") currentEffect = FX_OFF;
  else if (m == "turing") { currentEffect = FX_TURING; updateStripVisuals(true); }
  
  running = false; 
  
  // Actualizar pantalla indicando modo
  display.clearDisplay();
  display.setCursor(0,0); display.print("IP: "); display.println(WiFi.softAPIP());
  display.setCursor(0,25); display.setTextSize(2); 
  if(m=="off") display.print("APAGADO");
  else display.print("MODO LUZ");
  display.display();
  
  server.send(200, "text/plain", "OK");
}

void handleInit() {
  currentEffect = FX_TURING;
  
  if(!server.hasArg("a")) return;
  int numA = server.arg("a").toInt(); int numB = server.arg("b").toInt(); String op = server.arg("op");
  
  if(numA > 5) numA = 5; if(numB > 5) numB = 5;
  
  // Actualizar descripción para la pantalla
  currentOpDesc = (op == "add" ? "SUMA " : "RESTA ") + String(numA) + (op == "add" ? " + " : " - ") + String(numB);
  
  for(int i=0; i<CELLS; i++) tape[i] = 0;
  int startPos = 0; 
  for(int i=0; i<numA; i++) tape[startPos + i] = 1;
  tape[startPos + numA] = 2; 
  int startB = startPos + numA + 1;
  for(int i=0; i<numB; i++) tape[startB + i] = 1;
  head = startPos; 
  if(op == "add") state = 10; else state = 20;
  
  doHoming(); goToCell(head); updateStripVisuals(true); updateScreen();
  server.send(200, "text/plain", "OK");
}

void handleControl() {
  if(server.hasArg("run")) {
    running = (server.arg("run") == "1");
    if(running) {
      currentEffect = FX_TURING;
      updateScreen();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{"; 
  json += "\"head\":" + String(head) + ","; 
  json += "\"mode\":" + String(currentEffect) + ","; 
  json += "\"tape\":[";
  for(int i=0; i<CELLS; i++) { json += String(tape[i]); if(i < CELLS - 1) json += ","; }
  json += "]}"; 
  server.send(200, "application/json", json);
}

const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Nicowo</title>
  <style>
    body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;background:#121212;color:#eee;text-align:center;margin:0;padding:20px;}
    h2{color:#00e676; text-transform: uppercase; letter-spacing: 2px;}
    .panel{background:#1e1e1e;padding:20px;border-radius:15px;display:inline-block;max-width:400px;width:100%;box-shadow: 0 4px 20px rgba(0,0,0,0.5);}
    input, select { padding: 12px; font-size: 16px; margin: 5px; border-radius: 8px; border: 1px solid #333; background: #2c2c2c; color: white; width: 60px; text-align: center; }
    select { width: auto; }
    button { padding: 12px 20px; margin: 8px; cursor: pointer; border: none; border-radius: 8px; font-weight: bold; font-size: 14px; transition: transform 0.1s; width: 45%; }
    button:active { transform: scale(0.98); }
    .btn-load { background: #2979ff; color: white; width: 95%; }
    .btn-run { background: #00c853; color: white; }
    .btn-stop { background: #ff3d00; color: white; }
    .mode-section { margin-top: 20px; border-top: 1px solid #333; padding-top: 15px; }
    .mode-title { font-size: 14px; color: #aaa; margin-bottom: 10px; display: block; }
    .btn-mode { background: #424242; color: #ddd; width: 30%; padding: 10px; font-size: 12px; }
    .btn-mode:hover { background: #616161; }
    .btn-off { background: #212121; border: 1px solid #444; }
    .viz { display: flex; justify-content: center; gap: 3px; margin-top: 30px; }
    .bit { width: 18px; height: 30px; background: #222; border-radius: 3px; transition: background 0.2s; }
    .bit.one { background: #00e676; box-shadow: 0 0 8px #00e676; }
    .bit.sep { background: #2979ff; opacity: 0.6; }
    .bit.head { border: 2px solid #ff1744; transform: scale(1.3); z-index: 10; background: rgba(255, 23, 68, 0.2); }
  </style>
</head>
<body>
  <div class="panel">
    <h2>🖨️ Turing Nicowo</h2>
    <div>
      <input type="number" id="a" value="3" min="1" max="5">
      <select id="op"><option value="add">+</option><option value="sub">-</option></select>
      <input type="number" id="b" value="2" min="1" max="5">
      <br>
      <button class="btn-load" onclick="post('init')">CARGAR DATOS</button>
      <br>
      <button class="btn-run" onclick="post('control?run=1')">▶ INICIAR</button>
      <button class="btn-stop" onclick="post('control?run=0')">⏸ PAUSAR</button>
    </div>
    <div class="mode-section">
      <span class="mode-title">AMBIENTE LED</span>
      <button class="btn-mode" onclick="setMode('rainbow')">🌈 Arcoiris</button>
      <button class="btn-mode" onclick="setMode('fade')">🌊 Fade</button>
      <button class="btn-mode" onclick="setMode('confetti')">🎉 Fiesta</button>
      <button class="btn-mode btn-off" onclick="setMode('off')">⚫ Apagar</button>
    </div>
  </div>
  <div class="viz" id="v"></div>
  <script>
    const v = document.getElementById('v');
    for(let i=0; i<13; i++) { let d = document.createElement('div'); d.className = 'bit'; d.id = 'b'+i; v.appendChild(d); }
    function post(url) {
      if(url == 'init') {
        let a = document.getElementById('a').value, b = document.getElementById('b').value, op = document.getElementById('op').value;
        fetch(`/init?a=${a}&b=${b}&op=${op}`);
      } else fetch(url);
    }
    function setMode(mode) { fetch('/mode?m=' + mode); }
    setInterval(() => {
      fetch('/status').then(r => r.json()).then(d => {
        d.tape.forEach((x, i) => {
          let b = document.getElementById('b'+i); b.className = 'bit';
          if(x == 1) b.classList.add('one');
          if(x == 2) b.classList.add('sep');
          if(i == d.head && d.mode == 0) b.classList.add('head');
        });
      });
    }, 300);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", index_html); }