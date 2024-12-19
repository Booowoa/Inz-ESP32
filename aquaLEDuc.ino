#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h> 
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "FastLED.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

  /*
  Czesci projektowe takie jak:
  - laczenie sie z baza danych,
  - pobieranie czasu z serwera NTP,
  - sprawdzanie czy istnieje polaczenie Wi-Fi, jezeli nie to ESP32 ponownie sie laczy z Wi-Fi.
  Zostaly zrealizowane na podstawie materialow open source, ktore mozna znalezc w komentarzu ponizej.
  */
  
  /*
	Rui Santos
	Complete project details at https://RandomNerdTutorials.com/esp32-date-time-ntp-client-server-arduino/
	Complete project details at https://RandomNerdTutorials.com/esp32-firebase-realtime-database/
	Complete project details at https://RandomNerdTutorials.com/solved-reconnect-esp32-to-wifi/
	Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  Based in the RTDB Basic Example by Firebase-ESP-Client library by mobizt
  https://github.com/mobizt/Firebase-ESP-Client/blob/main/examples/RTDB/Basic/Basic.ino
  */	

// nazwa oraz haslo wykorzystanego wifi
#define WIFI_NAZWA "Nazwa Wi-Fi"
#define WIFI_HASLO "Haslo do Wi-Fi"

// Api bazy danych z firestore - indywidualne dla bazy danych
#define API_KEY "kod Api bazy danych"

// adres bazy danych
#define DATABASE_URL "adres bazy danych" 

// Konfiguracja LED
#define NUM_LEDS 48
#define DATA_PIN 32
const int ledPin = 25; 
CRGB leds[NUM_LEDS];

// Parametry PWM
const int freq = 50000;
const int resolution = 8;

// Obiekty Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

unsigned long poprzedni_czas = 0;
unsigned long czas = 30000;	// czas sprawdzania czy jest polaczenie z internetem

// Tablice na dane dla kazdej godziny
int R[24];
int G[24];
int B[24];
int Power[24];

// Funkcja interpolacji danych
void wypelnij_i_zalacz(int aktualna_godzina, int nastepna_godzina, int &r, int &g, int &b, int &pwm) {
  extern int R[24], G[24], B[24], Power[24];

  // Pobieranie aktualnej minuty
  time_t now = time(nullptr);
  struct tm* localTime = localtime(&now);
  int aktualna_min = localTime->tm_min;

  // zmiana przesuniecia interpolacji, aby dane zmienialy sie co minute
  float przesuniecie = aktualna_min / 60.0;

  // interpolacja wartosci i zaokraglenie do liczb calkowitych
  r = round(R[aktualna_godzina] + przesuniecie * (R[nastepna_godzina] - R[aktualna_godzina]));
  g = round(G[aktualna_godzina] + przesuniecie * (G[nastepna_godzina] - G[aktualna_godzina]));
  b = round(B[aktualna_godzina] + przesuniecie * (B[nastepna_godzina] - B[aktualna_godzina]));
  pwm = round(Power[aktualna_godzina] + przesuniecie * (Power[nastepna_godzina] - Power[aktualna_godzina]));

  // Ustaw wartosci LED
  ledcWrite(ledPin, pwm);
  fill_solid(leds, NUM_LEDS, CRGB(r, g, b));
  FastLED.show();
}

void setup() {

  delay(2000);
  ledcAttach(ledPin, freq, resolution);
  FastLED.addLeds<WS2813, DATA_PIN, GRB>(leds, NUM_LEDS);

  // Konfiguracja Wi-Fi
  WiFi.begin(WIFI_NAZWA, WIFI_HASLO);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  //        + 1 h 
  configTime(3600, 0, "pool.ntp.org");
  while (!time(nullptr)) {
    delay(1000);
  }

  // Konfiguracja Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  } 
  
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void pobierz_i_odczytaj_dane(){
if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    
    // Odczyt Stringa z Firebase na sciezce "Message"
    if (Firebase.RTDB.getString(&fbdo, "/Message")) {
      if (fbdo.dataType() == "string") {
        String ramka_danych = fbdo.stringData();

        // zamiana pierwszego znaku na "1"  - potwierdzenie odczytania danych z bazy
        ramka_danych.setCharAt(0, '1');
        Firebase.RTDB.setString(&fbdo, "/Message", ramka_danych); // wyslanie '1' do bazy danych

        // podzial ramki na segmenty
        int godzina = 0;     // indeks dla godzin
        int startIndex = 2;  // przesuniecie momentu od ktorego dekodowana jest ramka danych - pierwszy element to 0 lub 1
        while (godzina < 24 && startIndex < ramka_danych.length()) {
          // odczytaj RRR
          int endIndex = ramka_danych.indexOf('.', startIndex);
          R[godzina] = ramka_danych.substring(startIndex, endIndex).toInt();
          
          // odczytaj GGG
          startIndex = endIndex + 1;
          endIndex = ramka_danych.indexOf('.', startIndex);
          G[godzina] = ramka_danych.substring(startIndex, endIndex).toInt();
          
          // odczytaj BBB
          startIndex = endIndex + 1;
          endIndex = ramka_danych.indexOf('.', startIndex);
          B[godzina] = ramka_danych.substring(startIndex, endIndex).toInt();
          
          // odczytaj PPP
          startIndex = endIndex + 1;
          endIndex = ramka_danych.indexOf('.', startIndex);
          if (endIndex == -1) endIndex = ramka_danych.length();  
          Power[godzina] = ramka_danych.substring(startIndex, endIndex).toInt();
          
          // kolejna iteracja (nastepna godzina)
          startIndex = endIndex + 1;
          godzina++;
        }
      }
    } 
  }
}

void loop() {
  
  // co jakis czas sprawdza czy istnieje polaczenie wifi- jezeli nie to esp32 probuje znowu sie polaczyc z wifi
  unsigned long aktualny_czas = millis();
  if ((WiFi.status() != WL_CONNECTED) && (aktualny_czas - poprzedni_czas >= czas)) {
    WiFi.disconnect();
    WiFi.reconnect();
    poprzedni_czas = aktualny_czas;
  }

  pobierz_i_odczytaj_dane();    // funkcja pobierajaca oraz dekodujaca dane z bazy danych

  // pobieranie aktualnej godziny
  time_t now = time(nullptr);
  struct tm* localTime = localtime(&now);
  int aktualna_godzina = localTime->tm_hour;
  
  int nastepna_godzina = (aktualna_godzina + 1) % 24;	

  //Liniowe uzupelnienie wartosci pomiedzy pelnymi godzinami
  int r, g, b, pwm;
  wypelnij_i_zalacz(aktualna_godzina, nastepna_godzina, r, g, b, pwm);

  delay(30000);   // petla zalacza sie co 30 sekund
}