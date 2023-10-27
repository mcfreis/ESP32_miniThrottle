/*
miniThrottle, A WiThrottle/DCC-Ex Throttle for model train control

MIT License

Copyright (c) [2021-2023] [Enfield Cat]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/



/*
 * Reference Page: https://www.jmri.org/help/en/package/jmri/jmrit/withrottle/Protocol.shtml
 */
#include "miniThrottle.h"
#include "static_defs.h"

#include "nvs.h"
#include "util.h"
#include "serialTerm.h"
#include "displayRoutines.h"
#include "fastClock.h"
#include "connectMgr.h"
#include "keypadHandler.h"
#include "switchHandler.h"
#include "keepAlive.h"

// Initialize the OLED display using i2c interface, Adjust according to display device
// DisplaySSD1306_128x64_I2C display(-1); // or (-1,{busId, addr, scl, sda, frequency})
#ifdef SSD1306
DisplaySSD1306_128x64_I2C display (-1,{0, DISPLAYADDR, SCK_PIN, SDA_PIN, -1});
#endif
#ifdef SSD1327
DisplaySSD1327_128x128_I2C display (-1,{0, DISPLAYADDR, SCK_PIN, SDA_PIN, -1});
#endif
#ifdef ST7735
// params {reset, {busid, cs, dc, freq, scl, sca}}
// reset may be -1 if not used, otherwise -1 => defaults
DisplayST7735_128x160x16_SPI display(SPI_RESET,{-1, SPI_CS, SPI_DC, 0, SPI_SCL, SPI_SDA});
#endif
#ifdef ST7789
DisplayST7789_135x240x16_SPI display(SPI_RESET,{-1, SPI_CS, SPI_DC, 0, SPI_SCL, SPI_SDA});
#endif
#ifdef ILI9341
DisplayILI9341_240x320x16_SPI display(SPI_RESET,{-1, SPI_CS, SPI_DC, 0, SPI_SCL, SPI_SDA});
#endif

