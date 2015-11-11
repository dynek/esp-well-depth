//
// what: probe bottom of well and post depth to some web server
// author: Pierrick Brossin <github@bs-network.net>
// version: 0.1
// hardware: ESP8266 (ESP-07), one US-100 (Ultrasonic Sensor)
// software: this code and the other side is an http server (POST depth and graph them)
// monitoring: could be a good idea
//
// why NDEBUG: http://stackoverflow.com/questions/5473556/what-is-the-ndebug-preprocessor-macro-used-for-on-different-platforms

// debug
#define NDEBUG // comment line to disable debugging
#ifdef NDEBUG
  #define DEBUG_PRINT(x)      Serial.print (x)
  #define DEBUG_PRINTLN(x)    Serial.println (x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// include libraries
#include <ESP8266WiFi.h>

// clock watcher
#define SLEEP_INTERVAL 3774000000 // = ~1 hour (microseconds)

// wifi network
#define WIFI_SSID      "SSID"
#define WIFI_PASSWORD  "PASSWORD"
#define WIFI_WAIT      500
#define WIFI_TIMEOUT   20 // x * WIFI_WAIT -> 10 x 500 = 10sec

// remote web server to post depth
#define WEB_HOST  "outside.host.com"
#define WEB_PORT  2080
#define WEB_PATH  "/probe.php"
#define WEB_VALUE "depth"

// ultrasonic us-100
#define TRIGGER_PIN            4 // pin to trigger of us-100
#define ECHO_PIN               5 // pin to echo of us-100
#define POWER_PIN             12 // pin to power us-100
#define PROBE_ARRAY_SIZE       5 // number of probes
#define MIN_WELL_DEPTH        10 // minimum well depth to avoid inaccurate values
#define MAX_WELL_DEPTH       250 // maximum well depth to avoid inaccurate values
#define VARIATION_PERCENTAGE 0.1 // 10%
#define PROBE_TIMEOUT          2 // number of retries
#define DEPTH_WAIT           500 // ms between each retry
#define DEPTH_TIMEOUT          2 // x * PROBE_WAIT -> 2 x 500 = 1sec (+ time it takes to probe)

// Use WiFiClient class to create TCP connections
WiFiClient client;

// initiate wifi link
unsigned int initiate_wifi_link(void) {
  // let's connect to wifi network from scratch
  DEBUG_PRINTLN("clear wifi setup");
  WiFi.disconnect();
  DEBUG_PRINTLN("set esp8266 as station");
  WiFi.mode(WIFI_STA);
  DEBUG_PRINT("initiate wifi connection: "); 
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // wait for link to be established
  unsigned int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    // timeout
    if(counter++ == WIFI_TIMEOUT) {
      WiFi.disconnect();
      return false;
    }
    DEBUG_PRINT(".");
    delay(WIFI_WAIT);
  }
  DEBUG_PRINTLN("");
  DEBUG_PRINT("wifi connected: ");
  DEBUG_PRINTLN(WiFi.localIP());
  // successfully connected
  return true;
}

// send pulse, fetch echo and return value
unsigned int get_distance(void) {
  DEBUG_PRINTLN("entering get_distance");
  
  // send pulse
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(50);
  digitalWrite(TRIGGER_PIN, LOW);
  
  // read echo
  unsigned long echo_value = pulseIn(ECHO_PIN, HIGH);

  DEBUG_PRINT("raw echo: ");
  DEBUG_PRINTLN(echo_value);
  
  // return length in cm
  return echo_value / 29.1 / 2;
}

