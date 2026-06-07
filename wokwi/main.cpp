#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>


//DEFINICIJE

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET -1
#define POT_PIN 15

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


//VARIJABLE

Preferences prefs;

float alpha = 0.15;
float filteredADC = 2048;

float flow = 0;
float previousFlow = 0;
float flowDelta = 0;
bool inhale = true;

float volume = 0;
float bestVolume = 0;

float coachPos = 30;

unsigned long lastSample = 0;
unsigned long exerciseStart = 0;

bool timerRunning = false;
float failedAt = 0;
bool start = false;

enum State
{
  IDLE,
  RUNNING,
  SUCCESS,
  FAILED
};

State state = IDLE;

float bikeX = 10;
float bikeAngle = 0;
float bikeVelocity = 0;

/**
 * @brief Struktura koja predstavlja 2D točku.
 */
struct Point
{
  float x;
  float y;
};

//FUNKCIJE

/**
 * @brief Očitava potenciometar i pretvara ga u protok zraka.
 *
 * Funkcija:
 * - očitava ADC vrijednost
 * - filtrira signal EMA filtrom
 * - određuje smjer disanj
 * - mapira odstupanje od sredine ADC-a u protok [ml/s]
 */
void readFlow()
{
  int raw = analogRead(POT_PIN);

  filteredADC =
      alpha * raw +
      (1.0 - alpha) * filteredADC;

  inhale = filteredADC > 2048;

  float distanceFromCenter =
      abs(filteredADC - 2048.0);

  flow =
      (distanceFromCenter / 2047.0)
      * 1500.0;
}

/**
 * @brief Provjerava stabilnost protoka.
 *
 * Stabilnost se određuje na temelju promjene
 * protoka između dva uzastopna uzorka.
 *
 * @param dt Proteklo vrijeme od zadnjeg uzorka [s]
 * @return true - ako je protok stabilan
 * @return false - ako je protok nestabilan
 */
bool flowStable(float dt)
{
  flowDelta = flow - previousFlow;

  previousFlow = flow;

  return abs(flowDelta) * dt < 1;
}

/**
 * @brief Numerička integracija volumena.
 *
 * @param dt Proteklo vrijeme od zadnjeg uzorka [s]
 */
void integrateVolume(float dt)
{
  volume += flow * dt;
}

/**
 * @brief Ažurira fizikalni model motocikla.
 *
 * Brzina motocikla raste kada protok prijeđe
 * 600 ml/s. Između 900 i 1200 ml/s motor
 * se podiže na zadnji kotač pod kutem proporcionalan protoku.
 */
void updateBike()
{
  if(flow < 600)
  {
    bikeVelocity *= 0.9;
  }
  else
  {
    bikeVelocity += 0.15;
  }

  bikeVelocity = constrain(
      bikeVelocity,
      0,
      3);

  bikeX += bikeVelocity;

  if(bikeX > 128)
      bikeX = -20;

  if(flow < 900)
  {
    bikeAngle +=
      (0 - bikeAngle) * 0.1;
  }
  else if(flow <= 1200)
  {
    float targetAngle =
      map(
          (int)flow,
          900,
          1200,
          0,
          80);

    bikeAngle +=
      (targetAngle - bikeAngle) * 0.1;
  }
}

/**
 * @brief Sprema najbolji rezultat u NVS memoriju.
 *
 * Ako je trenutni volumen veći od prethodno
 * spremljenog najboljeg rezultata, vrijednost
 * se trajno zapisuje u ESP32 Preferences memoriju.
 */
void saveBest()
{
  if (volume > bestVolume)
  {
    bestVolume = volume;

    prefs.begin("spirometer", false);
    prefs.putFloat("bestVol", bestVolume);
    prefs.end();
  }
}

/**
 * @brief Resetira stanje igre.
 *
 * Briše trenutni rezultat, vraća motocikl
 * u početni položaj i postavlja stanje na IDLE.
 */
void resetExercise()
{
  timerRunning = false;
  exerciseStart = 0;
  volume = 0;
  bikeX = 10;
  bikeAngle = 0;
  bikeVelocity = 0;
  state = IDLE;
  updateBike();
}

/**
 * @brief Provjerava uvjete vježbe.
 *
 * Vježba je uspješna ako je:
 * - protok >= 900 ml/s
 * - protok < 1200 ml/s
 * - stabilan protok
 * - trajanje od najmanje 5 sekundi
 *
 * Prekoračenje 1200 ml/s ili gubitak stabilnosti
 * uzrokuje neuspjeh vježbe.
 *
 * @param dt Vrijeme između uzoraka [s]
 */