#ifdef USEWIFI
// WiFi Server Definitions
WiFiClient client;                    // client to use for connection to CS
#endif
#ifdef SERIALCTRL
HardwareSerial serial_dev(1);  // serial port to use when connection directly to DCC-Ex
#endif
SemaphoreHandle_t nvsSem       = xSemaphoreCreateMutex();  // only one thread to access nvs at a time
SemaphoreHandle_t serialSem    = xSemaphoreCreateMutex();  // only one thread to access serial to dcc-ex at a time
SemaphoreHandle_t consoleSem   = xSemaphoreCreateMutex();  // only aalow one message to be written console at a time
SemaphoreHandle_t velociSem    = xSemaphoreCreateMutex();  // used for velocity / direction changes table lock of locomotives
SemaphoreHandle_t functionSem  = xSemaphoreCreateMutex();  // used for setting functions - table lock of all functions
SemaphoreHandle_t turnoutSem   = xSemaphoreCreateMutex();  // used for setting turnouts  - table lock of all turnouts
SemaphoreHandle_t routeSem     = xSemaphoreCreateMutex();  // used for setting routes    - table lock of all routes
// Normal TCP/IP operations work OK in single threaded environment, but benefits from semaphore protection in multi-threaded envs
// This does lead to kludges to keep operations such as available() outside if or while logic to maintain runtime stability.
SemaphoreHandle_t tcpipSem     = xSemaphoreCreateMutex();
SemaphoreHandle_t fastClockSem = xSemaphoreCreateMutex();  // for sending/receiving/displaying fc messages
SemaphoreHandle_t shmSem       = xSemaphoreCreateMutex();  // shared memory for used for moved blocks of data between tasks/threads
#ifdef WEBLIFETIME
SemaphoreHandle_t webServerSem = xSemaphoreCreateMutex();  // used by different web server threads
#endif
#ifdef BACKLIGHTPIN
SemaphoreHandle_t screenSvrSem = xSemaphoreCreateMutex();  // used to coordinate screen/menu blanking
#endif
#ifdef RELAYPORT
SemaphoreHandle_t relaySvrSem  = xSemaphoreCreateMutex();  // used by various relay threads to coordinate memory/dcc-ex access
#endif
#ifdef OTAUPDATE
#ifndef NOHTTPCLIENT
SemaphoreHandle_t otaSem       = xSemaphoreCreateMutex();  // used to ensure only one ota activity at a time
HTTPClient *otaHttp            = new HTTPClient();         // Client used for update
#endif
#endif
QueueHandle_t cvQueue          = xQueueCreate (2,  sizeof(int16_t));  // Queue for querying CV Values
QueueHandle_t keyboardQueue    = xQueueCreate (3,  sizeof(char));     // Queue for keyboard type of events
QueueHandle_t keyReleaseQueue  = xQueueCreate (3,  sizeof(char));     // Queue for keyboard release type of events
QueueHandle_t dccAckQueue      = xQueueCreate (10, sizeof(uint8_t));  // Queue for dcc updates, avoid flooding of WiFi
QueueHandle_t dccLocoRefQueue  = xQueueCreate (10, sizeof(uint8_t));  // Queue for dcc locomotive speed and direction changes
QueueHandle_t dccTurnoutQueue  = xQueueCreate (10, sizeof(uint8_t));  // Queue for dcc turnout data
QueueHandle_t dccRouteQueue    = xQueueCreate (10, sizeof(uint8_t));  // Queue for dcc route data
QueueHandle_t dccOffsetQueue   = xQueueCreate (10, sizeof(uint8_t));  // Queue for dcc array offsets for setup of data
QueueHandle_t diagQueue        = xQueueCreate (256, sizeof(char));    // diagnostic data queue.
struct locomotive_s   *locoRoster   = (struct locomotive_s*) malloc (sizeof(struct locomotive_s) * MAXCONSISTSIZE);
struct turnoutState_s *turnoutState = NULL;  // table of turnout states
struct turnout_s      *turnoutList  = NULL;  // table of turnouts
struct routeState_s   *routeState   = NULL;  // table of route states
struct route_s        *routeList    = NULL;  // table of routes
char *sharedMemory = NULL;     // inter process communication shared memory buffer
#ifdef BACKLIGHTPIN
#ifdef SCREENSAVER
uint64_t screenActTime  = 0;   // time stamp of last user activity
uint64_t blankingTime   = 0;   // min time prior to blanking
#endif
uint16_t backlightValue = 200; // backlight brightness 0-255
#endif
#ifndef NODISPLAY
int screenWidth      = 0;      // screen geometry in pixels
int screenHeight     = 0;
#endif
int keepAliveTime    = 10;     // WiThrottle keepalive time
#ifdef BRAKEPRESPIN
int brakePres        = 0;      // brake pressure register
#endif
uint32_t fc_time = 36;         // in jmri mode we can receive fast clock, in relay mode we can send it, 36s past midnight => not updated
uint32_t defaultLatchVal     = 0;  // bit map of which functions latch
uint32_t defaultLeadVal      = 0;  // bit map of which functions are for lead loco only
const uint16_t routeDelay[]        = {0, 500, 1000, 2000, 3000, 4000}; // selectable delay times when setting route in DCCEX mode
const size_t num_routeDelay = sizeof(routeDelay) / sizeof(routeDelay[0]);
uint16_t numberOfNetworks    = 0;  // count of networks detected in scan of networks
uint16_t initialLoco         = 3;  // lead loco register for in-throttle consists
uint16_t *dccExNumList       = NULL;// List of numeric IDs returned by DCC-Ex when querying Ex-Rail
uint8_t dccExNumListSize     = 0;  // size of the above array
uint8_t locomotiveCount      = 0;  // count of defined locomotives
uint8_t turnoutCount         = 0;  // count of defined turnouts
uint8_t turnoutStateCount    = 0;  // count of defined turnout states
uint8_t routeCount           = 0;  // count of defined routes
uint8_t routeStateCount      = 0;  // count of defined route states
uint8_t lastMainMenuOption   = 0;  // track last selected option in main menu
uint8_t lastLocoMenuOption   = 0;  // track last selected option in locomotive menu
uint8_t lastSwitchMenuOption = 0;  // track last selected option in switch/turnout menu
uint8_t lastRouteMenuOption  = 0;  // track last selected option in route menu
uint8_t debuglevel           = DEBUGLEVEL;
uint8_t charsPerLine;              // using selected font the number chars across display
uint8_t linesPerScreen;            // using selected font the number lines on display
uint8_t debounceTime = DEBOUNCEMS; // debounce time to allow on mechanical (electric) switches
uint8_t cmdProtocol  = UNDEFINED;  // protocol to use when running this unit
uint8_t nextThrottle = 'A';        // use as throttle "number" in WiThrottle protocol
uint8_t screenRotate = 0;          // local display orientation
uint8_t dccPowerFunc = DCCPOWER;   // variant of power on to use for DCC power on
uint8_t coreCount    = 2;          // cpu core count, used for some diagnostics
uint8_t inventoryLoco = LOCALINV;  // default to local inventory for locos
uint8_t inventoryTurn = LOCALINV;  // default tp local inventory for turnouts
uint8_t inventoryRout = LOCALINV;  // default to local inventory for routes / automations
#ifdef RELAYPORT
WiFiServer *relayServer;                  // the relay server wifi service
struct relayConnection_s *remoteSys = NULL;      // table of connected clients and assoc states
uint64_t maxRelayTimeOut = ((KEEPALIVETIMEOUT * 2) + 1 ) * uS_TO_S_FACTOR;
uint32_t localinPkts     = 0;             // packet count over serial connection
uint32_t localoutPkts    = 0;             // packet count over serial connection
uint16_t relayPort       = RELAYPORT;     // tcp/ip relay listening port number
uint8_t maxRelay         = MAXRELAY;      // max number of relay clients
uint8_t relayMode        = WITHROTRELAY;  // protocol to relay
uint8_t relayCount       = 0;
uint8_t relayClientCount = 0;
uint8_t maxRelayCount    = 0;             // high water mark
// When relaying we can also supply fast clock time
float fc_multiplier      = 0.00;          // multiplier of elapsed real time to scale time
bool fc_restart          = false;         // restart fast clock service?
bool startRelay          = true;          // restart relay service?
uint8_t defaultWifiMode  = WIFIBOTH;      // default wifi mode
#else
uint8_t defaultWifiMode  = WIFIBOTH;      // default wifi mode - Optionally change to WIFISTA as non-relay default
#endif
#ifdef WEBLIFETIME
WiFiServer *webServer;                    // the web server wifi service
uint16_t webPort         = WEBPORT;       // tcp/ip web server port
int8_t webServerCount    = 0;             // sever serial number
int8_t webClientCount    = 0;             // count of open connection
int8_t maxWebClientCount = 0;             // high water mark
char webCredential[64] = { "" };          // http basic auth string
bool webIsRunning      = true;            // running state, used to set termination on inactivity
bool startWeb          = true;            // restart web server
#endif
char ssid[SSIDLENGTH];            // SSID connected to in STA mode
char tname[SSIDLENGTH];           // name of this throttle/relay
char remServerType[10] = { "" };  // eg: JMRI
char remServerDesc[64] = { "" };  // eg: JMRI My whizzbang server v 1.0.4
char remServerNode[32] = { "" };  // eg: 192.168.6.1
char lastMessage[40]   = { "" };  // eg: JMRI: address 'L23' not allowed as Long
char dccLCD[4][21];               // DCC-Ex LCD messages
char diagMonitorMode   = ' ';     // mode of diag monitor
bool configHasChanged  = false;
bool showPackets       = false;   // debug setting: [no]showpackets
bool showKeepAlive     = false;   // debug setting: [no]showkeepalive
bool showKeypad        = false;   // debug setting: [no]showkeypad
bool showWebHeaders    = false;   // debug setting: [no]showweb
bool trackPower        = false;   // track power status on/off
bool refreshDisplay    = true;    // display requires refresh in loco driving mode
bool drivingLoco       = false;   // Are we driving anything
bool initialDataSent   = false;   // Has a request been sent to CS for initial data
bool bidirectionalMode = false;   // Are we running in bidirectional mode?
bool menuMode          = false;   // in menu mode - screen savable mode, differrent keypad maps
bool funcChange        = true;    // in locomotive driving mode, have functions changed?
bool speedChange       = false;   // in locomotive driving mode, has speed changed?
bool netReceiveOK      = false;
bool diagReceiveOK     = false;   // flag to ensure only one cpy is running
bool diagIsRunning     = false;   // run state indicator
bool APrunning         = false;   // are we running as an access point?
WiFiServer *diagServer = NULL;    // the diagnostic server wifi service
#ifdef POTTHROTPIN
bool enablePot         = true;    // potentiometer enable/disable while driving
#endif
#ifdef SCREENSAVER
bool inScreenSaver     = false;   // Has backlight been turned off?
#endif
#ifdef FILESUPPORT
fs::File writeFile;
bool writingFile       = false;
const  char* rootCACertificate= NULL;
#ifdef CERTFILE
const  char* defaultCertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAw\n" \
"WhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n" \
"RW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n" \
"AoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cP\n" \
"R5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdx\n" \
"sxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8Zutm\n" \
"NHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxg\n" \
"Z3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG\n" \
"/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMC\n" \
"AYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYB\n" \
"Af8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaA\n" \
"FHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcw\n" \
"AoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRw\n" \
"Oi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQB\n" \
"gt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6W\n" \
"PTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wl\n" \
"ikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQz\n" \
"CkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BIm\n" \
"lJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4\n" \
"avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2\n" \
"yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1O\n" \
"yK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90Ids\n" \
"hCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+\n" \
"HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6Zv\n" \
"MldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqX\n" \
"nLRbwHOoq7hHwg==\n" \
"-----END CERTIFICATE-----\n" ;
#endif  //  CERTFILE
#ifdef WEBLIFETIME
const char *cssTemplate = {"* { font-family: system-ui; }\n" \
"html { text-align: left; }\n" \
"body { max-width: 800px; margin: 5px; background-color: #FFFFEE; }\n" \
"hr { border-top: 1px dashed blue; }\n" \
"pre { background-color: #000000; color: #88FF88; }\n" \
"table { border 0; }\n" \
"tr:nth-child(even) { background-color: #DDDDDD; }\n" \
"h1 { color: #0000DD; }\n" \
"h2, h3, h4, th { background-color: #0000DD; color: #FFFFFF; }\n" \
"textarea { font-family: \"Lucida Console\", \"Courier New\", monospace; }\n" \
"a:link { color: #0000DD !important; outline: none !important; }\n" \
"a:visited { color: #0000DD !important; }\n" \
"a:hover { background-color: #0000DD !important; color: #FFFFFF !important; text-decoration: none; }\n" \
"a:active { color: #0000DD !important; }\n" \
".Active, .active, .thrown, .Thrown { background-color: #CCFFCC !important; }\n" \
".Inactive, .inactive, .closed, .Closed { background-color: #CCCCFF !important; }\n" \
".unknown, .Unknown .Inconsistent { background-color: #FFCCCC !important; }\n" \
".failed, .Failed { background-color: #CC0000 !important; color: #FFFFFF !important; }\n" \
".speed, .On { background-color: #008800 !important; padding: 0px; border-spacing: 0px; }\n" \
".space { background-color: #FFFFEE !important; padding: 0px; border-spacing: 0px; }\n" \
".Off { background-color: #DD0000 !important; padding: 0px; border-spacing: 0px; }\n" \
};
#endif  //  WEBLIFETIME

