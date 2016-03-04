#include <Adafruit_LSM9DS0.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <SPI.h>
#include <WiFiUdp.h>
#include <Wire.h>

////// EEPROM Layout:
// 0x000  : SSID          [0x20]
char ssid[0x20];
// 0x020  : Wifi Password [0x20]
char passwd[0x20];
// 0x040  : UDP Hostname  [0x80]
char udp_hostname[0x80];
// 0x0C0  : UDP Port      [0x04]
unsigned int udp_port = 0;
// 0x0C4  : Padding       [0x0C]
// 0x0D0  : session_count [0x04]
unsigned int session_count = 0;
// 0x0D4  : Padding       [0x0C]
// 0x0E0  : Start of empty space
// 0x200  : End of EEPROM (not really)

WiFiUDP Udp;

unsigned long long unix_time_offset_ns = 0;

#define INFLUX_OUTPUT true

#ifdef JSON_OUTPUT
char udp_buffer[0x100];
int udp_buffer_size = 0x100;
#endif

#ifdef INFLUX_OUTPUT
char udp_buffer[0x300];
int udp_buffer_size = 0x300;
#endif

// Init LSM sensor object
Adafruit_LSM9DS0 lsm = Adafruit_LSM9DS0();

void zeroStuffOut() {
  memset(ssid, 0, sizeof(ssid));
  memset(passwd, 0, sizeof(passwd));
  memset(udp_hostname, 0, sizeof(udp_hostname));
  udp_port = 0;
  session_count = 0;
  memset(udp_buffer, 0, (size_t)udp_buffer_size);
}

void setupSensor() {
  lsm.setupAccel(lsm.LSM9DS0_ACCELRANGE_2G);
  lsm.setupMag(lsm.LSM9DS0_MAGGAIN_2GAUSS);
  lsm.setupGyro(lsm.LSM9DS0_GYROSCALE_245DPS);
}

void printEEPROM() {
  Serial.println("EEPROM 0-512:");
  for (int i = 0; i < 0x10; i++) {
    String printme = "";
    for (int j = (0x20 * i); j < (0x20 * (i + 1)); j++ ) {
      printme += (char) EEPROM.read(j);
    }
    Serial.println(printme);
  }
}

inline void writeStringToEEPROM(char* writeme, int len, int offset) {
  int i = 0;
  while(writeme[i] != 0x0 && i < len) {
    EEPROM.write(i + offset, writeme[i]);
    i++;
  }
  while(i < len) {
    EEPROM.write(i + offset, 0);
    i++;
  }
}

inline void getStringFromEEPROM(char* dest, int maxlen, int offset) {
  char temp = 0;
  int i = 0;
  do {
    temp = EEPROM.read(i + offset);
    dest[i++] = temp;
  } while (temp != 0 && i < maxlen);
}

// Padding ints to 16 bytes because I wanna
inline void writeIntToEEPROM(int writeme, int offset) {
  unsigned char bytes[4];
  unsigned long n = writeme;
  bytes[0] = (n >> 24) & 0xFF;
  bytes[1] = (n >> 16) & 0xFF;
  bytes[2] = (n >> 8) & 0xFF;
  bytes[3] = n & 0xFF;
  EEPROM.write(0 + offset, bytes[0]);
  EEPROM.write(1 + offset, bytes[1]);
  EEPROM.write(2 + offset, bytes[2]);
  EEPROM.write(3 + offset, bytes[3]);
  for(int i = 4; i < 0x10; i++) {
    EEPROM.write(i + offset, 0);
  }
}

inline int getIntFromEEPROM(int offset) {
  unsigned long bytes[4];
  int retval;
  bytes[0] = EEPROM.read(0 + offset);
  bytes[1] = EEPROM.read(1+offset);
  bytes[2] = EEPROM.read(2+offset);
  bytes[3] = EEPROM.read(3+offset);
  retval = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
  return retval;
}