// get an (as accurate as possible) depth
unsigned int get_depth (void) {
  DEBUG_PRINTLN("entering get_depth");
  
  // array to hold probes
  unsigned int probes[PROBE_ARRAY_SIZE] = { 0 };

  // power ultrasonic sensor
  digitalWrite(POWER_PIN, HIGH);
  delay(1000);

  unsigned int i = 0;
  unsigned int probes_total = 0;
  unsigned int value = 0;
  unsigned int count = 1;
  boolean bFirst = true;
  for(i = 0; i < PROBE_ARRAY_SIZE; i++) {
    DEBUG_PRINT("loop ");
    DEBUG_PRINTLN(i);

    while((value < MIN_WELL_DEPTH || value > MAX_WELL_DEPTH) && count++ <= PROBE_TIMEOUT) {
      if(!bFirst) {
        DEBUG_PRINT("incorrect value: ");
        DEBUG_PRINT(value);
        DEBUG_PRINTLN(" - retrying");
        delay(500); // if value is incorrect sleep for 500ms before retrying
      }
      value = get_distance();
      bFirst = false;
    }
    probes[i] = value;
    probes_total += value;

    DEBUG_PRINT("Value ");
    DEBUG_PRINT(i);
    DEBUG_PRINT(" equals ");
    DEBUG_PRINTLN(value);

    // reset for next loop
    bFirst = true;
    value = 0;
    count = 1;
  }

  // power off ultrasonic sensor
  digitalWrite(POWER_PIN, LOW);

  if(probes_total > 0) {
    float probes_average = probes_total / PROBE_ARRAY_SIZE;
    DEBUG_PRINT("Probes average: ");
    DEBUG_PRINTLN(probes_average);

    float accepted_delta = probes_average * VARIATION_PERCENTAGE;
    DEBUG_PRINT("Accepted delta: ");
    DEBUG_PRINTLN(accepted_delta);

    float lowest_accepted_value = probes_average - accepted_delta;
    float highest_accepted_value = probes_average + accepted_delta;
    DEBUG_PRINT("lowest accepted value: ");
    DEBUG_PRINTLN(lowest_accepted_value);
    DEBUG_PRINT("highest accepted value: ");
    DEBUG_PRINTLN(highest_accepted_value);

    int accepted_probes_count = 0; // number of probes within accepted range
    probes_total = 0; // reusing variable
    for(i = 0; i < PROBE_ARRAY_SIZE; i++) {
      // if probe is within accepted range, count it and add it to total
      if(probes[i] > lowest_accepted_value && probes[i] < highest_accepted_value) {
        accepted_probes_count++;
        probes_total += probes[i];
      }
    }

    // return calculated average
    if(accepted_probes_count > 0) return probes_total / accepted_probes_count;
  }

  // in case something weird occurs, avoid division by 0 and return -1
  return -1;
}

// setup program
void setup(void) {
  #ifdef NDEBUG
    // hold on for 1 sec so Serial Monitor can be opened
    delay(1000);
    // start serial communication
    Serial.begin(115200);
  #endif

  // setup OK
  DEBUG_PRINTLN("configure ultrasonic us-100");
  pinMode(POWER_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // setup OK
  DEBUG_PRINTLN("setup done!");
}

// well ... loop
void loop(void) {
  // initiate wifi link
  if(!initiate_wifi_link()) {
    DEBUG_PRINTLN("WIFI NOT OK");
  } else {
    // get well depth
    boolean bFirst = true;
    unsigned int count = 1;
    unsigned int depth = 0;
    while(depth == 0 && count++ <= DEPTH_TIMEOUT) {
      if(!bFirst) delay(DEPTH_WAIT);
      depth = get_depth();
      if(bFirst) bFirst = false;
    }

    // check if depth could be determined
    if(depth == 0) {
      DEBUG_PRINTLN("Could not determine depth!");
    } else {
      DEBUG_PRINT("Depth = ");
      DEBUG_PRINTLN(depth);
    
      // connect to web server
      DEBUG_PRINT("connecting to ");
      DEBUG_PRINTLN(WEB_HOST);
   
      if(!client.connect(WEB_HOST, WEB_PORT)) {
        DEBUG_PRINTLN("connection failed");  
      } else {
        DEBUG_PRINTLN("POSTing depth to remote server");  
        
        // construct data to post
        char *post_data = (char *)malloc(10);
        sprintf(post_data,"depth=%d", depth);
    
        // post value to the server
        client.print("POST "); client.print(WEB_PATH); client.println(" HTTP/1.1");
        client.print("Host: "); client.println(WEB_HOST);
        client.println("Content-Type: application/x-www-form-urlencoded");
        client.print("Content-Length: ");
        client.println(strlen(post_data));
        client.println("Connection: close");
        client.println();
        client.println(post_data);
        client.println();
        delay(10);

        DEBUG_PRINTLN("Done!");  
      }
    }

    WiFi.disconnect();
    DEBUG_PRINTLN("disconnected from AP");
  }

  // turn off wifi
  WiFi.mode(WIFI_OFF);
  DEBUG_PRINTLN("WiFi off");

  // sleep
  DEBUG_PRINTLN("now deep sleep for 1 hour");
  unsigned int sleeptime = SLEEP_INTERVAL - micros();
  ESP.deepSleep(sleeptime, WAKE_NO_RFCAL);
  delay(500); // wait deep sleep
}
