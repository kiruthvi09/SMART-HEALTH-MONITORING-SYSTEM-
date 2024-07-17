#include <Wire.h>
#include <Adafruit_GFX.h> //OLED libraries
#include <Adafruit_SSD1306.h>
#include "MAX30105.h" //sparkfun MAX3010X library
#include "heartRate.h"
#include <ESP8266WiFi.h>
#include <Adafruit_MLX90614.h>
MAX30105 particleSensor;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

const char* ssid = "your SSID";
const char* password = "your password";
const char* server = "api.thingspeak.com";
const String apiKey = "Your channels API key";
const String field1Name = "HeartRate";
const String field2Name = "SpO2";
const String field3Name = "TemperatureF";

#define INTERVAL_MESSAGE2 10000
unsigned long time_2 = 0;
int period = 2000;
unsigned long time_now = 0;
double avered = 0;
double aveir = 0;
double sumirrms = 0;
double sumredrms = 0;
int i = 0;
int Num = 100; //calculate SpO2 by this sampling interval
int TemperatureF = 0;
int SPo2;
double ESpO2 = 95.0;    //initial value of estimated SpO2
double FSpO2 = 0.7;     //filter factor for estimated SpO2
double frate = 0.95;    //low pass filter for IR/red LED value to eliminate AC component
#define TIMETOBOOT 1// wait for this time(msec) to output SpO2
#define SCALE 88.0      //adjust to display heart beat and SpO2 in the same scale
#define SAMPLING 1    //if you want to see heart beat more precisely , set SAMPLING to 1
#define FINGER_ON 3000  // if red signal is lower than this , it indicates your finger is not on the sensor
#define MINIMUM_SPO2 0.0
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];    //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int heartRate;



#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); //Declaring the display name (display)


#define USEFIFO

void setup()
{
  Serial.begin(115200);
  Serial.println("Initializing...");
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //Start the OLED display
  display.display();
  display.clearDisplay();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi!");

  // Initialize sensor
  while (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println("MAX30102 was not found. Please check wiring/power/solder jumper at MH-ET LIVE MAX30102 board. ");
    //while (1);
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  //Setup to sense a nice looking saw tooth on the plotter
  byte ledBrightness = 255; // 0x7F Options: 0=Off to 255=50mA
  byte sampleAverage = 4;   //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2;         //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 400;     //1000 is best but needs processing power//Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;     //Options: 69, 118, 215, 411
  int adcRange = 16384;     //Options: 2048, 4096, 8192, 16384
  // Set up the wanted parameters
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  particleSensor.enableDIETEMPRDY();
  mlx.begin();
}


void calculateSpo2() {
  uint32_t ir, red, green;
  double fred, fir;
  double SpO2 = 0; //raw SpO2 before low pass filtered
  int SPo2;
#ifdef USEFIFO
  particleSensor.check(); //Check the sensor, read up to 3 samples

  while (particleSensor.available())

  {
#ifdef MAX30105
    red = particleSensor.getFIFORed(); //Sparkfun's MAX30105
    ir = particleSensor.getFIFOIR();
    //Sparkfun's MAX30105
#else
    red = particleSensor.getFIFOIR(); //why getFOFOIR output Red data by MAX30102 on MH-ET LIVE breakout board
    ir = particleSensor.getFIFORed();
    //why getFIFORed output IR data by MAX30102 on MH-ET LIVE breakout board
#endif

    i++;
    fred = (double)red;
    fir = (double)ir;
    avered = avered * frate + (double)red * (1.0 - frate); //average red level by low pass filter
    aveir = aveir * frate + (double)ir * (1.0 - frate);    //average IR level by low pass filter
    sumredrms += (fred - avered) * (fred - avered);        //square sum of alternate component of red level
    sumirrms += (fir - aveir) * (fir - aveir);             //square sum of alternate component of IR level
    if ((i % SAMPLING) == 0)
    { //slow down graph plotting speed for arduino Serial plotter by thin out
      if (millis() > TIMETOBOOT)
      {
        if (ir < FINGER_ON)
          ESpO2 = MINIMUM_SPO2; //indicator for finger detached
        //float temperature = particleSensor.readTemperatureF();
        if (ESpO2 <= -1)
        {
          ESpO2 = 0;
        }

        if (ESpO2 > 100)
        {
          ESpO2 = 100;
        }

        SPo2 = ESpO2;


      }
    }
    if ((i % Num) == 0)
    {
      double R = (sqrt(sumredrms) / avered) / (sqrt(sumirrms) / aveir);
      // Serial.println(R);
      SpO2 = -23.3 * (R - 0.4) + 100;               //http://ww1.microchip.com/downloads/jp/AppNotes/00001525B_JP.pdf
      ESpO2 = FSpO2 * ESpO2 + (1.0 - FSpO2) * SpO2; //low pass filter
      //Serial.print(SpO2); Serial.print(","); Serial.println(ESpO2);
      sumredrms = 0.0;
      sumirrms = 0.0;
      i = 0;
      break;


    }

    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    //Serial.println(SpO2)
  }
}
void calculateHeartRate()
{
  long irValue = particleSensor.getIR();

  if (checkForBeat(irValue) == true)
  {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20)
    {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      heartRate = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        heartRate += rates[x];
      heartRate /= RATE_SIZE;
    }

  }
}

void loop()
{
  calculateSpo2();

  calculateHeartRate();
  int TemperatureF = mlx.readObjectTempF();

  int irValue = particleSensor.getIR();



  if (irValue > 7000) {
    // Finger detected, display data
  /*  display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println("SpO2");
    display.print(SPo2);
    display.println("%");
    display.setCursor(0, 20);
    display.println("Temperature");
    display.print(TemperatureF);
    display.setCursor(0, 40);
    display.println("Heart Rate::");
    display.print(heartRate);
    display.println(":BPM");
    display.display();*/
    sendToThingspeak(heartRate, SPo2, TemperatureF);

  } else {
    // No finger detected, display a message
   /* display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(30, 10);
    display.println("WiFi Connected");
    display.setCursor(30, 25);
    display.println("Please Place");
    display.setCursor(30, 40);
    display.println("Your Finger");
    display.display();*/
    Serial.println("finger not detected");
  }
}
#endif

void sendToThingspeak(int heartRate, int SPo2, int TemperatureF) {
  WiFiClient client;
  if (client.connect(server, 80)) {
    String postStr = apiKey;
    postStr += "&field1=";
    postStr += String(heartRate, 1);
    postStr += "&field2=";
    postStr += String(SPo2, 1);
    postStr += "&field3=";
    postStr += String(TemperatureF,1);
    
    postStr += "\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);

    Serial.println("Data sent to Thingspeak!");
  }
  else {
    Serial.println("Failed to connect to Thingspeak!");
  }
  client.stop();
}