void setConfig(char* a_ssid, char* a_passwd, char* a_hostname, int a_port, int a_sescount) {
  writeStringToEEPROM(a_ssid, 0x20, 0);
  writeStringToEEPROM(a_passwd, 0x20, 0x20);
  writeStringToEEPROM(a_hostname, 0x80, 0x40);
  writeIntToEEPROM(a_port, 0xC0);
  writeIntToEEPROM(a_sescount, 0xD0);
  EEPROM.commit();
}

void getConfig() {
  getStringFromEEPROM(ssid, 0x20, 0);
  getStringFromEEPROM(passwd, 0x20, 0x20);
  getStringFromEEPROM(udp_hostname, 0x80, 0x40);
  udp_port = getIntFromEEPROM(0xC0);
  session_count = getIntFromEEPROM(0xD0);
}

// this function is the product of a desparate man who has
// discovered 6 different sprintf() functions for the
// esp8266 that can't handle "%llu".
// It is a product of a mind that is not merely twisted, but actually sprained.
inline void longLongToStr(long long n, char* pStr) {
  char buf[32];
  memset(buf, 0, 32);
  int i = 0;
  int m;
  int len;
  char c;

  do
  {
    m = n % (long)10;
    buf[i] = '0'+ m;
    n = n / (long)10;
    i++;
  }
  while(n != 0);
  i--; // i now contains the index of the last character we wrote to buf[]
  int j = 0;
  while(i >= 0) {
    pStr[j++] = buf[i--];
  }
  while(j < 32) {
    pStr[j++] = 0;
  }
}



String buildJSONPacket() {
  String retval = "{ t: "; retval += millis();
  retval += ", session: "; retval += session_count;
  retval += ", ax: "; retval += ((int)lsm.accelData.x);
  retval += ", ay: "; retval += ((int)lsm.accelData.y);
  retval += ", az: "; retval += ((int)lsm.accelData.z);
  retval += ", gx: "; retval += ((int)lsm.gyroData.x);
  retval += ", gy: "; retval += ((int)lsm.gyroData.y);
  retval += ", gz: "; retval += ((int)lsm.gyroData.z);
  retval += ", mx: "; retval += ((int)lsm.magData.x);
  retval += ", my: "; retval += ((int)lsm.magData.y);
  retval += ", mz: "; retval += ((int)lsm.magData.z);
  retval += ", temp: "; retval += ((int)lsm.temperature);
  retval += " }\n";
  return retval;
}

/*
 * © Francesco Potortì 2013 - GPLv3 - Revision: 1.13
 *
 * Send an NTP packet and wait for the response, return the Unix time
 *
 * To lower the memory footprint, no buffers are allocated for sending
 * and receiving the NTP packets.  Four bytes of memory are allocated
 * for transmision, the rest is random garbage collected from the data
 * memory segment, and the received packet is read one byte at a time.
 * The Unix time is returned, that is, seconds from 1970-01-01T00:00.
 */
