


/*************************************
 * https://everythingsmarthome.co.uk
 * 
 * This is an MQTT connected fingerprint sensor which can 
 * used to connect to your home automation software of choice.
 * 
 * You can add and remove fingerprints using MQTT topics by
 * sending the ID through the topic.
 * 
 * Simply configure the Wifi and MQTT parameters below to get
 * started!
 *
 */
 
#include <ArduinoJson.h>
//#include <ESP8266WiFi.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// Wifi Settings
#define SSID                          "NoT"
#define PASSWORD                      "ald938gGaf"
// #define STATIC_IP                     192, 168, 70, 70
#define STATIC_IP                     192, 168, 70, 159
#define GATEWAY                       192, 168, 70, 1
#define SUBNET_MASK                   255, 255, 255, 0

// GPIOs definitions
#define D3 10


// MQTT Settings
#define HOSTNAME                      "fingerprintESP32"
#define MQTT_SERVER                   "192.168.70.100"
#define SENSOR_ENABLED_TOPIC          "/fingerprint/enabled"
#define STATE_TOPIC                   "/fingerprint/mode/status"
#define MODE_LEARNING                 "/fingerprint/mode/learning"
#define MODE_READING                  "/fingerprint/mode/reading"
#define MODE_DELETE                   "/fingerprint/mode/delete"
#define MODE_REBOOT                   "/fingerprint/mode/reboot"
#define MODE_FLUSH_MEMORY             "/fingerprint/mode/flush-memory"
#define AVAILABILITY_TOPIC            "/fingerprint/available"
#define DEBUG_TOPIC                   "/fingerprint/debug"
#define mqtt_username                 "mqtt_user"
#define mqtt_password                 "mqtt_user"

#define SENSOR_TX 20                  //GPIO Pin for RX
#define SENSOR_RX 21                  //GPIO Pin for TX

SoftwareSerial mySerial(SENSOR_TX, SENSOR_RX);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

WiFiClient wifiClient;                // Initiate WiFi library
PubSubClient client(wifiClient);      // Initiate PubSubClient library

uint8_t id = 0;                       //Stores the current fingerprint ID
uint8_t lastID = 0;                   //Stores the last matched ID
uint8_t lastConfidenceScore = 0;      //Stores the last matched confidence score
boolean modeLearning = false;
boolean modeReading = true;
boolean modeDelete = false;
boolean sensorOn = true;


IPAddress staticIP(STATIC_IP);     // Desired static IP address
IPAddress gateway(GATEWAY);        // Your router's IP address
IPAddress subnet(SUBNET_MASK);       // Subnet mask  WiFi.mode(WIFI_STA);


//Declare JSON variables
DynamicJsonDocument mqttMessage(512);
size_t mqttMessageSize;
char mqttBuffer[512];

// For debugging serial output relayed over MQTT
void send_over_mqtt(const char string[])
{
    mqttMessage["debugString"] = string;
    mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
    client.publish(DEBUG_TOPIC, mqttBuffer, mqttMessageSize);
}

void print_to_output(const char string[])
{
  Serial.println(string);
  send_over_mqtt(string);
}