const char *sampleConfig = {"# Sample file for adding definitions to miniThrottle\n" \
"#\nTypically this may be used to define a locomotive roster and turnouts for\n" \
"# DCC-Ex or set configuration parameters which can be set at the console.\n\n" \
"# Comment lines start with a \"#\"\n" \
"# Suggestions:\n" \
"#  1. be consistent with names starting with either Capitals or lowercase.\n" \
"#  2. max route length is 20 turnouts per route\n" \
"#\n" \
"# add loco <dcc-address> <description>\n" \
"# add turnout <name> {DCC|SERVO|VPIN} <dcc-definition>\n" \
"# add route <name> {<turnout-name> <state>} {<turnout-name> <state>}...\n" \
"#    Add a locomotive, turnout or route to DCC-Ex roster\n" \
"#    where for turnouts, definitions may be:\n" \
"#       DCC <linear-address>\n" \
"#       DCC <address> <sub-address>\n" \
"#       SERVO <pin> <throw-posn> <closed-posn> <profile>\n" \
"#       VPIN <pin-number>\n" \
"#    Servo profile: 0=immediate, 1=0.5 secs, 2=1 sec, 3=2 secs, 4=bounce\n" \
"#    And where for routes, turn-out state is one of:\n" \
"#       C - Closed\n" \
"#       T - Thrown\n" \
"#\n" \
"# del {loco|turnout|route} <dcc-address|name>\n" \
"#    Delete a locomotive or turnout from DCC-Ex roster\n" \
"#\n" \
"add loco 3 factory default loco\n" \
"add turnout riverRd01 SERVO 101 132 198 2\n" \
"add turnout riverRd02 SERVO 102 132 198 2\n" \
"add route platform-1 riverRd01 T riverRd02 T\n" \
"add route platform-2 riverRd01 C riverRd02 C\n" \
"del loco 7\n" \
"cpuspeed 240\n" \
"name trainCtrl\n" \
"wifi 3 BrocolliSoup PepperCorns123\n" \
"restart\n" \
};
const char *sampleCommand = { "# Sample file showing running of a command to start diag port\n" \
"diag\n\n" \
};
#endif  //  FILESUPPORT

