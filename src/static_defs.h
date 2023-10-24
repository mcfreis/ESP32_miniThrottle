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

#pragma once

/*
 * Use this file to centralise any defines and includes
 * This file should contain non-customisable definitions
 */
// #include <stdio.h>
// #include <time.h>
// Used for user interfaces
#ifndef NODISPLAY
#include "lcdgfx.h"
#include <Keypad.h>
#include <ESP32Encoder.h>
#endif
// Used for hardware inspection
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <rom/rtc.h>
#include <esp_timer.h>
// Used for process / thread control
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
// Used for NVS access and query
#include <Preferences.h>
#include <esp_partition.h>
#include <pgmspace.h>

// Project identity and version
#ifdef PRODUCTNAME
#undef PRODUCTNAME
#endif
#ifdef VERSION
#undef VERSION
#endif
#define PRODUCTNAME "MiniThrottle" // Branding name
#define VERSION     "0.7"          // Version string

// Use either WiFi or define additional pins for second serial port to connect directly to DCC-Ex (WiFi free)
// It is expected most users will want to use miniThrottle as a WiFi device.
// Comment out "#define USEWIFI" to use direct serial connection to DCC-Ex
#ifdef USEWIFI
#include <WiFi.h>
// #include <WiFiMulti.h>
#include <ESPmDNS.h>
#endif
// Geek feature prereqs
#ifdef SHOWPARTITIONS
#include <esp_partition.h>
#endif
// Sanity check of BACKLIGHTPIN - not available on some display types
#ifdef BACKLIGHTPIN
#ifdef SSD1306
#undef BACKLIGHTPIN
#endif
#ifdef SSD1327
#undef BACKLIGHTPIN
#endif
#endif

// NB: OTA update may have limited applicability
// Name of default certificate file used for Over The Air Update using https, say for a club standard build from the club web server
#ifdef OTAUPDATE
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#ifndef FILESUPPORT
#define FILESUPPORT
#endif
#endif

// Should a web interface start and how long should it be active for?
// Comment out => no web server, 0 => always running or lifetime in minutes
//#define WEBLIFETIME   0
#ifdef WEBLIFETIME
//extern "C" {
//#include "crypto/base64.h"
#include "mbedtls/base64.h"
//}
#define CSSFILE "/miniThrottle.css"
#ifndef FILESUPPORT
#define FILESUPPORT
#endif
#endif

// Local filesystem can potentially be used to store https certificates, command sequences or icons/images
#ifdef FILESUPPORT
#define DEFAULTCONF "/sampleConfig.cfg"
#define DEFAULTCOMMAND "/sampleCommand.cfg"
#ifdef USEWIFI
#define CERTFILE "/rootCACertificate"
#ifndef NOHTTPCLIENT
#include <HTTPClient.h>
#endif
#endif
#include <FS.h>
#include <SPIFFS.h>
#define WEB_BUFFER_SIZE 4098
#define FORMAT_SPIFFS_IF_FAILED true   // format unformatted partition
#endif


/* ****************************************************************************
 * 
 * Beyond this point there probably isn't anything too interesting as far as
 * application configuration goes.
 * 
 **************************************************************************** */

// Define application things
#define MAXFUNCTIONS   30   // only expect 0-29 supported
#define MAXCONSISTSIZE  8   // max number of locomotives to drive in one session
#define NAMELENGTH     32   // Length of names for locos, turnouts, routes etc
#define SYSNAMELENGTH  16   // Length of system names for locos, turnouts, routes etc
#define SSIDLENGTH     33   // Length to permit SSIDs and passwords
#define SORTDATA        1   // 0 = unsorted, 1 = sorted lists of data
#define MAXRTSTEPS     25   // Max number of steps / waypoints in a route.
#define MAXPARAMS  (MAXRTSTEPS*2)+3   // Maximum number of parameters a serial console comand can have, 43 => route of up to 20 switches