void handle_error(int line)
{
  String stringToSend = "Restarting! Error in line " + line;
  send_over_mqtt(stringToSend.c_str());
  //ESP.restart();
}
void fingerprint_sensor_fpm10a_init()
{
    // set the data rate for the sensor serial port
  finger.begin(57600);
  delay(5);
  if (finger.verifyPassword()) {
    print_to_output("Found fingerprint sensor!");
    print_to_output("This is new!");
  } else {
    print_to_output("Did not find fingerprint sensor :(");
  }

}
void setup()
{
  pinMode(D3, INPUT); // Connect D3 to T-Out (pin 5 on reader), T-3v to 3v

  Serial.begin(57600);
  while (!Serial);
  delay(100);
  print_to_output("\n\nWelcome to the MQTT Fingerprint Sensor program!");

  fingerprint_sensor_fpm10a_init();

  WiFi.config(staticIP, gateway, subnet);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting...");

  while (WiFi.status() != WL_CONNECTED) {       // Wait till Wifi connected
    delay(500);
    Serial.print(".");
  }

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());                     // Print IP address

  client.setServer(MQTT_SERVER, 1883);                // Set MQTT server and port number
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();                //Just incase we get disconnected from MQTT server
  }
  
  int fingerState = digitalRead(D3); // Read T-Out, normally HIGH (when no finger)

  if (fingerState == HIGH) { // No finger is placed on the sensor

    finger.LEDcontrol(false);
    print_to_output("No Finger Detected");
    mqttMessage["mode"] = "reading";
    mqttMessage["id"] = id;
    mqttMessage["state"] = "Waiting";
    mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
    client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);

    while (fingerState == HIGH) {         // Disable sensor while no finger
      fingerState = digitalRead(D3);
      client.loop();                      // Keep checking for incoming MQTT messages 
      delay(100);
    }
  } else if (sensorOn == false) {        // Disable sensor regardless of finger presence
    finger.LEDcontrol(false);
  } else {
    if (modeReading == true && modeLearning == false) {
      uint8_t result = getFingerprintID(); // Read fingerprint
      if (result == FINGERPRINT_OK) {
        mqttMessage["mode"] = "reading";
        mqttMessage["id"] = lastID;
        mqttMessage["state"] = "Matched";
        mqttMessage["confidence"] = lastConfidenceScore;
        mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
        client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
        finger.LEDcontrol(false);
        delay(3000);
      } else if (result == FINGERPRINT_NOTFOUND) {
        mqttMessage["mode"] = "reading";
        mqttMessage["match"] = false;
        mqttMessage["id"] = id;
        mqttMessage["state"] = "Not matched";
        mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
        client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
        delay(500);
      } else if (result == FINGERPRINT_NOFINGER) {
        mqttMessage["mode"] = "reading";
        mqttMessage["id"] = id;
        mqttMessage["state"] = "Waiting";
        mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
        client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
      } else {

      }
    }
  }

  client.loop();         // Keep checking for incoming MQTT messages 
  delay(100);            //don't need to run this at full speed.
}