const char prevMenuOption[] = { "Prev. Menu"};
const char *protoList[]     = { "Undefined", "WiThrottle", "DCC-Ex" };
#ifdef RELAYPORT
const char *relayTypeList[] = { "None", "WiThrottle", "DCC-Ex"};
#endif

// selected font sizes being:
uint8_t selFontWidth  = 8;
uint8_t selFontHeight = 16;

const char txtWarning[] = { "Warning" };

void print_reset_reason(uint8_t n, RESET_REASON reason);


/*
 * Set up steps
 * * Initialise console connection
 * * Load basic config
 * * Start subtasks to handle I/O
 */
void setup()  {
  esp_chip_info_t chip_info;
  //  printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
  //          chip_info.cores,
  //          (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
  //          (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
  Serial.begin(115200);
  for (uint8_t n=0; n<MAXCONSISTSIZE; n++) {
    strcpy (locoRoster[n].name, "Void");
    locoRoster[n].owned    = false;
    locoRoster[n].relayIdx = 255;
  }
  // load initial settings from Non Volatile Storage (NVS)
  nvs_init();
  nvs_get_string ("tname", tname, NAME, sizeof(tname));
  // Print a diagnostic to the console, Prior to starting tasks no semaphore required
  esp_chip_info(&chip_info);
  coreCount = chip_info.cores;
  Serial.printf  ("\r\n----------------------------------------\r\n");
  Serial.printf  ("Hardware Vers: %d core ESP32 (rev %d) %dMHz, Xtal: %dMHz, %d MB %s flash\r\n", \
     chip_info.cores, \
     ESP.getChipRevision(), \
     ESP.getCpuFreqMHz(), \
     getXtalFrequencyMhz(), \
     spi_flash_get_chip_size() / (1024 * 1024), \
     (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
  // Optional define in miniThrottle.h - not everyone cares about BogoMips
  // do test before starting too many other threads, ideally we have a core to ourselves.
  Serial.printf  ("Software Vers: %s %s\r\n", PRODUCTNAME, VERSION);
  Serial.printf  (" Compile Time: %s %s\r\n", __DATE__, __TIME__);
  #ifndef NODISPLAY
  Serial.printf  (" Display Type: %s\r\n", DISPLAYNAME);
  #endif   //  NODISPLAY
  Serial.printf  ("Throttle Name: %s\r\n", tname);
  // Optional define in miniThrottle.h, not critical to check partitions on each reboot
  #ifdef SHOWPARTITIONS
  Serial.printf  ("----------------------------------------\r\n\r\n");
  for (uint8_t n=0; n<coreCount; n++) print_reset_reason(n, rtc_get_reset_reason(n));
  Serial.printf  ("----------------------------------------\r\n\r\n");
  printf("ESP32 Partition table:\r\n");
  Serial.printf("+------+-----+----------+----------+------------------+\r\n");
  Serial.printf("| Type | Sub |  Offset  |   Size   |       Label      |\r\n");
  Serial.printf("| ---- | --- | -------- | -------- | ---------------- |\r\n");
  for (uint8_t part_type=0 ; part_type<2; part_type++) {
    const char descr[][4] = {"APP", "DAT"};
    const esp_partition_type_t ptype[] = { ESP_PARTITION_TYPE_APP, ESP_PARTITION_TYPE_DATA };
    // esp_partition_iterator_t pi = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    esp_partition_iterator_t pi = esp_partition_find (ptype[part_type], ESP_PARTITION_SUBTYPE_ANY, NULL);
    // esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (pi != NULL) {
      do {
        const esp_partition_t* p = esp_partition_get(pi);
        Serial.printf("| %s  | %02x  | 0x%06X | 0x%06X | %-16s |\r\n", 
          descr[part_type], p->subtype, p->address, p->size, p->label);
      } while (pi = (esp_partition_next(pi)));
    }
  }
  Serial.printf("+------+-----+----------+----------+------------------+\r\n");
  #endif
  debounceTime = nvs_get_int ("debounceTime", DEBOUNCEMS);
  screenRotate = nvs_get_int ("screenRotate", 0);
  if (nvs_get_int ("bidirectional", 0) == 1) bidirectionalMode = true;
  #ifdef SHOWPACKETSONSTART
  showPackets = true;
  #endif    //  SHOWPACKETSONSTART
  // Also change CPU speed before starting wireless comms
  #ifndef NOCPUSPEED
  int cpuSpeed = nvs_get_int ("cpuspeed", 0);
  if (cpuSpeed > 0) {
    #ifdef USEWIFI   //  USEWIFI
    if (cpuSpeed < 80) cpuSpeed = 80; 
    #endif
    Serial.printf ("%s Setting CPU speed to %d MHz\r\n", getTimeStamp(), cpuSpeed);
    setCpuFrequencyMhz (cpuSpeed);
  }
  #endif
  if (showPinConfig()) Serial.printf ("%s Basic hardware check passed\r\n", getTimeStamp());
  else {
    Serial.printf ("%s Basic hardware check failed\r\n", getTimeStamp());
    Serial.printf ("%s Reconfigure and recompile required to proceed\r\n", getTimeStamp());
    Serial.printf ("%s System initialisation aborted\r\n", getTimeStamp());
    while (true) delay (10000);
  }
  debuglevel      = nvs_get_int ("debuglevel",    DEBUGLEVEL);
  dccPowerFunc    = nvs_get_int ("dccPower",      DCCPOWER);
  defaultLatchVal = nvs_get_int ("FLatchDefault", FUNCTLATCH);
  defaultLeadVal  = nvs_get_int ("FLeadDefault",  FUNCTLEADONLY);
  inventoryLoco   = nvs_get_int ("inventoryLoco", LOCALINV);
  inventoryTurn   = nvs_get_int ("inventoryTurn", LOCALINV);
  inventoryRout   = nvs_get_int ("inventoryRout", LOCALINV);
  for (uint8_t n=0;n<4;n++) dccLCD[n][0] = '\0'; // store empty string in dccLCD array
  #ifdef RELAYPORT
  relayPort = nvs_get_int ("relayPort", RELAYPORT);
  relayServer = new WiFiServer(relayPort);
  maxRelay = nvs_get_int ("maxRelay", MAXRELAY);
  if (maxRelay>MAXRELAY)  maxRelay = MAXRELAY;  // MAXRELAY is the absolute max number of connections we want to support
  relayMode = nvs_get_int ("relayMode", WITHROTRELAY);
  maxRelayTimeOut =  ((nvs_get_int ("relayKeepAlive", KEEPALIVETIMEOUT) * 2) + 1) * uS_TO_S_FACTOR;
  if (debuglevel>0 && xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    const char *relType[] = { "None", "WiThrottle", "DCC-Ex" };
    Serial.printf ("%s Relay type is: %s, port %d, max clients %d\r\n", getTimeStamp(), relType[relayMode], relayPort, maxRelay);
    xSemaphoreGive(consoleSem);
  }
  #endif    //  RELAYPORT
  // Configure I/O pins
  // Track power indicator
  #ifdef TRACKPWR
  pinMode(TRACKPWR, OUTPUT);
  digitalWrite(TRACKPWR, LOW);
  #else
  #ifdef TRACKPWRINV
  pinMode(TRACKPWRINV, OUTPUT);
  digitalWrite(TRACKPWRINV, HIGH);
  #endif   //  TRACKPWRINV
  #endif   //  TRACKPWR
  // function key indicators off
  #ifdef F1LED
  pinMode(F1LED, OUTPUT);
  digitalWrite(F1LED, LOW);
  #endif   //  F1LED
  #ifdef F2LED
  pinMode(F2LED, OUTPUT);
  digitalWrite(F2LED, LOW);
  #endif   //  F2LED
  // trainset mode indicator
  #ifdef TRAINSETLED
  pinMode(TRAINSETLED, OUTPUT);
  if (bidirectionalMode) digitalWrite(TRAINSETLED, HIGH);
  else digitalWrite(TRAINSETLED, LOW);
  #endif   // TRAINSETLED
  // Read a backlight reference ADC pin and set backlight PWM using this
  #ifdef BACKLIGHTPIN
  pinMode(BACKLIGHTPIN, OUTPUT);
  #ifdef BACKLIGHTREF
  analogReadResolution(10);
  adcAttachPin(BACKLIGHTREF);
  analogSetPinAttenuation(BACKLIGHTREF, ADC_11db);  // param 2 = attenuation, range 0-3 sets FSD: 0:ADC_0db=800mV, 1:ADC_2_5db=1.1V, 2:ADC_6db=1.35V, 3:ADC_11db=2.6V
  backlightValue = (analogRead(BACKLIGHTREF))>>2;
  #else
  // digitalWrite (BACKLIGHTPIN, 1);
  backlightValue = nvs_get_int ("backlightValue", 200);
  #endif    //  BACKLIGHTREF
  ledcSetup(0, 5000, 8);
  ledcAttachPin(BACKLIGHTPIN, 0);
  ledcWrite(0, backlightValue);
  // analogWrite(BACKLIGHTPIN, backlightValue);
  #endif    //  BACKIGHTPIN
  // Set speedometer initial position
  #ifdef SPEEDOPIN
  dacWrite (SPEEDOPIN, 0);
  #endif   //  SPEEDOPIN
  // Set brake initial position
  #ifdef BRAKEPRESPIN
  dacWrite (BRAKEPRESPIN, 0);
  #endif   //  BRAKEPRESPIN

  // Check filesystem for config, cert and icon storage
  #ifdef FILESUPPORT
  Serial.printf ("%s Attaching SPIFFS filesystem - may take several minutes if formatting is required.\r\n", getTimeStamp());
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    Serial.printf ("%s SPIFFS filesystem required formatted.\r\n", getTimeStamp());
    delay (1500);
  }
  sampleConfigExists(SPIFFS);
  #ifdef CERTFILE
  defaultCertExists(SPIFFS);
  #endif   //  CERTFILE
  #ifdef WEBLIFETIME
  webPort = nvs_get_int ("webPort", WEBPORT);
  webServer = new WiFiServer(webPort);
  defaultCssFileExists(SPIFFS);
  #endif   //  WEBLIFETIME
  #endif   //  FILESUPPORT
  // Use tasks to process various input and output streams
  // micro controller has enough memory, that stack sizes can be generously allocated to avoid stack overflows
  xTaskCreate(serialConsole, "serialConsole", 8192, NULL, 4, NULL);
  #ifdef SERIALPORT
  cmdProtocol = DCCEX;    // expect it always to be this!
  #else
  cmdProtocol = nvs_get_int ("defaultProto", WITHROT);
  #endif  //  SERIALPORT
  #ifdef DELAYONSTART
  {
    uint16_t delayOnStart = nvs_get_int ("delayOnStart", DELAYONSTART);
    if (delayOnStart>120) delayOnStart = 120;
    if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
      Serial.printf ("%s Delay %ds before starting network services and initialising display\r\n", getTimeStamp(), delayOnStart);
      xSemaphoreGive(consoleSem);
    }
    delayOnStart *= 1000;
    delay (delayOnStart);  // console started but no network services before the delay
  }
  #endif   // DELAYONSTART
  #ifdef USEWIFI
  if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s Starting WiFi network services\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  xTaskCreate(connectionManager, "connectionMgr", 4096, NULL, 4, NULL);
  #ifndef SERIALCTRL
  // Only used if connection to controlstation is over WiFi
  xTaskCreate(keepAlive, "keepAlive", 2048, NULL, 4, NULL);
  #endif   //  SERIALCTRL
  #endif   //  USEWIFI
  #ifdef SERIALCTRL
  if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s Starting connection to serially connected DCC-Ex\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  xTaskCreate(serialConnectionManager, "serialCntMgr", 4096, NULL, 4, NULL);
  #ifdef RELAYPORT
  #ifdef USEWIFI
  // if (relayMode == WITHROTRELAY) {
    if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
      Serial.printf ("%s Starting fast clock server\r\n", getTimeStamp());
      xSemaphoreGive(consoleSem);
    }
    xTaskCreate(fastClock, "fastClock", 2048, NULL, 4, NULL);
  // }
  #endif  //  USEWIFI
  #endif  //  RELAYPORT
  #endif  //  SERIALCTRL
  #ifndef NODISPLAY
  #ifdef SCREENSAVER
  if (xSemaphoreTake(screenSvrSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    blankingTime  = nvs_get_int ("screenSaver", SCREENSAVER) * 60 * uS_TO_S_FACTOR;
    screenActTime = esp_timer_get_time();
    xSemaphoreGive(screenSvrSem);
    if (debuglevel>1 && xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
      Serial.printf ("SCREEN: Blanking time %d S\r\n", (nvs_get_int ("screenSaver", SCREENSAVER) * 60));
      xSemaphoreGive(consoleSem);
    }
  }
  else semFailed ("screenSvrSem", __FILE__, __LINE__);
  #endif   // SCREENSAVER
  #ifdef keynone
  if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s No keypad defined\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  #else
  xTaskCreate(keypadMonitor, "keypadMonitor", 2048, NULL, 4, NULL);
  #endif   // keynone
  xTaskCreate(switchMonitor, "switchMonitor", 2048, NULL, 4, NULL);
  #endif   // NODISPLAY
  //if (nvs_get_int("diagPortEnable", 0) == 1) {
  //  startDiagPort();
  //}
}