// Define Network and buffer sizes
#define WIFINETS        6   // Count of number network credentials to store
#define BUFFSIZE      250   // Keyboard buffer size
#define NETWBUFFSIZE 1400   // Network packet size
#define WITHROTMAXFIELDS  64   // Max fields per WiThrottle received packet
#define MAXSUBTOKEN     4   // Max sub tokens per WiThrottle field
#define DEBOUNCEMS     33   // millis to wait for switch / keypad debounce
#define INITWAIT        5   // seconds to wait for first packet from server to ID protocol
#define BUMPCOUNT     100   // re-evalute loco count every N times through loco control routine
#define TIMEOUT      2000   // Network timeout in milliseconds
#define APCHANNEL       6   // Default WiFi Channel for AP mode
#define MAXAPCLIENTS    8   // Max number of access point clients
#define DEFAPCLIENTS    4   // Default number of access point clients allowed
#define MAXDIAGCONNECT  2   // Maximum number of concurrent diagnotic clients

// Divisor for converting uSeconds to Seconds
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

// DCC-Ex Values
#define CALLBACKNUM 10812
#define CALLBACKSUB 22112

// Function Mapping
// Option one from WiThrottle Doc, Option two from DigiTrax SDN144PS
//#define FUNCTNAMES "Headlight~Bell~Whistle~Short Whistle~Steam Release~FX5 Light~FX6 Light~Dimmer~Mute~Water Stop~Injectors~Brake Squeal~Coupler~~~~~~~~~~~~~~~~"
#define FUNCTNAMES "Lights~Bell~Horn/Whistle~Coupler Crash~Air Feature~Dyn Brake Fans~Notch Up/Blow Down~Crossing Gate Horn~Mute~Brake Squeal~Air Horn Seq~Greaser~Safety Blow Off~~~~~~~~~~~~~~~~"
#define FUNCTLATCH 483
#define FUNCTLEADONLY 225

/*
 * **********  ENUMERATIONS  *********************************************************************
 */
// enumerations
enum directionInd { FORWARD = 0,   STOP = 1,     REVERSE = 2, UNCHANGED = 3 };
enum ctrlProtocol { UNDEFINED = 0, WITHROT = 1,  DCCEX = 2 };
enum varTypes     { STRING = 0,    PASSWORD = 1, INTEGER = 2 };
enum dccPower     { BOTH = 0,      MAINONLY = 1, PROGONLY = 2, JOIN = 3 };
enum dccIntegrate { LOCALINV = 0,  DCCINV = 1,   BOTHINV = 2 };
enum wifiModes    { WIFISTA = 1,   WIFIAP = 2,   WIFIBOTH = 3 };
#ifdef RELAYPORT
enum relayTypes   { NORELAY = 0,  WITHROTRELAY = 1, DCCRELAY = 2 };
#endif

/*
 * ***********  STRUCTURES  ***********************************************************************
 */
// Structures
struct locomotive_s {
  char *functionString;           // function description string for imported loco from DCC-Ex
  uint32_t function;              // Bit Array of enabled functions
  uint32_t functionLatch;         // set if latching function
  uint16_t id;                    // DCC address
  int16_t speed;                  // 128 steps, -1, 0-126
  uint8_t throttleNr;             // Throttle Number
  uint8_t steps;                  // Steps to use when updating speed
  uint8_t relayIdx;               // If relayed, which relay
  int8_t direction;               // FORWARD, STOP or REVERSE
  char steal;                     // Is a steal required?
  char type;                      // Long or short addr. 127 and below should be short, 128 and above should be long
  char name[NAMELENGTH];          // name of loco
  bool owned;                     // Is loco owned by this throttle?
};

struct turnoutState_s {
  uint8_t state;                  // numeric state value
  char name[SYSNAMELENGTH];       // human readible equivalent
};

struct turnout_s {
  uint8_t state;                  // numeric turnout state
  char sysName[SYSNAMELENGTH];    // DCC address / system name
  char userName[NAMELENGTH];      // human readible name
};

struct routeState_s {
  uint8_t state;                  // numeric state value
  char name[SYSNAMELENGTH];       // human readible equivalent
};

struct route_s {
  uint8_t state;                  // State of the route
  char sysName[SYSNAMELENGTH];    // System name of the route
  char userName[NAMELENGTH];      // User Name
  uint8_t turnoutNr[MAXRTSTEPS];  // turnout numbers
  uint8_t desiredSt[MAXRTSTEPS];  // desired state of turnout
};