unsigned long inline ntpUnixTime (WiFiUDP &udp) {
  static int udpInited = udp.begin(123); // open socket on arbitrary port

  const char timeServer[] = "pool.ntp.org";  // NTP server

  // Only the first four bytes of an outgoing NTP packet need to be set
  // appropriately, the rest can be whatever.
  const long ntpFirstFourBytes = 0xEC0600E3; // NTP request header

  // Fail if WiFiUdp.begin() could not init a socket
  if (! udpInited)
    return 0;

  // Clear received data from possible stray received packets
  udp.flush();

  // Send an NTP request
  if (! (udp.beginPacket(timeServer, 123) // 123 is the NTP port
   && udp.write((byte *)&ntpFirstFourBytes, 48) == 48
   && udp.endPacket()))
    return 0;       // sending request failed

  // Wait for response; check every pollIntv ms up to maxPoll times
  const int pollIntv = 150;   // poll every this many ms
  const byte maxPoll = 15;    // poll up to this many times
  int pktLen;       // received packet length
  for (byte i=0; i<maxPoll; i++) {
    if ((pktLen = udp.parsePacket()) == 48)
      break;
    delay(pollIntv);
  }
  if (pktLen != 48)
    return 0;       // no correct packet received

  // Read and discard the first useless bytes
  // Set useless to 32 for speed; set to 40 for accuracy.
  const byte useless = 40;
  for (byte i = 0; i < useless; ++i)
    udp.read();

  // Read the integer part of sending time
  unsigned long time = udp.read();  // NTP time
  for (byte i = 1; i < 4; i++)
    time = time << 8 | udp.read();

  // Round to the nearest second if we want accuracy
  // The fractionary part is the next byte divided by 256: if it is
  // greater than 500ms we round to the next second; we also account
  // for an assumed network delay of 50ms, and (0.5-0.05)*256=115;
  // additionally, we account for how much we delayed reading the packet
  // since its arrival, which we assume on average to be pollIntv/2.
  time += (udp.read() > 115 - pollIntv/8);

  // Discard the rest of the packet
  udp.flush();

  return time - 2208988800ul;   // convert NTP time to Unix time
}
////// End Francisco's GPL code I stole ////////

void setupTimeOffset() {
  char buf[32];
  memset(buf, 0, 32);
  unsigned long unix_time_sec = 0L;

  do {
    Serial.println("Attempting to poll pool.ntp.org for time...");
    unix_time_sec = ntpUnixTime(Udp);
    delay(250);
  } while(unix_time_sec < 1);
  Serial.print("Got time: "); Serial.println(unix_time_sec);
  unsigned long long offset_ns = (long long) unix_time_sec * (long long)1000000000L;
  unsigned long long millis_ns = (long long) millis() * 1000000; // yes this is a dumb variable name. fight me.
  unix_time_offset_ns = offset_ns - millis_ns;
}

unsigned long long getInfluxTime() {
  unsigned long long ts = unix_time_offset_ns;
  ts += (long long) ((long long) millis() * 1000000L);
  return ts;
}

void getInfluxTimestamp(char* dest) {
  longLongToStr(getInfluxTime(), dest);
}

String buildInfluxPacket() {
  char ts[32];
  String timestamp = "";
  String session_tag = "session=sack";
  session_tag += session_count;
  memset(ts, 0, 32);
  getInfluxTimestamp(ts);
  timestamp += ts;
  
  // Holy string concatenation, Batman!
  String retval = "";
  retval += "ax,"; retval += session_tag; retval += " value="; retval += ((int)lsm.accelData.x); retval += "i "; retval += timestamp; retval += "\n";
  retval += "ay,"; retval += session_tag; retval += " value="; retval += ((int)lsm.accelData.y); retval += "i "; retval += timestamp; retval += "\n";
  retval += "az,"; retval += session_tag; retval += " value="; retval += ((int)lsm.accelData.z); retval += "i "; retval += timestamp; retval += "\n";
  retval += "gx,"; retval += session_tag; retval += " value="; retval += ((int)lsm.gyroData.x); retval += "i "; retval += timestamp; retval += "\n";
  retval += "gy,"; retval += session_tag; retval += " value="; retval += ((int)lsm.gyroData.y); retval += "i "; retval += timestamp; retval += "\n";
  retval += "gz,"; retval += session_tag; retval += " value="; retval += ((int)lsm.gyroData.z); retval += "i "; retval += timestamp; retval += "\n";
  retval += "mx,"; retval += session_tag; retval += " value="; retval += ((int)lsm.magData.x); retval += "i "; retval += timestamp; retval += "\n";
  retval += "my,"; retval += session_tag; retval += " value="; retval += ((int)lsm.magData.y); retval += "i "; retval += timestamp; retval += "\n";
  retval += "mz,"; retval += session_tag; retval += " value="; retval += ((int)lsm.magData.z); retval += "i "; retval += timestamp; retval += "\n";
  retval += "temp,"; retval += session_tag; retval += " value="; retval += ((int)lsm.temperature); retval += "i "; retval += timestamp; retval += "\n";
  return retval;
}