void checkExercise(float dt)
{
  bool stable = flowStable(dt);

  if (flow > 1200 || !stable)
  {
    volume = 0;
    state = FAILED;
    timerRunning = false;
    failedAt = millis();
    return;
  }

  bool valid =
      flow >= 900 &&
      flow < 1200 &&
      stable;

  if (valid)
   {
    if (!timerRunning)
    {
      exerciseStart = millis();
      timerRunning = true;
      state = RUNNING;
    }

    integrateVolume(dt);

    if (millis() - exerciseStart >= 5000)
    {
      state = SUCCESS;
      timerRunning = false;

      saveBest();
    }
  }
  else
  {
    volume = 0;
    timerRunning = false;

    if (state == RUNNING)
      state = IDLE;
  }
}


/**
 * @brief Rotira točku oko ishodišta.
 *
 * Primjenjuje standardnu 2D rotacijsku matricu.
 *
 * @param x Početna x koordinata
 * @param y Početna y koordinata
 * @param angle Kut rotacije [rad]
 * @return Point - rotirana točka
 */
Point rotatePoint(float x, float y, float angle)
{
  Point p;

  float c = cos(angle);
  float s = sin(angle);

  p.x = x * c - y * s;
  p.y = x * s + y * c;

  return p;
}

/**
 * @brief Crta rotiranu liniju.
 *
 * Sve koordinate definirane su relativno
 * na pivot točku motocikla.
 *
 * @param pivotX X koordinata pivota
 * @param pivotY Y koordinata pivota
 * @param angle Kut rotacije [rad]
 * @param x1 Početni relativni X
 * @param y1 Početni relativni Y
 * @param x2 Završni relativni X
 * @param y2 Završni relativni Y
 */
void drawRotLine(
    int pivotX,
    int pivotY,
    float angle,
    float x1,
    float y1,
    float x2,
    float y2)
{
  Point p1 = rotatePoint(x1, y1, angle);
  Point p2 = rotatePoint(x2, y2, angle);

  display.drawLine(
      pivotX + p1.x,
      pivotY + p1.y,
      pivotX + p2.x,
      pivotY + p2.y,
      WHITE);
}

/**
 * @brief Crta rotirani krug.
 *
 * Koristi se za crtanje kotača i glave vozača.
 *
 * @param pivotX X koordinata pivota
 * @param pivotY Y koordinata pivota
 * @param angle Kut rotacije [rad]
 * @param x Relativni X položaj
 * @param y Relativni Y položaj
 * @param r Radijus kruga
 */
void drawRotCircle(
    int pivotX,
    int pivotY,
    float angle,
    float x,
    float y,
    int r)
{
  Point p = rotatePoint(x, y, angle);

  display.drawCircle(
      pivotX + p.x,
      pivotY + p.y,
      r,
      WHITE);
}

/**
 * @brief Crta motocikl.
 *
 * Motocikl je sastavljen od više geometrijskih
 * oblika (linije i krugovi) koji se rotiraju
 * oko stražnjeg kotača radi simulacije podizanja
 * na zadnji kotač.
 * 
 * @param angle Kut motocikla [°]
 */
void drawBike(int angle)
{
  int px = (int)bikeX;
  int py = 42;

  float a = -angle * PI / 180.0;

  //KOTAČI
  drawRotCircle(px, py, a, 0, 0, 6);
  drawRotCircle(px, py, a, 0, 0, 5);

  drawRotCircle(px, py, a, 24, 0, 6);
  drawRotCircle(px, py, a, 24, 0, 5);

  //SIC
  drawRotLine(px,py,a,-2,-11,4,-11);
  drawRotLine(px,py,a,-2,-10,8,-10);
  drawRotLine(px,py,a,2,-9,8,-9);

  //REZERVAR
  drawRotLine(px,py,a,12,-11,16,-11);
  drawRotLine(px,py,a,10,-10,17,-10);
  drawRotLine(px,py,a,10,-9,17,-9);
  drawRotLine(px,py,a,11,-8,16,-8);

  //VILICA
  drawRotLine(px,py,a,18,-11,24,0);

  //OKVIR
  drawRotLine(px,py,a,6,-5,16,-5);
  drawRotLine(px,py,a,5,-6,16,-6);
  drawRotLine(px,py,a,8,-1,14,-1);
  drawRotLine(px,py,a,9,0,14,0);
  drawRotLine(px,py,a,16,-5,14,-1);
  drawRotLine(px,py,a,15,-5,13,-1);
  drawRotLine(px,py,a,14,-5,12,-1);

  //VOLAN
  drawRotLine(px,py,a,18,-11,16,-14);
  drawRotLine(px,py,a,16,-14,13,-14);

  //LAMPA
  drawRotLine(px,py,a,20,-11,20,-12);
  drawRotLine(px,py,a,21,-10,21,-13);

  //ČOEK
  drawRotLine(px,py,a,4,-11,8,-18);
  drawRotCircle(px, py, a, 10, -20, 2);
  drawRotLine(px,py,a,7,-17,13,-14);
  drawRotLine(px,py,a,4,-11,13,-9);
  drawRotLine(px,py,a,13,-9, 12,-1);
}