#ifdef RELAYPORT
struct relayConnection_s {
  WiFiClient *client;             // Client data
  IPAddress address;              // remote IP
  uint64_t keepAlive;             // time of last packet
  uint32_t inPackets;             // count of packets in  - receive from remote side
  uint32_t outPackets;            // count of packets out - sent to remote side
  uint8_t  id;                    // serial number of this entry, used for reverse lookup
  char nodeName[NAMELENGTH];      // remote name
};
#endif

// structure for holding base information on many NVS values
struct nvsVar_s {
  char *varName;
  uint8_t varType;
  int16_t varMin; 
  uint16_t varMax;
  int16_t numDefault;
  char *strDefault;
  char *varDesc;
};

// structure for holding pin assignments
struct pinVar_s {
  int8_t pinNr;
  char *pinDesc;
};


/*
 * Required by Throttle functionality to get loco, turnout and route data, but also useful debug tool for inspecting NVS
 * Inspired by https://github.com/Edzelf/ESP32-Show_nvs_keys/blob/master/Show_nvs_keys.ino
 * Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
 */
struct nvs_entry
{
  uint8_t  Ns ;         // Namespace ID
  uint8_t  Type ;       // Type of value
  uint8_t  Span ;       // Number of entries used for this item
  uint8_t  Rvs ;        // Reserved, should be 0xFF
  uint32_t CRC ;        // CRC
  char     Key[16] ;    // Key in Ascii
  union {
    uint64_t sixFour;
    uint32_t threeTwo[2];
    uint16_t oneSix[4];
    uint8_t  eight[8];
  } Data ;       // Data in entry 
} ;

struct nvs_page                                     // For nvs entries
{                                                   // 1 page is 4096 bytes
  uint32_t  State ;
  uint32_t  Seqnr ;
  
  uint32_t  Unused[5] ;
  uint32_t  CRC ;
  uint8_t   Bitmap[32] ;
  nvs_entry Entry[126] ;
} ;

extern uint8_t nvs_index_ref[];
extern const size_t num_nvs_index_ref;
extern const char *nvs_descrip[];

#ifdef SSD1306
extern DisplaySSD1306_128x64_I2C display;
#endif
#ifdef SSD1327
extern DisplaySSD1327_128x128_I2C display;
#endif
#ifdef ST7735
extern DisplayST7735_128x160x16_SPI display;
#endif
#ifdef ST7789
extern DisplayST7789_135x240x16_SPI display;
#endif
#ifdef ILI9341
extern DisplayILI9341_240x320x16_SPI display;
#endif

