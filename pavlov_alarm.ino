#if defined(ESP8266)
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>          //https://github.com/esp8266/Arduino
#endif

//needed for library
#include <DNSServer.h>
#if defined(ESP8266)
#include <ESP8266WebServer.h>
#else
#include <WebServer.h>
#endif

#define UDP_ASYNC   1

#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

#if defined(UDP_ASYNC)
#include <ESPAsyncUDP.h>
AsyncUDP udp;
#else
#include <WiFiUdp.h>
WiFiUDP udp;
#endif

#include <Ticker.h>

#define CONTROL_PIN  D2

//flag for saving data
bool shouldSaveConfig = false;
int brightness = 0;

Ticker dimmer;
Ticker udp_handler;

IPAddress timeServerIP; // time.nist.gov NTP server address
// const char* ntpServerName = "time.nist.gov";
const char* ntpServerName = "pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte outPacketBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
byte inPacketBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
unsigned int localPort = 2390; // local port to listen for UDP packets



void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(CONTROL_PIN, OUTPUT);
  
  Serial.begin(9600);
  
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  Serial.println("WM: Initializing");
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  //reset saved settings
  //wifiManager.resetSettings();

  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  Serial.println("WM: Creating AP");
  wifiManager.autoConnect("PavlovAP", "doglamp");

  //if you get here you have connected to the WiFi
  Serial.println("WM: Connected to AP");

  dimmer.attach(0.010, adjust_brightness);
  // udp_handler.attach(10.0, ntpUnixTime);

  #if defined(UDP_ASYNC)
  udp.onPacket([](AsyncUDPPacket packet) {
    Serial.print("UDP Packet Type: ");
    Serial.println(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
    
    Serial.print("From: ");
    Serial.print(packet.remoteIP());
    Serial.print(":");
    Serial.println(packet.remotePort());
    
    Serial.print("To: ");
    Serial.print(packet.localIP());
    Serial.print(":");
    Serial.println(packet.localPort());
    
    Serial.print("Length: ");
    Serial.println(packet.length());
    Serial.println();

    memcpy(inPacketBuffer, packet.data(), NTP_PACKET_SIZE);    
    parsePacket();
  });
  #else
    Serial.println("UDP: starting");
    udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(udp.localPort());
  #endif
  
  //Send unicast
  // udp.print("Hello Server!");
}

void adjust_brightness() {
  brightness = (brightness + 1) % 256;
  analogWrite(CONTROL_PIN, brightness);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  sendNtpPacket(timeServerIP); // send an NTP packet to a time server

  #if !defined(UDP_ASYNC)
  // wait to see if a reply is available
  delay(1000);  
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    Serial.println();
    
    // We've received a packet, read the data from it
    udp.read(inPacketBuffer, NTP_PACKET_SIZE); // read the packet into the buffer


    parsePacket();

  // wait ten seconds before asking for the time again
  }
  #endif
  delay(10000 + 1000 * random(10, 20));
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}


void buildPacket() {
  Serial.println("Building NTP packet...");
  memset(outPacketBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  outPacketBuffer[0] = 0b11100011;   // LI, Version, Mode
  outPacketBuffer[1] = 0;     // Stratum, or type of clock
  outPacketBuffer[2] = 6;     // Polling Interval
  outPacketBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  outPacketBuffer[12]  = 49;
  outPacketBuffer[13]  = 0x4E;
  outPacketBuffer[14]  = 49;
  outPacketBuffer[15]  = 52;
}

// send an NTP request to the time server at the given address
unsigned long sendNtpPacket(IPAddress& address) {
  buildPacket();
  WiFi.hostByName(ntpServerName, timeServerIP); 
  Serial.print("NTP: connecting to ");
  Serial.print(ntpServerName);
  Serial.print(" at ");
  Serial.println(timeServerIP);
  Serial.println("sending NTP packet...");
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  #if defined(UDP_ASYNC)
    // udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.writeTo(outPacketBuffer, NTP_PACKET_SIZE, timeServerIP, 123);
  #else
    udp.beginPacket(address, 123); //NTP requests are to port 123
    udp.write(outPacketBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
  #endif
}



//callback notifying us of the need to save config
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void parsePacket() {
  //the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, esxtract the two words:
  unsigned long highWord = word(inPacketBuffer[40], inPacketBuffer[41]);
  unsigned long lowWord = word(inPacketBuffer[42], inPacketBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  Serial.print("Seconds since Jan 1 1900 = " );
  Serial.println(secsSince1900);

  // now convert NTP time into everyday time:
  Serial.print("Unix time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;
  // print Unix time:
  Serial.println(epoch);

  // print the hour, minute and second:
  Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
  Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
  Serial.print(':');
  if ( ((epoch % 3600) / 60) < 10 ) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
  Serial.print(':');
  if ( (epoch % 60) < 10 ) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.println(epoch % 60); // print the second
}