uint8_t getFingerprintID() {
  print_to_output("entering finger.getImage");
  uint8_t p = finger.getImage();
  print_to_output("exited finger.getImage");
  switch (p) {
    case FINGERPRINT_OK:
      print_to_output("Image taken");
      break;
    case FINGERPRINT_NOFINGER:
      //print_to_output("No finger detected");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      print_to_output("Communication error1");
      handle_error(__LINE__);
      return p;
    case FINGERPRINT_IMAGEFAIL:
      print_to_output("Imaging error");
      return p;
    default:
      print_to_output("Unknown error");
      return p;
  }

  // OK success!
  print_to_output("entering finger.image2T");
  p = finger.image2Tz();
  print_to_output("exited finger.image2T");

  switch (p) {
    case FINGERPRINT_OK:
      print_to_output("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      print_to_output("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      print_to_output("Communication error2");
      handle_error(__LINE__);
      return p;
    case FINGERPRINT_FEATUREFAIL:
      print_to_output("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      print_to_output("Could not find fingerprint features");
      return p;
    default:
      print_to_output("Unknown error");
      return p;
  }

  // OK converted!
  print_to_output("entering finger.fingerFastSearc");
  p = finger.fingerFastSearch();
  print_to_output("exited finger.fingerFastSearc");

  if (p == FINGERPRINT_OK) { // found a match
    print_to_output("Found a print match!");
    lastID = finger.fingerID;
    lastConfidenceScore = finger.confidence;
    Serial.print("Found ID #"); Serial.print(finger.fingerID);
    Serial.print(" with confidence of "); Serial.println(finger.confidence);
    return p;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    print_to_output("Communication error3");
    handle_error(__LINE__);
    return p;
  } else if (p == FINGERPRINT_NOTFOUND) {
    print_to_output("Did not find a match");
    return p;
  } else {
    print_to_output("Unknown error");
    return p;
  }

  return finger.fingerID;
}

uint8_t getFingerprintEnroll() {
  int p = -1;
  mqttMessage["mode"] = "learning";
  mqttMessage["id"] = id;
  mqttMessage["state"] = "Place finger..";
  mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
  Serial.print("Waiting for valid finger to enroll as #"); Serial.println(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        print_to_output("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        print_to_output("Communication error4");
        handle_error(__LINE__);
        break;
      case FINGERPRINT_IMAGEFAIL:
        print_to_output("Imaging error");
        break;
      default:
        print_to_output("Unknown error");
        break;
    }
  }

  // OK success!

  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      print_to_output("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      print_to_output("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      print_to_output("Communication error5");
      handle_error(__LINE__);
      return p;
    case FINGERPRINT_FEATUREFAIL:
      print_to_output("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      print_to_output("Could not find fingerprint features");
      return p;
    default:
      print_to_output("Unknown error");
      return p;
  }

  mqttMessage["mode"] = "learning";
  mqttMessage["id"] = id;
  mqttMessage["state"] = "Remove finger..";
  mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
  print_to_output("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }
  Serial.print("ID "); Serial.print(id);
  p = -1;
  mqttMessage["mode"] = "learning";
  mqttMessage["id"] = id;
  mqttMessage["state"] = "Place same finger..";
  mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
  client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
  print_to_output("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        print_to_output("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        print_to_output("Communication error6");
        handle_error(__LINE__);
        break;
      case FINGERPRINT_IMAGEFAIL:
        print_to_output("Imaging error");
        break;
      default:
        print_to_output("Unknown error");
        break;
    }
  }

  // OK success!

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      print_to_output("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      print_to_output("Image too messy");
      return p;
    case FINGERPRINT_PACKETRECIEVEERR:
      print_to_output("Communication error7");
      handle_error(__LINE__);
      return p;
    case FINGERPRINT_FEATUREFAIL:
      print_to_output("Could not find fingerprint features");
      return p;
    case FINGERPRINT_INVALIDIMAGE:
      print_to_output("Could not find fingerprint features");
      return p;
    default:
      print_to_output("Unknown error");
      return p;
  }

  // OK converted!
  Serial.print("Creating model for #");  Serial.print(id);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    print_to_output("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    print_to_output("Communication error8");
    handle_error(__LINE__);
    return p;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    print_to_output("Fingerprints did not match");
    return p;
  } else {
    print_to_output("Unknown error");
    return p;
  }

  Serial.print("ID "); Serial.print(id);
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    mqttMessage["mode"] = "learning";
    mqttMessage["id"] = id;
    mqttMessage["state"] = "Success, stored!";
    mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
    client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
    print_to_output("Stored!");
    return true;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    print_to_output("Communication error9");
    handle_error(__LINE__);
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    print_to_output("Could not store in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    print_to_output("Error writing to flash");
    return p;
  } else {
    print_to_output("Unknown error");
    return p;
  }
}

uint8_t deleteFingerprint() {
  uint8_t p = -1;
  p = finger.deleteModel(id);

  if (p == FINGERPRINT_OK) {
    print_to_output("Deleted!");
    mqttMessage["mode"] = "deleting";
    mqttMessage["id"] = id;
    mqttMessage["state"] = "Deleted!";
    mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
    client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
    return true;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    print_to_output("Communication error10");
    handle_error(__LINE__);
    return p;
  } else if (p == FINGERPRINT_BADLOCATION) {
    print_to_output("Could not delete in that location");
    return p;
  } else if (p == FINGERPRINT_FLASHERR) {
    print_to_output("Error writing to flash");
    return p;
  } else {
    Serial.print("Unknown error: 0x"); Serial.print(p, HEX);
    return p;
  }
}

void reconnect() {
  while (!client.connected()) {       // Loop until connected to MQTT server
    Serial.print("Attempting MQTT connection...");
    if (client.connect(HOSTNAME, mqtt_username, mqtt_password, AVAILABILITY_TOPIC, 1, true, "offline")) {       //Connect to MQTT server
      print_to_output("connected");
      client.publish(AVAILABILITY_TOPIC, "online");         // Once connected, publish online to the availability topic
      client.subscribe(MODE_LEARNING);       //Subscribe to Learning Mode Topic
      client.subscribe(MODE_READING);
      client.subscribe(MODE_DELETE);
      client.subscribe(MODE_REBOOT);
      client.subscribe(MODE_FLUSH_MEMORY);
      client.subscribe(DEBUG_TOPIC);
      client.subscribe(SENSOR_ENABLED_TOPIC);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      print_to_output(" try again in 5 seconds");
      delay(5000);  // Will attempt connection again in 5 seconds
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {                    //The MQTT callback which listens for incoming messages on the subscribed topics

  if (strcmp(topic, MODE_LEARNING) == 0) {
    char charArray[3];
    for (int i = 0; i < length; i++) {
      //Serial.print((char)payload[i]);
      charArray[i] = payload[i];
    }
    id = atoi(charArray);
    
    print_to_output("Got learning command");
    // Valid ID
    if (id > 0 && id < 127) { 
      print_to_output("Entering Learning mode");
      mqttMessage["mode"] = "learning";
      mqttMessage["id"] = id;
      mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
      client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
      while (!getFingerprintEnroll());
      print_to_output("Exiting Learning mode");
      modeLearning = false;
      modeReading = true;
      modeDelete = false;
      id = 0;
    } else {
      print_to_output("No");
    }
  }

  if (strcmp(topic, MODE_DELETE) == 0) {
    char charArray[3];
    for (int i = 0; i < length; i++) {
      //Serial.print((char)payload[i]);
      charArray[i] = payload[i];
    }
    id = atoi(charArray);
    if (id > 0 && id < 128) {
      mqttMessage["mode"] = "deleting";
      mqttMessage["id"] = id;
      mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
      client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
      print_to_output("Entering delete mode");
      while (! deleteFingerprint());
      print_to_output("Exiting delete mode");
      delay(2000); //Make the mqttMessage readable in HA
      modeLearning = false;
      modeReading = true;
      modeDelete = false;
      id = 0;
    }
  }

  if (strcmp(topic, SENSOR_ENABLED_TOPIC) == 0) {
    String msg;
    for (int i = 0; i < length; i++) {
      msg += (char)payload[i];
    }

    if (msg == "on"){
      sensorOn = true;
      print_to_output("Turning sensor on");
    } else if (msg == "off") {
      sensorOn = false;
      print_to_output("Turning sensor off");
    }
  }


  if (strcmp(topic, MODE_REBOOT) == 0) {
    //String msg;
    //for (int i = 0; i < length; i++) {
    //  msg += (char)payload[i];
    //}
      print_to_output("Got MQTT reboot command");
      mqttMessage["mode"] = "reboot";
      mqttMessage["state"] = "Rebooting!";
      mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
      client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
      print_to_output("Rebooting!");
      WiFi.disconnect();                                    // Drop current connection
      delay(1000);
      ESP.restart();
  }


    if (strcmp(topic, MODE_FLUSH_MEMORY) == 0) {

      print_to_output("Got MQTT command to flush memory");
      mqttMessage["mode"] = "Erase";
      mqttMessage["state"] = "Erasing All!";
      mqttMessageSize = serializeJson(mqttMessage, mqttBuffer);
      client.publish(STATE_TOPIC, mqttBuffer, mqttMessageSize);
      print_to_output("Erasing all finger prints from memory!");
      finger.emptyDatabase();
    }

}