inline void connectToWifi(const char* ssid, const char* password) {
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
 
  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  zeroStuffOut();
  pinMode(0, INPUT);
  if (!lsm.begin()) {
    Serial.println("Unable to initialize the LSM9DS0. Check your wiring!");
    delay(999999999);
  }
  Serial.println("Found LSM9DS0 9DOF");
  Serial.println("");
  EEPROM.begin(512);
  // Do this once to get the values into EEPROM
  /*
  setConfig(
    "YOUR_SSID",
    "YOUR_PASSWD",
    "YOUR_HOSTNAME",
    1234,
    0
  );
  */
  getConfig();
  session_count++;
  writeIntToEEPROM(session_count, 0xD0);
  EEPROM.commit();
  delay(5000);
}

void loop() {
  connectToWifi(ssid, passwd);
  setupTimeOffset();
  Serial.print("UDP target host: "); Serial.print(udp_hostname); Serial.print(":"); Serial.println(udp_port);
  //Serial.println("Starting sensor loop");
  while(digitalRead(0) == HIGH) {
    lsm.read();
    #ifdef JSON_OUTPUT
    buildJSONPacket().toCharArray(udp_buffer, udp_buffer_size);
    #endif
    #ifdef INFLUX_OUTPUT
    buildInfluxPacket().toCharArray(udp_buffer, udp_buffer_size);
    #endif
    Udp.beginPacket(udp_hostname, udp_port);
    Udp.write(udp_buffer);
    Udp.endPacket();
    delay(1);
  }
  Serial.println("GPIO pressed, exiting sensor loop!");
  delay(999999999);
}

/////////// UDP Message format (JSON): /////////////
/*
{ t: 116747, ax: -1235, ay: 5567, az: 234, mx: 3546, my: -7786, mz: 942, gx: 12, gy: -166, gz: 24, temp: 67 }\n

String                      | Max Length (bytes)
"{ t: "                     | 5
unsigned long t as ascii    | 12
", session: "               | 11
unsigned int session_count  | 12
", ax: "                    | 6
unsigned long ax as ascii   | 12
", ay: "                    | 6
unsigned long ay as ascii   | 12
", az: "                    | 6
unsigned long az as ascii   | 12
", gx: "                    | 6
unsigned long mx as ascii   | 12
", gy: "                    | 6
unsigned long my as ascii   | 12
", gz: "                    | 6
unsigned long mz as ascii   | 12
", mx: "                    | 6
unsigned long mx as ascii   | 12
", my: "                    | 6
unsigned long my as ascii   | 12
", mz: "                    | 6
unsigned long mz as ascii   | 12
", temp: "                  | 8
unsigned long temp as ascii | 12
" }\n"                      | 3
////// Total length:        | 225 bytes
////// Safe buffer size:    | 256 bytes
 */

//////////// UDP message format (InfluxDB): ////////////
/*
ax,sess=sack001, value=-2147483648i 1434055562000000000
ay,sess=sack001, value=-2147483648i 1434055562000000000
az,sess=sack001, value=-2147483648i 1434055562000000000
gx,sess=sack001, value=-2147483648i 1434055562000000000
gy,sess=sack001, value=-2147483648i 1434055562000000000
gz,sess=sack001, value=-2147483648i 1434055562000000000
mx,sess=sack001, value=-2147483648i 1434055562000000000
my,sess=sack001, value=-2147483648i 1434055562000000000
mz,sess=sack001, value=-2147483648i 1434055562000000000
temp,host=sack0001, value=-2147483648i 1434055562000000000
/// Total length:       563 bytes
/// Safe buffer size:   768 bytes
 */