/**
 * @brief Crta indikator stabilnosti protoka.
 *
 * Marker prikazuje promjenu protoka između
 * uzastopnih uzoraka. Cilj je održavati marker
 * unutar označene ciljane zone.
 */
void drawCoach()
{
  int x = 112;

  display.drawRect(x, 5, 10, 54, WHITE);

  display.drawRect(x, 26, 10, 15, WHITE);

  float delta =
      constrain(flowDelta, -100, 100);

  int markerY =
      map(
          (int)delta,
          -100,
          100,
          58,
          6);

  display.fillRect(
      x + 2,
      markerY,
      6,
      3,
      WHITE);
}

/**
 * @brief Iscrtava cijelo korisničko sučelje.
 *
 * Funkcija prikazuje:
 * - motocikl
 * - coach indikator
 * - trenutni protok
 * - volumen
 * - najbolji rezultat
 * - stanje igre
 */
void drawScreen()
{
  display.clearDisplay();

  display.drawLine(
    0,
    48,
    127,
    48,
    WHITE);

  if(state == FAILED)
  {
    if (bikeAngle >= 180)
      drawBike(180);
    else {
      bikeX+=5;
      drawBike(bikeAngle+=10);
    }
  }
  else
  {
      drawBike(bikeAngle);
  }

  drawCoach();

  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  display.setCursor(0,0);
  display.print((int)flow);
  display.print(" ml/s");

  display.setCursor(0, 56);
  display.print("V:");
  display.print((int)volume);

  display.setCursor(60, 56);
  display.print("B:");
  display.print((int)bestVolume);

  switch (state)
  {
    case SUCCESS:
      if (millis() - exerciseStart <= 7000)
      {
        display.setCursor(60, 0);
        display.print("SUCCESS");
      }
      else
      {
        start = false;
        display.clearDisplay();
        display.setCursor(40, 3);
        display.print("SUCCESS");
        display.setCursor(3, 15);
        display.print("Score: ");
        display.print(volume);
        display.setCursor(3, 25);
        display.print("Top score: ");
        display.print(bestVolume);
        display.setCursor(12, 40);
        display.print("Stop breathing to");
        display.setCursor(15, 47);
        display.print("restart the game");
      }
      break;

    case FAILED:
      if (millis() - failedAt <= 2000)
      {
        display.setCursor(60, 0);
        display.print("TOO FAST");
      }
      else
      {
        start = false;
        display.clearDisplay();
        display.setCursor(17, 20);
        display.print("EXERCISE FAILED");
        display.setCursor(12, 35);
        display.print("Stop breathing to");
        display.setCursor(15, 42);
        display.print("restart the game");
      }
      break;

    case RUNNING:
      display.setCursor(60, 0);
      display.print("HOLD...");
      break;

    default:
      break;
  }

  display.display();
}

/**
 * @brief Inicijalizacija sustava.
 *
 * Pokreće:
 * - serijsku komunikaciju
 * - ADC
 * - OLED zaslon
 * - NVS memoriju
 */
void setup()
{
  Serial.begin(115200);

  analogReadResolution(12);

  display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C);

  prefs.begin("spirometer", true);
  bestVolume =
      prefs.getFloat("bestVol", 0);
  prefs.end();

  display.clearDisplay();
  display.display();
}

/**
 * @brief Glavna petlja programa.
 *
 * Periodički:
 * - očitava protok
 * - ažurira model motocikla
 * - provjerava uvjete vježbe
 * - osvježava OLED prikaz
 */
void loop()
{
  unsigned long now = millis();

  if (now - lastSample >= 20)
  {
    float dt =
        (now - lastSample) / 1000.0;

    lastSample = now;

    readFlow();

    if (!start && flow < 30) {
      start = true;
    }

    if (state != FAILED && state != SUCCESS && start)
    {
      updateBike();

      checkExercise(dt);

      drawScreen();
    }
  }

  if (state == FAILED)
  {
    drawScreen();
    if (flow < 30)
    {
      resetExercise();
    }
  }

  if (state == SUCCESS)
  {
    drawScreen();
    if (flow < 30)
    {
      resetExercise();
    }
  }
}