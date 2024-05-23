#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>

#include <DHT.h>
#include <Adafruit_NeoPixel.h>

#include "SimplexNoise.h"

const char* ssid = "iPhone de Fernando JR. 2do";
const char* password = "12345678";

// const char* mqtt_server = "192.168.100.27";
const char* mqtt_server = "172.20.10.15";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];

long lastUpdate = 0;
float cooldown_factor = 0.0;

long min_cooldown = 25;
long max_cooldown = 100;

float color_factor = 0.0;
int max_color_brightness = 6;

#define DHTPIN 21
#define DHTTYPE DHT11

#define LED_MAT_PIN 23
#define NUMPIXELS 256

DHT dht(DHTPIN, DHTTYPE);

Adafruit_NeoPixel pixel_mat = Adafruit_NeoPixel(NUMPIXELS, LED_MAT_PIN, NEO_GRB + NEO_KHZ800);

#define SIZE 16
uint32_t pixels[SIZE * SIZE];

inline uint32_t rgb_to_grb(uint16_t rgb) {
    return (((rgb & 0x0f0) << 12) | (rgb & 0xf0f)) << 8;
}

#define BALL_COUNT 3
float positions[6];

SimplexNoise sn;
float noiseIncrese[] = {
    0.01, 0.015,
    0.013, 0.011,
    0.02, 0.017
};

float noiseX[] = {
    0.0, 100.0, 10000.0,
    3414.0, 1000.0, 100000.0
};
float noiseY[] = {
    0.0, 10.0, 100.0,
    1000.0, 10000.0, 100000.0
};

void setup_wifi();
void callback(char* topic, byte* message, unsigned int length);
void reconnect();

void setup()
{
    Serial.begin(115200);

    dht.begin();

    pixel_mat.begin();

    for (int i = 0; i < SIZE * SIZE; i++)
    {
        pixels[i] = 0;
    }
    
    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop()
{
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();
    long now = millis();
    if (now - lastMsg > 1000)
    {
        lastMsg = now;

        float humedad = dht.readHumidity();
        float temperatura = dht.readTemperature();

        if (isnan(humedad) || isnan(temperatura))
        {
            Serial.printf("Error obteniendo los datos del sensor DHT11: %d, %d\n", humedad, temperatura);
            return;
        }

        char tempString[8];
        dtostrf(temperatura, 1, 2, tempString);
        Serial.print("Temperatura: ");
        Serial.println(tempString);
        client.publish("esp32/temperature", tempString);

        char humString[8];
        dtostrf(humedad, 1, 2, humString);
        Serial.print("Humedad: ");
        Serial.println(humString);
        client.publish("esp32/humidity", humString);
        
    }
    if (now - lastUpdate > ((max_cooldown - min_cooldown) * (1 - cooldown_factor) + min_cooldown))
    {
        lastUpdate = now;

        for (int i = 0; i < SIZE * SIZE; i++)
        {
            pixels[i] = 0;
        }

        for (int i = 0; i < BALL_COUNT; i++)
        {
            positions[i * 2] = sn.noise(noiseX[i], noiseY[i]) * SIZE / 2 + SIZE / 2;
            positions[i * 2 + 1] = sn.noise(noiseX[i + BALL_COUNT], noiseY[i + BALL_COUNT]) * SIZE / 2 + SIZE / 2;
            noiseX[i] += noiseIncrese[i];
            noiseX[i + BALL_COUNT] += noiseIncrese[i + BALL_COUNT];
        }

        for (int row = 0; row < SIZE; row++)
        {
            for (int col = 0; col < SIZE; col++)
            {
                int d = 1000;
                int blob = 0;
                for (int i = 0; i < BALL_COUNT; i++)
                {
                    int newD = abs(positions[i * 2] - row) + abs(positions[i * 2 + 1] - col);
                    if (newD < d)
                    {
                        d = newD;
                        blob = i;
                    }
                }
                if (d < 8)
                {
                    int r = 0, g = 0, b = 0;
                    g = blob * 2;
                    b = color_factor * max_color_brightness;
                    r = (1 - color_factor) * max_color_brightness;
                    pixels[row + col * SIZE] = r << 16 | g << 8 | b;

                    // pixels[row + col * SIZE] = /* 0x000005 | */ (b * 2) << 16 | (3 - b) * 2;
                }
            }
        }

        for (int i = 0; i < SIZE * SIZE; i++)
        {
            int col = i / SIZE;
            int row = i % SIZE;
            int pos = SIZE * col + ((col % 2 == 0) ? row : (SIZE - 1 - row));
            pixel_mat.setPixelColor(pos, pixels[i]);
        }

        pixel_mat.show();
    }
}

void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *message, unsigned int length)
{
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String messageTemp;

    for (int i = 0; i < length; i++)
    {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
    }
    Serial.println();

    // If a message is received on the topic esp32/output, you check if the message is either "on" or "off".
    // Changes the output state according to the message
    if (String(topic) == "server/cooldown")
    {
        cooldown_factor = atof(messageTemp.c_str());
    }
    else if (String(topic) == "server/color")
    {
        color_factor = atof(messageTemp.c_str());
    }
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect("ESP8266Client"))
        {
            Serial.println("connected");
            // Subscribe
            client.subscribe("server/cooldown");
            client.subscribe("server/color");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}