/*
 * Main loop is used to run display
 * To avoid a corrupted display only update the display from a single thread
 * Try to avoid writing over the edge of the screen.
 */
void loop()
{
  #ifndef NODISPLAY
  uint8_t menuId = 0;
  uint8_t change = 0;
  uint8_t answer;
  uint8_t menuResponse[] = {1,2,3,4,5,6};
  char commandKey;
  char commandStr[2];
  const char *baseMenu[]  = { "Locomotives", "Turnouts", "Routes", "Track Power", "CV Programming", "Configuration" };
  const char txtNoPower[] = { "Track power off. Turn power on to enable function." };
  #endif   // NODISPLAY

  if (debuglevel>2 && xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s loop()\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  delay (250);
  #ifdef NODISPLAY
  if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s No display device\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  #else
  if (xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s Start device display (", getTimeStamp());
    #ifdef DISPLAYNAME
    Serial.print (DISPLAYNAME);
    Serial.print (", ");
    #endif
    #ifdef SCREENROTATE
    Serial.print (SCREENROTATE);
    Serial.print (" way rotatable, ");
    #endif
    #ifdef SCALEFONT
    Serial.print ("scalable speed indic, ");
    #endif
    #ifdef COLORDISPLAY
    Serial.println ("color)");
    #else
    Serial.println ("monochrome)");
    #endif
    xSemaphoreGive(consoleSem);
  }
  display.begin();
  setupFonts();

  while (xQueueReceive(keyboardQueue, &commandKey, pdMS_TO_TICKS(debounceTime)) == pdPASS) {} // clear keyboard buffer
  // No keypad or encoder => not locally controllable, so no menu just show info
  #ifndef ENCODE_UP
  #ifdef keynone
  while (true) {
    displayInfo();
    delay (20000);     // 20 second timeout
  }
  #endif
  #endif
  // Normal control
  while (true) {
    #ifndef SERIALCTRL
    if (!client.connected()) {
      uint8_t answer;
      static bool stateChange   = true;
      static bool wifiConnected = false;
      static bool APConnected   = false;

      if ((!APrunning) && WiFi.status() != WL_CONNECTED && xQueueReceive(keyboardQueue, &commandKey, pdMS_TO_TICKS(debounceTime)) == pdPASS) {
        mkConfigMenu();
        stateChange = true;
      }
      if (((!APrunning) && WiFi.status() != WL_CONNECTED) || (!client.connected())) {
        if (wifiConnected && WiFi.status() != WL_CONNECTED) {
          wifiConnected = false;
          stateChange   = true;
        }
        else if ((!wifiConnected) && WiFi.status() == WL_CONNECTED) {
          wifiConnected = true;
          stateChange   = true;
        }
        if (APrunning && !APConnected) { //Access-point just started?
          APConnected = true;
          stateChange = true;
        }
        if (stateChange) {  // Update display if there is something to update
          uint8_t lineNr = 0;
          char outData[SSIDLENGTH + 10];
          stateChange = false;
          display.clear();
          displayScreenLine ("No Connection", lineNr++, true);
          sprintf (outData, "Name: %s", tname);
          displayScreenLine (outData, lineNr++, false);
          if ((!APrunning) && WiFi.status() == WL_CONNECTED) {
            if (xSemaphoreTake(tcpipSem, pdMS_TO_TICKS(TIMEOUT*10)) == pdTRUE) {
              uint8_t waitLoop = 0;
              while (strlen (ssid) == 0 && waitLoop++ < 100) {
                xSemaphoreGive(tcpipSem);
                delay (100);
                xSemaphoreTake(tcpipSem, pdMS_TO_TICKS(TIMEOUT*10));
              }
              xSemaphoreGive(tcpipSem);
            }
            else semFailed ("tcpipSem", __FILE__, __LINE__);
          }
          if (APrunning || WiFi.status() == WL_CONNECTED) {
            if (APrunning) {
              displayScreenLine ("WiFi: AP Mode", lineNr++, false);
            }
            else if (WiFi.status() == WL_CONNECTED && strlen(ssid)>0) {
              sprintf (outData, "WiFi: %s", ssid);
              displayScreenLine (outData, lineNr++, false);
            }
            displayScreenLine ("Svr:  Connecting", lineNr++, false);
          }
          else {
            displayScreenLine ("Wifi: Connecting", lineNr++, false);
          }
        }
      }
    }
    else {
      while (client.connected()) {
    #else
    // handling for Serial connection
    if (true) {
      while (true) { // Assume serial => always connected
    #endif
        if (trackPower) {
          menuResponse[0] = 1;
          if (turnoutCount == 0) menuResponse[1] = 200;
          else menuResponse[1] = 2;
          if (routeCount == 0)   menuResponse[2] = 200;
          else menuResponse[2] = 3;
          if (cmdProtocol == WITHROT) {
            menuResponse[4] = 200;
            if (lastMainMenuOption == 4) lastMainMenuOption = 0;
          }
          else menuResponse[4] = 5;
        }
        else {
          menuResponse[0] = 200;
          if (nvs_get_int("noPwrTurnouts", 0) == 0) menuResponse[1] = 200;
          if (nvs_get_int("noPwrTurnouts", 0) == 0) menuResponse[2] = 200;
          menuResponse[4] = 200;
          if (lastMainMenuOption != 3 && lastMainMenuOption != 5) lastMainMenuOption = 3;
        }
        answer = displayMenu ((const char**)baseMenu, menuResponse, 6, lastMainMenuOption);
        if (answer > 0) lastMainMenuOption = answer - 1;
        switch (answer) {
          case 1:
            if (trackPower) mkLocoMenu ();
            else displayTempMessage ((char*)txtWarning, (char*)txtNoPower, true);
            break;
          case 2:
            if (trackPower || nvs_get_int("noPwrTurnouts", 0) == 1) mkTurnoutMenu ();
            else displayTempMessage ((char*)txtWarning, (char*)txtNoPower, true);
            break;
          case 3:
            if (trackPower || nvs_get_int("noPwrTurnouts", 0) == 1) mkRouteMenu();
            else displayTempMessage ((char*)txtWarning, (char*)txtNoPower, true);
            break;
          case 4:
            mkPowerMenu();
            break;
          case 5:
            if (trackPower) mkCVMenu ();
            else displayTempMessage ((char*)txtWarning, (char*)txtNoPower, true);
            break;
          case 6:
            mkConfigMenu();
            break;
        }
      }
    }
    delay (debounceTime);
  }
  #endif   //  NODISPLAY
  if (debuglevel>2 && xSemaphoreTake(consoleSem, pdMS_TO_TICKS(TIMEOUT)) == pdTRUE) {
    Serial.printf ("%s Terminate loop()\r\n", getTimeStamp());
    xSemaphoreGive(consoleSem);
  }
  vTaskDelete( NULL );
}



void print_reset_reason(uint8_t n, RESET_REASON reason)
{
  if (reason>0 && reason<17) {
    Serial.printf ("%s CPU %d reset reason: ", getTimeStamp(), n);
    switch ( reason)
    {
      case 1  : Serial.printf ("Vbat power on reset");break;
      case 3  : Serial.printf ("Software reset digital core");break;
      case 4  : Serial.printf ("Legacy watch dog reset digital core");break;
      case 5  : Serial.printf ("Deep Sleep reset digital core");break;
      case 6  : Serial.printf ("Reset by SLC module, reset digital core");break;
      case 7  : Serial.printf ("Timer Group0 Watch dog reset digital core");break;
      case 8  : Serial.printf ("Timer Group1 Watch dog reset digital core");break;
      case 9  : Serial.printf ("RTC Watch dog Reset digital core");break;
      case 10 : Serial.printf ("Instrusion tested to reset CPU");break;
      case 11 : Serial.printf ("Time Group reset CPU");break;
      case 12 : Serial.printf ("Software reset CPU");break;
      case 13 : Serial.printf ("RTC Watch dog Reset CPU");break;
      case 14 : Serial.printf ("for APP CPU, reset by PRO CPU");break;
      case 15 : Serial.printf ("Reset when the vdd voltage is not stable");break;
      case 16 : Serial.printf ("RTC Watch dog reset digital core and rtc module");break;
      default : Serial.printf ("NO_MEAN");
    }
    Serial.printf ("\r\n");
  }
}