#ifdef USEWIFI
extern WiFiClient client;
#endif
#ifdef SERIALCTRL
extern HardwareSerial serial_dev;
#endif
extern SemaphoreHandle_t nvsSem;  // only one thread to access nvs at a time
extern SemaphoreHandle_t serialSem;  // only one thread to access serial to dcc-ex at a time
extern SemaphoreHandle_t consoleSem;  // only aalow one message to be written console at a time
extern SemaphoreHandle_t velociSem;  // used for velocity / direction changes table lock of locomotives
extern SemaphoreHandle_t functionSem;  // used for setting functions - table lock of all functions
extern SemaphoreHandle_t turnoutSem;  // used for setting turnouts  - table lock of all turnouts
extern SemaphoreHandle_t routeSem ;  // used for setting routes    - table lock of all routes
// Normal TCP/IP operations work OK in single threaded environment, but benefits from semaphore protection in multi-threaded envs
// This does lead to kludges to keep operations such as available() outside if or while logic to maintain runtime stability.
extern SemaphoreHandle_t tcpipSem ;
extern SemaphoreHandle_t fastClockSem;  // for sending/receiving/displaying fc messages
extern SemaphoreHandle_t shmSem   ;  // shared memory for used for moved blocks of data between tasks/threads
#ifdef WEBLIFETIME
extern SemaphoreHandle_t webServerSem;  // used by different web server threads
#endif
#ifdef BACKLIGHTPIN
extern SemaphoreHandle_t screenSvrSem;  // used to coordinate screen/menu blanking
#endif
#ifdef RELAYPORT
extern SemaphoreHandle_t relaySvrSem;  // used by various relay threads to coordinate memory/dcc-ex access
#endif
#ifdef OTAUPDATE
#ifndef NOHTTPCLIENT
extern SemaphoreHandle_t otaSem   ;  // used to ensure only one ota activity at a time
extern HTTPClient *otaHttp;         // Client used for update
#endif
#endif
extern QueueHandle_t cvQueue;  // Queue for querying CV Values
extern QueueHandle_t keyboardQueue;     // Queue for keyboard type of events
extern QueueHandle_t keyReleaseQueue ;     // Queue for keyboard release type of events
extern QueueHandle_t dccAckQueue      ;  // Queue for dcc updates, avoid flooding of WiFi
extern QueueHandle_t dccLocoRefQueue  ;  // Queue for dcc locomotive speed and direction changes
extern QueueHandle_t dccTurnoutQueue  ;  // Queue for dcc turnout data
extern QueueHandle_t dccRouteQueue    ;  // Queue for dcc route data
extern QueueHandle_t dccOffsetQueue   ;  // Queue for dcc array offsets for setup of data
extern QueueHandle_t diagQueue;    // diagnostic data queue.
extern struct locomotive_s   *locoRoster;
extern struct turnoutState_s *turnoutState;  // table of turnout states
extern struct turnout_s      *turnoutList ;  // table of turnouts
extern struct routeState_s   *routeState  ;  // table of route states
extern struct route_s        *routeList   ;  // table of routes
extern char *sharedMemory;     // inter process communication shared memory buffer
#ifdef BACKLIGHTPIN
#ifdef SCREENSAVER
extern uint64_t screenActTime  ;   // time stamp of last user activity
extern uint64_t blankingTime   ;   // min time prior to blanking
#endif
extern uint16_t backlightValue; // backlight brightness 0-255
#endif
#ifndef NODISPLAY
extern int screenWidth      ;      // screen geometry in pixels
extern int screenHeight     ;
#endif
extern int keepAliveTime;     // WiThrottle keepalive time
#ifdef BRAKEPRESPIN
extern int brakePres        ;      // brake pressure register
#endif
extern uint32_t fc_time;         // in jmri mode we can receive fast clock, in relay mode we can send it, 36s past midnight => not updated
extern uint32_t defaultLatchVal     ;  // bit map of which functions latch
extern uint32_t defaultLeadVal      ;  // bit map of which functions are for lead loco only
extern const  uint16_t routeDelay[]; // selectable delay times when setting route in DCCEX mode
extern const size_t num_routeDelay;
extern uint16_t numberOfNetworks    ;  // count of networks detected in scan of networks
extern uint16_t initialLoco;  // lead loco register for in-throttle consists
extern uint16_t *dccExNumList      ;// List of numeric IDs returned by DCC-Ex when querying Ex-Rail
extern uint8_t dccExNumListSize     ;  // size of the above array
extern uint8_t locomotiveCount      ;  // count of defined locomotives
extern uint8_t turnoutCount         ;  // count of defined turnouts
extern uint8_t turnoutStateCount    ;  // count of defined turnout states
extern uint8_t routeCount           ;  // count of defined routes
extern uint8_t routeStateCount      ;  // count of defined route states
extern uint8_t lastMainMenuOption   ;  // track last selected option in main menu
extern uint8_t lastLocoMenuOption   ;  // track last selected option in locomotive menu
extern uint8_t lastSwitchMenuOption ;  // track last selected option in switch/turnout menu
extern uint8_t lastRouteMenuOption  ;  // track last selected option in route menu
extern uint8_t debuglevel;
extern uint8_t charsPerLine;              // using selected font the number chars across display
extern uint8_t linesPerScreen;            // using selected font the number lines on display
extern uint8_t debounceTime; // debounce time to allow on mechanical (electric) switches
extern uint8_t cmdProtocol;  // protocol to use when running this unit
extern uint8_t nextThrottle;        // use as throttle "number" in WiThrottle protocol
extern uint8_t screenRotate ;          // local display orientation
extern uint8_t dccPowerFunc;   // variant of power on to use for DCC power on
extern uint8_t coreCount;          // cpu core count, used for some diagnostics
extern uint8_t inventoryLoco ;  // default to local inventory for locos
extern uint8_t inventoryTurn ;  // default tp local inventory for turnouts
extern uint8_t inventoryRout ;  // default to local inventory for routes / automations
#ifdef RELAYPORT
extern  WiFiServer *relayServer;                  // the relay server wifi service
extern struct relayConnection_s *remoteSys;      // table of connected clients and assoc states
extern uint64_t maxRelayTimeOut;
extern uint32_t localinPkts     ;             // packet count over serial connection
extern uint32_t localoutPkts    ;             // packet count over serial connection
extern uint16_t relayPort ;     // tcp/ip relay listening port number
extern uint8_t maxRelay ;      // max number of relay clients
extern uint8_t relayMode;  // protocol to relay
extern uint8_t relayCount       ;
extern uint8_t relayClientCount ;
extern uint8_t maxRelayCount    ;             // high water mark
// When relaying we can also supply fast clock time
extern float fc_multiplier;          // multiplier of elapsed real time to scale time
extern bool fc_restart ;         // restart fast clock service?
extern bool startRelay;          // restart relay service?
extern uint8_t defaultWifiMode;      // default wifi mode
#else
extern uint8_t defaultWifiMode;      // default wifi mode - Optionally change to WIFISTA as non-relay default
#endif
#ifdef WEBLIFETIME
extern WiFiServer *webServer;                    // the web server wifi service
extern uint16_t webPort;       // tcp/ip web server port
extern int8_t webServerCount    ;             // sever serial number
extern int8_t webClientCount    ;             // count of open connection
extern int8_t maxWebClientCount ;             // high water mark
extern char webCredential[64];          // http basic auth string
extern bool webIsRunning;            // running state, used to set termination on inactivity
extern bool startWeb;            // restart web server
#endif
extern char ssid[SSIDLENGTH];            // SSID connected to in STA mode
extern char tname[SSIDLENGTH];           // name of this throttle/relay
extern char remServerType[10];  // eg: JMRI
extern char remServerDesc[64];  // eg: JMRI My whizzbang server v 1.0.4
extern char remServerNode[32];  // eg: 192.168.6.1
extern char lastMessage[40]  ;  // eg: JMRI: address 'L23' not allowed as Long
extern char dccLCD[4][21];               // DCC-Ex LCD messages
extern char diagMonitorMode;     // mode of diag monitor
extern bool configHasChanged ;
extern bool showPackets      ;   // debug setting: [no]showpackets
extern bool showKeepAlive    ;   // debug setting: [no]showkeepalive
extern bool showKeypad       ;   // debug setting: [no]showkeypad
extern bool showWebHeaders   ;   // debug setting: [no]showweb
extern bool trackPower       ;   // track power status on/off
extern bool refreshDisplay;    // display requires refresh in loco driving mode
extern bool drivingLoco      ;   // Are we driving anything
extern bool initialDataSent  ;   // Has a request been sent to CS for initial data
extern bool bidirectionalMode;   // Are we running in bidirectional mode?
extern bool menuMode         ;   // in menu mode - screen savable mode, differrent keypad maps
extern bool funcChange ;    // in locomotive driving mode, have functions changed?
extern bool speedChange      ;   // in locomotive driving mode, has speed changed?
extern bool netReceiveOK     ;
extern bool diagReceiveOK    ;   // flag to ensure only one cpy is running
extern bool diagIsRunning    ;   // run state indicator
extern bool APrunning        ;   // are we running as an access point?
extern WiFiServer *diagServer;    // the diagnostic server wifi service
#ifdef POTTHROTPIN
extern bool enablePot;    // potentiometer enable/disable while driving
#endif
#ifdef SCREENSAVER
extern bool inScreenSaver    ;   // Has backlight been turned off?
#endif

extern const char prevMenuOption[];
extern const char *protoList[];
#ifdef RELAYPORT
extern const char *relayTypeList[];
#endif

// selected font sizes being:
extern uint8_t selFontWidth;
extern uint8_t selFontHeight;

extern const char txtWarning[];

extern uint16_t wwwFontWidth ;
extern struct nvsVar_s nvsVars[];
extern const size_t num_nvsVars;
extern struct pinVar_s pinVars[];
extern const size_t num_pinVars;

extern const char *fontLabel[];