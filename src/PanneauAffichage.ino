#define VERSION "26.2.9-1"

// Charger les messages passés à la mise en route

/*
 *     English: Model timetable for train based on ESP8266 or ESP32
 *     Français : Panneau d'affichge pour train électrique à base d'ESP8266 ou d'ESP32
 *
 *  Available URL
 *      /           Root page
 *      /status     Returns status in JSON format
 *      /setup      Display setup page
 *      /command    Supports the following commands:
 *                      enable enter - disable enter
 *                      enable debug - disable debug
 *                      enable verbose - disable verbose
 *                      enable java - disable java
 *                      enable syslog - disable syslog
 *      /upload     Upload file (internal use only)
 *      /languages  Return list of supported languages
 *      /configs    Return list of configuration files
 *      /settings   Returns settings in JSON format
 *      /debug      Display internal variables to debug
 *      /log        Return saved log
 *      /edit       Manage and edit file system
 *      /tables     Display all program tables
 *      /changed    Change a variable value (internal use only)
 *      /rest       Execute API commands
 *          /restart
 *                      Restart ESP
 *
 *  URL disponibles
 *      /           Page d'accueil
 *      /status     Retourne l'état sous forme JSON
 *      /setup      Affiche la page de configuration
 *      /command    Supporte les commandes suivantes :
 *                      enable enter - disable enter
 *                      enable debug - disable debug
 *                      enable verbose - disable verbose
 *                      enable java - disable java
 *                      enable syslog - disable syslog
 *      /upload     Charge un fichier (utilisation interne)
 *      /languages  Retourne la liste des langues supportées
 *      /configs    Retourne la liste des fichiers de configuration
 *      /settings   Retourne la configuration au format JSON
 *      /debug      Affiche les variables internes pour déverminer
 *      /log        Retourne le log mémorisé
 *      /edit       Gère et édite le système de fichier
 *      /tables     Affiche l'ensemble des tables du programme
 *      /changed    Change la valeur d'une variable (utilisation interne)
 *      /rest       Exécute une commande de type API
 *          /restart
 *                      Redémarre l'ESP
 *
 *  Connections/Connexions
 *
 *      TFT	  ESP8266
 *      GND	    GND
 *      VCC	    3V3
 *      SCL	    D5
 *      SDA	    D7
 *      RES     D4
 *      DC		D2
 *      CS		D3
 *      BLK     3V3
 *
 *  Screen coordinates
 * 
 *      0,0             W,0
 *      +----------------+          +--> X/Width
 *      |                |          |
 *      |                |          v
 *      +----------------+          Y/Height
 *      0,H             W,H
 * 
 *  Display principles
 *      A table contains both values to display and previously displayed values
 *      When needed, code write values to display
 *      At regular interval, code scan this table and update changes on screen
 *
 *  Details
 *      We have a per row table, containing new and previous version of 
 *          Displayed text
 *          Text and background colors
 *          Flags
 *               Standard/timetable line (timetable lines have some black separators)
 *       An additional line is used to store message out of service text, colors and flags
 *       Another additional line is used to store out of service message text, colors and flags
 *       We update display when a change occurs in table
 *
 *  Flying Domotic - Novembre 2025
 *
 *  GNU GENERAL PUBLIC LICENSE - Version 3, 29 June 2007
 *
 */

#include <arduino.h>                                                // Arduino
#include <ArduinoJson.h>                                            // JSON documents

#ifdef ESP32
    #include <getChipId.h>                                          // ESP.getChipId emulation
    #if CONFIG_IDF_TARGET_ESP32
        #include "esp32/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32S2
        #include "esp32s2/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C2
        #include "esp32c2/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C3
        #include "esp32c3/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32S3
        #include "esp32s3/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C6
        #include "esp32c6/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32H2
        #include "esp32h2/rom/rtc.h"
    #else
        #error Target CONFIG_IDF_TARGET is not supported
    #endif
#endif

//  ---- Macros ----
#undef __FILENAME__                                                 // Deactivate standard macro, only supporting "/" as separator
#define __FILENAME__ (strrchr(__FILE__, '\\')? strrchr(__FILE__, '\\')+1 : (strrchr(__FILE__, '/')? strrchr(__FILE__, '/')+1 : __FILE__))

//          -------------------
//          ---- Variables ----
//          -------------------

//  ---- WiFi ----
#ifdef ESP32
    #include <WiFi.h>                                               // WiFi
#else
    #include <ESP8266WiFi.h>                                        // WiFi
#endif

//  ---- Log ----
#ifndef LOG_MAX_LINES
    #ifdef ESP32
        #define LOG_MAX_LINES 15
        #define LOG_LINE_LEN 100
    #endif
    #ifdef ESP8266
        #define LOG_MAX_LINES 5
        #define LOG_LINE_LEN 100
    #endif
#endif

char emptyChar[] = "";                                              // Empty string
char savedLogLines[LOG_MAX_LINES][LOG_LINE_LEN];                    // Buffer to save last log lines
uint16_t savedLogNextSlot = 0;                                      // Address of next slot to be written
uint16_t logRequestNextLog = 0;                                     // Address of next slot to be send for a /log request

//  ---- Syslog ----
#ifdef FF_TRACE_USE_SYSLOG
    #include <Syslog.h>                                             // Syslog client https://github.com/arcao/Syslog
    #include <WiFiUdp.h>
    WiFiUDP udpClient;
    Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
    unsigned long lastSyslogMessageMicro = 0;                       // Date of last syslog message (microseconds)
#endif

//  ---- Asynchronous web server ----
#ifdef ESP32
    #include <AsyncTCP.h>                                           // Asynchronous TCP
#else
    #include <ESPAsyncTCP.h>                                        // Asynchronous TCP
#endif
#include <ESPAsyncWebServer.h>                                      // Asynchronous web server
#include <LittleFS.h>                                               // Flash file system
#include <littleFsEditor.h>                                         // LittleFS file system editor
AsyncWebServer webServer(80);                                       // Web server on port 80
AsyncEventSource events("/events");                                 // Event root
String lastUploadedFile = emptyChar;                                // Name of last download file
int lastUploadStatus = 0;                                           // HTTP last error code

//  ---- Preferences ----
#define SETTINGS_FILE "/settings.json"

String ssid;                                                        // SSID of local network
String pwd;                                                         // Password of local network
String accessPointPwd;                                              // Access point password
#ifdef VERSION_FRANCAISE
    String espName = "Panneau";                                     // Name of this module
#else
    String espName = "Timetable";                                   // Name of this module
#endif
String hostName;                                                    // Host name
String serverLanguage;                                              // This server language
String syslogServer;                                                // Syslog server name or IP (can be empty)
String fileToStart;                                                 // Configuration file to start
uint16_t syslogPort;                                                // Syslog port (default to 514)
bool traceEnter = true;                                             // Trace routine enter?
bool traceDebug = true;                                             // Trace debug messages?
bool traceVerbose = false;                                          // Trace (super) verbose?
bool traceJava = false;                                             // Trace Java code?
bool traceSyslog = false;                                           // Send traces to syslog?
bool traceTable = false;                                            // Trace tables after loading?

uint16_t startTimeHour = 0;                                         // Simulation start hour
uint16_t startTimeMinute = 0;                                       //      and minute
uint16_t endTimeHour = 23;                                          // Simulation end hour
uint16_t endTimeMinute = 59;                                        //      and minute
float cycleTime = 10.0;                                             // Cycle duration in minutes

//  ---- Local to this program ----
String resetCause = emptyChar;                                      // Used to save reset cause
bool sendAnUpdateFlag = false;                                      // Should we send an update?
String wifiState = emptyChar;                                       // Wifi connection state

//  ---- Serial commands ----
#ifdef SERIAL_COMMANDS
    char serialCommand[100];                                        // Buffer to save serial commands
    size_t serialCommandLen = 0;                                    // Buffer used lenght
#endif

//  ---- Display ----
#include <Adafruit_ST7735.h>                                        // Adafruit ST7735 TFT library

#define TFT_RST D4                                                  // Screen reset pin
#define TFT_CS D3                                                   // Screen CS pin
#define TFT_DC D2                                                   // Screen DC PIN

#define LINE_CHARACTERS 26                                          // Count of characters on a line
#define PIXEL_HEIGHT 8                                              // Character height in pixels
#define PIXEL_WIDTH 6                                               // Character witdh in pixels
#define SCREEN_WIDTH 160                                            // Screen width in pixels
#define SCREEN_HEIGHT 80                                            // Screen height in pixels
#define MAX_LINES 9                                                 // Max number of screen lines
#define DETAIL_LINES_OFFSET 2                                       // Offset of first detail lines
#define MAX_DETAIL_LINES 7                                          // Max number of detail lines
#define TIME_OFFSET 0                                               // Time offset into message
#define TIME_LENGTH 5                                               // Time length into message
#define CITY_OFFSET 6                                               // City offset
#define CITY_LENGTH 13                                              // City length
#define TRACK_OFFSET 20                                             // Track offset
#define TRACK_LENGTH 1                                              // Track length
#define TRAIN_OFFSET 22                                             // Train number offset
#define TRAIN_LENGTH 4                                              // Train number length

// Define TFT storage
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define OUT_OF_SERVICE_MESSAGE (MAX_LINES)                        // Index of out of service messahe in screenLines
#define OUT_OF_SERVICE_TEXT (MAX_LINES+1)                             // Index of out of service text in screenLines
#define SCREEN_LINES (MAX_LINES+2)                                  // ScreenLine size

#ifdef VERSION_FRANCAISE
    #define DEFAULT_OUT_OF_SERVICE "HORS SERVICE"
#else
    #define DEFAULT_OUT_OF_SERVICE "OUT OF SERVICE"
#endif

// Screen flags
const uint8_t screenFlagNone = 0;                                   // No flags
const uint8_t screenFlagTimeTableLine = 1;                          // This is a timetable line (else standard)
const uint8_t screenFlagCentered = 2;                               // Line is centered (else set left)
const uint8_t screenFlagCenterBackground = 4;                       // Line is centered (else set left)
const uint8_t screenFlagDontTrace = 128;                            // Don't trace line change

// Screen storage
struct screenStorage_s {
    char* text;                                                     // Text to display
    uint16_t textColor;                                             // Text color
    uint16_t oddBackgroundColor;                                    // Odd lines background color
    uint16_t evenBackgroundColor;                                   // Even lines background color
    uint8_t flags;                                                  // Flags
};

int simulationTime = 0;                                             // Current simulation time
int simulationStart = 0;                                            // Simulation start time
int simulationStop = 0;                                             // Simulation start time
unsigned long minuteDuration = 0;                                   // Duration of one minute when in simulation
unsigned long displayLastRun = 0;                                   // Time of last display run
bool simulationActive = false;                                      // Simulation active flag
bool forceRefresh = true;                                           // Force screen refresh
screenStorage_s screenLines[SCREEN_LINES];                          // Current screen lines
screenStorage_s previousLines[SCREEN_LINES];                        // Previous screen lines
char currentTexts[SCREEN_LINES][LINE_CHARACTERS+1];                 // Buffer for current line text
char previousTexts[SCREEN_LINES][LINE_CHARACTERS+1];                // Buffer for previous line text
bool screenCleared = false;                                         // Screen just cleared

//  ---- Color names ----
struct colorNames_s {
    char* color;                                                    // Pointer to color name
    uint16_t value;                                                 // Color value
};

char blackColorName[] = "black";
char whiteColorName[] = "white";
char redColorName[] = "red";
char greenColorName[] = "green";
char blueColorName[] = "blue";
char cyanColorName[] = "cyan";
char magentaColorName[] = "magenta";
char yellowColorName[] = "yellow";
char orangeColorName[] = "orange";
char noirColorName[] = "noir";
char blancColorName[] = "blanc";
char rougeColorName[] = "rouge";
char vertColorName[] = "vert";
char bleuColorName[] = "bleu";
char jauneColorName[] = "jaune";

colorNames_s colorNames[] = {                                       // Compose table of color names/color values
    {blackColorName, ST7735_BLACK},
    {whiteColorName, ST7735_WHITE},
    {redColorName, ST7735_RED},
    {greenColorName, ST7735_GREEN},
    {blueColorName, ST7735_BLUE},
    {cyanColorName, ST7735_CYAN},
    {magentaColorName, ST7735_MAGENTA},
    {yellowColorName, ST7735_YELLOW},
    {orangeColorName, ST7735_ORANGE},
    {noirColorName, ST7735_BLACK},
    {blancColorName, ST7735_WHITE},
    {rougeColorName, ST7735_RED},
    {vertColorName, ST7735_GREEN},
    {bleuColorName, ST7735_BLUE},
    {jauneColorName, ST7735_YELLOW}
};

//  ---- Agenda ----
const char* separator = ";";                                        // Fields separator

#ifdef VERSION_FRANCAISE
    char agendaName[] = "Agenda";                                   // Agenda text
    char agendaName2[] = "Agenda";                                  // Agenda text
#else
    char agendaName[] = "Agenda";                                   // Agenda text
    char agendaName2[] = "Agenda";                                  // Agenda text
#endif

uint16_t agendaCount = 0;                                           // Count of agendas in file
uint16_t agendaPreviousTime = 0;                                    // Time of previous line in agenda
uint16_t tablePtr = 0;                                              // Pointer to table currently dumped by /tables
uint16_t tableRow = 0;                                              // Row  of table currently dumped by /tables

#define AGENDA_OFFSET 1                                             // Offset to add to index to get agenda line number

typedef enum {                                                      // Agenda structure type
    typeUnknown = 0,                                                // Content is unknown
    typeArrival,                                                    // Content is arrival
    typeDeparture,                                                  // Content is departure
    typeFixedMessage,                                               // Content is fixed message
    typeBlinkingMessage,                                            // Content is blinking message
    typeScrollingMessage,                                           // Content is scrolling message
    typeOutOfService                                                // Content is Out of Service
} lineType_t;

struct displayParams_s {
    uint16_t maxDelay;                                              // Max delay to simulation time to display line
    uint16_t maxTrackDelay;                                         // Max delay to simulation time to display track
    uint16_t oddColor;                                              // Background color to display on odd lines
    uint16_t evenColor;                                             // Background color to display on even lines
    uint16_t displayDuration;                                       // Display duration before switching
    bool enable;                                                    // Is this type of display active?
    uint8_t minLines;                                               // Minimum number of lines to display
    String header;                                                  // Display header
};

struct agendaTable_s {                                              // Agenda table
    uint16_t time;                                                  // Time (in minutes since midnight)
    uint16_t textColor;                                             // Text color
    uint16_t backgroundColor;                                       // Background color
    lineType_t lineType;                                            // Agenda line type
    char message[LINE_CHARACTERS+1];                                // Message
};

// Indexes into displayParameters
#define DISPLAY_ARRIVAL 0
#define DISPLAY_DEPARTURE 1
#define DISPLAY_UNKNOWN 99

typedef enum {
    unknownFileFormat = 0,                                          // File format in unknown
    agendaFileFormat                                                // We're in an agenda file
} fileFormat_t;

typedef enum {
    unknown = 0,
    fixed,                                                          // Message is fixed
    blinking,                                                       // Blink for one second
    scrolling                                                       // Scroll
} messageType_t;

int agendaError = -1;                                               // Error code of last agenda analysis
char lastErrorMessage[100] = {0};                                   // Last agenda loading error message
uint16_t agendaIndex = 0;                                           // Current position in agenda
fileFormat_t fileFormat = unknownFileFormat;                        // Current header file format
char configurationName[32] = {0};                                   // Configuration file name
int tableLineNumber = 0;                                            // Table line number
int fileLineNumber = 0;                                             // File line number
agendaTable_s* agendaTable = new agendaTable_s[0];                  // Agenda table
char additionalMessage[LINE_CHARACTERS+1] = {0};                    // Additional message
uint16_t additionalMessageTextColor = ST7735_WHITE;                 // Additional message text color
uint16_t additionalMessageBackgroundColor = ST7735_BLACK;           // Additional message background color
uint8_t additionalMessageIndex;                                     // Additional messge index (off/on or pointer)
unsigned long additionalMessageLastTime = 0;                        // Last time we did something on additional message
unsigned long additionalMessageStepDuration = 1000;                 // Additional message duration between indexes (ms)
messageType_t additionalMessageType;                                // Has an additional message to display
bool additionalMessageChanged = false;                              // Does additional message change in setAgendaLine?
displayParams_s displayParams[2];                                   // Parameters for arrival and departure
unsigned long lastSwitchTime = 0;                                   // Last time we switched type
uint8_t currentType = DISPLAY_UNKNOWN;                              // Current displayed type
uint16_t switchDelay = 0;                                           // Delay before switch
bool restartMe = false;                                             // Ask for code restart

//          --------------------------------------
//          ---- Function/routines definition ----
//          --------------------------------------

//  ---- WiFi ----
#ifdef ESP32
    void onWiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onWiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onWiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info);
#endif

#ifdef ESP8266
    WiFiEventHandler onStationModeConnectedHandler;                 // Event handler called when WiFi is connected
    WiFiEventHandler onStationModeDisconnectedHandler;              // Event handler called when WiFi is disconnected
    WiFiEventHandler onStationModeGotIPHandler;                     // Event handler called when WiFi got an IP
#endif

//  ---- Log routines ----
void saveLogMessage(const char* message);
String getLogLine(const uint16_t line, const bool reversedOrder = false);
void logSetup(void);
void syslogSetup(void);

//  ---- Serial commands ----
#ifdef SERIAL_COMMANDS
    void serialLoop(void);
#endif

//  ---- Trace rountines ----

#include <FF_Trace.h>                                               // Trace module https://github.com/FlyingDomotic/FF_Trace
trace_callback(traceCallback);
void traceSetup(void);
void enterRoutine(const char* routineName);

//  ---- System routines ----

#ifdef ESP32
    String verbosePrintResetReason(int3 reason);
#endif

String getResetCause(void);

//  ---- Preferences routines ----

void restartToApply(void);
void dumpSettings(void);
bool readSettings(void);
void writeSettings(void);

//  ---- Web server routines ----

void percentDecode(char *src);
int parseUrlParams(char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl);
void setupReceived(AsyncWebServerRequest *request);
void restReceived(AsyncWebServerRequest *request);
void settingsReceived(AsyncWebServerRequest *request);
void debugReceived(AsyncWebServerRequest *request);
void statusReceived(AsyncWebServerRequest *request);
void setChangedReceived(AsyncWebServerRequest *request);
void languagesReceived(AsyncWebServerRequest *request);
void configsReceived(AsyncWebServerRequest *request);
void commandReceived(AsyncWebServerRequest *request);
void logReceived(AsyncWebServerRequest *request);
void tableReceived(AsyncWebServerRequest *request);
void notFound(AsyncWebServerRequest *request);
void updateWebServerData(void);
void sendWebServerUpdate(void);
void startUpload(AsyncWebServerRequest *request);
void handleUpload(AsyncWebServerRequest *request, String fileName, size_t index, uint8_t *data,
    size_t len, bool final);

//  ---- OTA routines ----

#include <ArduinoOTA.h>                                             // OTA (network update)

void onStartOTA(void);
void onEndOTA(void);
void onErrorOTA(const ota_error_t erreur);

// --- User's routines ---

bool inString(const String candidate, const String listOfValues, const String separator = ",");
String extractItem(const String candidate, const uint16_t index, const String separator = ",");
void checkFreeBufferSpace(const char *function, const uint16_t line, const char *bufferName,
    const size_t bufferSize, const size_t bufferLen);
bool isDebugCommand(const String givenCommand);

//  ---- Display routines ----

uint8_t getLineTopPixel(const uint8_t row);
uint8_t getCharLeftPixel(const uint8_t column);
void setOutOfService(const char* text, const char* message=emptyChar, 
    const uint16_t textColor=ST7735_WHITE, const uint16_t backgroundColor=ST7735_BLACK);
void setDefaultOutOfService(const char* message=emptyChar, 
    const uint16_t textColor=ST7735_WHITE, const uint16_t backgroundColor=ST7735_BLACK);
void setTitle(const char* title);
void setText(const char* text, const int row, const int textColor, const int oddRowColor, const int evenRowColor,
    const bool timeTableLine=true, bool const dontTrace=false, bool const center=false, const bool centerBackground=false);
void setCenter(const char* text, const uint8_t row, const uint16_t textColor,
    const uint16_t backgroundColor, const bool dontTrace=false, const bool centerBackground=false);
bool setScroll(char* scrolledText, const char* text, const uint8_t pointer);
bool isNotEmpty(char toTest);
void displaySetup(void);
void displayLoop(void);
void updateScreen (const bool forced=false);
void updateLine(const uint8_t row, const uint8_t index, const bool forced=false);
size_t lineLen(char* line);
void displayAdditionalMessage(void);
void displayParameterChanged(void);
void formatTime(const int time, char* buffer, const size_t bufferLen, const char* prefix = emptyChar);
void startDisplay(void);
void stopDisplay(void);
void setAgendaLine(uint16_t index);
void refreshPanel(void);
uint16_t deltaTime (const uint16_t candidateTime);
void clearDisplay(void);
void uploadLoop(void);
uint8_t decodeHex(const char* hexa);
void clearMessage(char* message);
void setMessage(char* message, const char* data, const uint16_t offset, const uint16_t length);
void myStrncpy(char* destination, const char* source, const size_t size);
//  ---- Agenda routines ----

// Parameters for a readallFile callbacks
#define READ_FILE_PARAMETERS const char* fileLineData

int loadAgenda(void);
int loadAgendaDetails(void);
int readFile(const char* readFileName, int (*callback)(READ_FILE_PARAMETERS));
int readAllHeaders(READ_FILE_PARAMETERS);
int readAllTables(READ_FILE_PARAMETERS);
bool startWith(const char* stringToTest, const char* compareWith);
int checkValueRange(const char* stringValue, const int fieldNumber, uint8_t *valueToWrite,
    const uint8_t minValue, const uint8_t maxValue, const uint8_t defaultValue);
int checkValueRange(const char* stringValue, const int fieldNumber, uint16_t *valueToWrite,
    const uint16_t minValue, const uint16_t maxValue, const uint16_t defaultValue);
int checkColorRange(const char* stringValue, const int fieldNumber, uint16_t *valueToWrite,
    const uint16_t minValue, const uint16_t maxValue, const uint16_t defaultValue);
int signalError(const int errorCode, const int integerValue = 0, const char* stringValue = emptyChar);
bool waitForEventsEmpty(void);
void checkAgenda(void);

//          ----------------------------
//          ---- Functions/routines ----
//          ----------------------------

//  ---- WiFi ----

// Called when wifi is connected
#ifdef ESP32
    void onWiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif
#ifdef ESP8266
    void onWiFiStationConnected(WiFiEventStationModeConnected data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    char buffer[100];
    #ifdef VERSION_FRANCAISE
        snprintf_P(buffer, sizeof(buffer), "Connecté à %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #else
        snprintf_P(buffer, sizeof(buffer), "Connected to %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #endif
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    wifiState = String(buffer);
    updateWebServerData();
}

// Called when an IP is given
#ifdef ESP32
    void onWiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif
#ifdef ESP8266
    void onWiFiStationGotIp(WiFiEventStationModeGotIP data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    char buffer[100];
    #ifdef VERSION_FRANCAISE
        snprintf_P(buffer, sizeof(buffer), "Connecté à %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #else
        snprintf_P(buffer, sizeof(buffer), "Connected to %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #endif
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    wifiState = String(buffer);
    updateWebServerData();
}

// Called when WiFi is disconnected
#ifdef ESP32
    void onWiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif

#ifdef ESP8266
    void onWiFiStationDisconnected(WiFiEventStationModeDisconnected data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Wifi déconnecté", NULL);
    #else
        trace_info_P("Wifi disconnected", NULL);
    #endif
}

//  ---- Log routines ----

// Save a message to log queue
void saveLogMessage(const char* message) {
    strncpy(savedLogLines[savedLogNextSlot++], message, LOG_LINE_LEN-1);
    if (savedLogNextSlot >= LOG_MAX_LINES) {
        savedLogNextSlot = 0;
    }
}

// Returns a log line number
String getLogLine(const uint16_t line, const bool reversedOrder) {
    int16_t ptr = savedLogNextSlot;
    if (reversedOrder) {
        ptr -= line+1;
        if (ptr < 0) {
            ptr += (LOG_MAX_LINES-1);
        }
    } else {
        ptr += line+1;
        if (ptr >= LOG_MAX_LINES) {
            ptr -= (LOG_MAX_LINES-1);
        }
    }
    if (ptr >=0 && ptr < LOG_MAX_LINES) {
        return savedLogLines[ptr];
    }
    return String("");
}

// Setup part for log
void logSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    // Clear all log slots
    for (uint16_t i = 0; i < LOG_MAX_LINES; i++) {
        memset(savedLogLines[i], 0, LOG_LINE_LEN);
    }
}

// Setup part of syslog
void syslogSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef FF_TRACE_USE_SYSLOG
        if (syslogServer != "") {
            syslog.server(syslogServer.c_str(), syslogPort);
        }
        syslog.defaultPriority(LOG_LOCAL0 + LOG_DEBUG);
        syslog.appName(__FILENAME__);
    #endif
}

//  ---- Serial commands ----
#ifdef SERIAL_COMMANDS
    // Manage Serial commands
    void serialLoop(void) {
        while(Serial.available()>0) {
            char c = Serial.read();
            // Check for end of line
            if (c == '\n' || c== '\r') {
                // Do we have some command?
                if (serialCommandLen) {
                    #ifdef VERSION_FRANCAISE
                        Serial.printf("Commande : >%s<\n", serialCommand);
                    #else
                        Serial.printf("Command: >%s<\n", serialCommand);
                    #endif
                    String command = serialCommand;
                    if (isDebugCommand(command)) {
                        // Command is known and already executed, do nothing
                    } else {
                        #ifdef VERSION_FRANCAISE
                            Serial.println(PSTR("Utiliser enable/disable trace/debug/enter/syslog"));
                    #else
                            Serial.println(PSTR("Use enable/disable trace/debug/enter/syslog"));
                        #endif
                    }
                }
                // Reset command
                serialCommandLen = 0;
                serialCommand[serialCommandLen] = '\0';
            } else {
                // Do we still have room in buffer?
                if (serialCommandLen < sizeof(serialCommand)) {
                    // Yes, add char
                    serialCommand[serialCommandLen++] = c;
                    serialCommand[serialCommandLen] = '\0';
                } else {
                    // Reset command
                    serialCommandLen = 0;
                    serialCommand[serialCommandLen] = '\0';
                }
            }
        }
    }
#endif

//  ---- Trace routines ----

trace_declare();                                                    // Declare trace class

// Trace callback routine
//    _level contains severity level
//    _file: calling source file name with extension (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _line: calling source file line (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _function: calling calling source function name (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _message contains message to display/send

trace_callback(traceCallback) {
    //String messageLevel = FF_TRACE.textLevel(_level);
    if (_level != FF_TRACE_LEVEL_DEBUG || traceDebug) {             // Don't trace debug if debug flag not set
        Serial.print(FF_TRACE.textLevel(_level));
        Serial.print(": ");
        Serial.println(_message);                                   // Print message on Serial
        #ifdef SERIAL_FLUSH
            Serial.flush();
        #endif
        if (_level == FF_TRACE_LEVEL_ERROR || _level == FF_TRACE_LEVEL_WARN) {
            events.send(_message, "error");                         // Send message to destination
        } else if (_level != FF_TRACE_LEVEL_NONE) {
            if (events.count() && (events.avgPacketsWaiting() < 5)) {// If any clients connected and less than 5 packets pending
                events.send(_message, "info");                      // Send message to destination
            }
        }
        // Send trace to syslog if needed
        #ifdef FF_TRACE_USE_SYSLOG
            #define MIN_MICROS 1000
            if (syslogServer != "" && WiFi.status() == WL_CONNECTED && traceSyslog) {
                unsigned long currentMicros = micros();             // Get microseconds
                if ((currentMicros - lastSyslogMessageMicro) < MIN_MICROS) {  // Last message less than a ms
                    delayMicroseconds(MIN_MICROS - (currentMicros - lastSyslogMessageMicro)); // Wait remaining ms                            // Delay a ms to avoid overflow
                }
                syslog.deviceHostname(messageLevel.c_str());
                switch(_level) {
                    case FF_TRACE_LEVEL_ERROR:
                        syslog.log(LOG_ERR, _message);
                        break;
                    case FF_TRACE_LEVEL_WARN:
                        syslog.log(LOG_WARNING, _message);
                        break;
                    case FF_TRACE_LEVEL_INFO:
                        syslog.log(LOG_INFO, _message);
                        break;
                    default:
                        syslog.log(LOG_DEBUG, _message);
                        break;
                }
                lastSyslogMessageMicro = micros();                  // Save date of last syslog message in microseconds
            }
        #endif
        saveLogMessage(_message);                                   // Save message into circular log
    }
}

//  Trace setup code
void traceSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    trace_register(&traceCallback);                                 // Register callback
    FF_TRACE.setLevel(FF_TRACE_LEVEL_VERBOSE);                      // Start with verbose trace
}

//  Trace each routine entering
void enterRoutine(const char* routineName) {
    #ifdef VERSION_FRANCAISE
        trace_info_P("Entre dans %s", routineName);
    #else
        trace_info_P("Entering %s", routineName);
    #endif
}

//  ---- System routines ----

// Return ESP32 reset reason text
#ifdef ESP32
    String verbosePrintResetReason(int reason) {
        switch ( reason) {
            case 1  : return PSTR("Vbat power on reset");break;
            case 3  : return PSTR("Software reset digital core");break;
            case 4  : return PSTR("Legacy watch dog reset digital core");break;
            case 5  : return PSTR("Deep Sleep reset digital core");break;
            case 6  : return PSTR("Reset by SLC module, reset digital core");break;
            case 7  : return PSTR("Timer Group0 Watch dog reset digital core");break;
            case 8  : return PSTR("Timer Group1 Watch dog reset digital core");break;
            case 9  : return PSTR("RTC Watch dog Reset digital core");break;
            case 10 : return PSTR("Instrusion tested to reset CPU");break;
            case 11 : return PSTR("Time Group reset CPU");break;
            case 12 : return PSTR("Software reset CPU");break;
            case 13 : return PSTR("RTC Watch dog Reset CPU");break;
            case 14 : return PSTR("for APP CPU, reseted by PRO CPU");break;
            case 15 : return PSTR("Reset when the vdd voltage is not stable");break;
            case 16 : return PSTR("RTC Watch dog reset digital core and rtc module");break;
            default : return PSTR("Can't decode reason ")+String(reason);
        }
    }
#endif

// Return ESP reset/restart cause
String getResetCause(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef ESP32
        #ifdef VERSION_FRANCAISE
            String reason = "Raison reset : CPU#0: "+verbosePrintResetReason(rtc_get_reset_reason(0))
                +", CPU#1: "+verbosePrintResetReason(rtc_get_reset_reason(1));
        #else
            String reason = "Reset reasons: CPU#0: "+verbosePrintResetReason(rtc_get_reset_reason(0))
                +", CPU#1: "+verbosePrintResetReason(rtc_get_reset_reason(1));
        #endif
        return reason;
    #else
        struct rst_info *rtc_info = system_get_rst_info();
        // Get reset reason
        #ifdef VERSION_FRANCAISE
            String reason = PSTR("Raison reset : ") + String(rtc_info->reason, HEX)
                + PSTR(" - ") + ESP.getResetReason();
        #else
            String reason = PSTR("Reset reason: ") + String(rtc_info->reason, HEX)
                + PSTR(" - ") + ESP.getResetReason();
        #endif
        // In case of software restart, send additional info
        if (rtc_info->reason == REASON_WDT_RST
                || rtc_info->reason == REASON_EXCEPTION_RST
                || rtc_info->reason == REASON_SOFT_WDT_RST) {
            // If crashed, print exception
            if (rtc_info->reason == REASON_EXCEPTION_RST) {
                reason += PSTR(", exception (") + String(rtc_info->exccause)+PSTR("):");
            }
            reason += PSTR(" epc1=0x") + String(rtc_info->epc1, HEX)
                    + PSTR(", epc2=0x") + String(rtc_info->epc2, HEX)
                    + PSTR(", epc3=0x") + String(rtc_info->epc3, HEX)
                    + PSTR(", excvaddr=0x") + String(rtc_info->excvaddr, HEX)
                    + PSTR(", depc=0x") + String(rtc_info->depc, HEX);
        }
        return reason;
    #endif
}

//  ---- Preferences routines ----

// Dumps all settings on screen
void dumpSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    trace_info_P("ssid = %s", ssid.c_str());
    trace_info_P("pwd = %s", pwd.c_str());
    trace_info_P("accessPointPwd = %s", accessPointPwd.c_str());
    trace_info_P("name = %s", espName.c_str());
    trace_info_P("traceEnter = %s", traceEnter? "true" : "false");
    trace_info_P("traceDebug = %s", traceDebug? "true" : "false");
    trace_info_P("traceVerbose = %s", traceVerbose? "true" : "false");
    trace_info_P("traceJava = %s", traceJava? "true" : "false");
    trace_info_P("traceSyslog = %s", traceSyslog? "true" : "false");
    trace_info_P("traceTable = %s", traceTable? "true" : "false");
    trace_info_P("serverLanguage = %s", serverLanguage.c_str());
    trace_info_P("syslogServer = %s", syslogServer.c_str());
    trace_info_P("syslogPort = %d", syslogPort);
    trace_info_P("startTime = %02d:%02d", startTimeHour, startTimeMinute);
    trace_info_P("endTime = %02d:%02d", endTimeHour, endTimeMinute);
    trace_info_P("cycleTime = %.1f", cycleTime);
    trace_info_P("fileToStart = %s", fileToStart.c_str());
    trace_info_P("maxDelayArrival = %d",  displayParams[DISPLAY_ARRIVAL].maxDelay);
    trace_info_P("maxTrackDelayArrival = %d",  displayParams[DISPLAY_ARRIVAL].maxTrackDelay);
    trace_info_P("oddColorArrival = %d",  displayParams[DISPLAY_ARRIVAL].oddColor);
    trace_info_P("evenColorArrival = %d",  displayParams[DISPLAY_ARRIVAL].evenColor);
    trace_info_P("displayDurationArrival = %d",  displayParams[DISPLAY_ARRIVAL].displayDuration);
    trace_info_P("enableArrival = %s", displayParams[DISPLAY_ARRIVAL].enable? "true" : "false");
    trace_info_P("minLinesArrival = %d",  displayParams[DISPLAY_ARRIVAL].minLines);
    trace_info_P("headerArrival = %s",  displayParams[DISPLAY_ARRIVAL].header.c_str());
    trace_info_P("maxDelayDeparture = %d",  displayParams[DISPLAY_DEPARTURE].maxDelay);
    trace_info_P("maxTrackDelayDeparture = %d",  displayParams[DISPLAY_DEPARTURE].maxTrackDelay);
    trace_info_P("oddColorDeparture = %d",  displayParams[DISPLAY_DEPARTURE].oddColor);
    trace_info_P("evenColorDeparture = %d",  displayParams[DISPLAY_DEPARTURE].evenColor);
    trace_info_P("displayDurationDeparture = %d",  displayParams[DISPLAY_DEPARTURE].displayDuration);
    trace_info_P("enableDeparture = %s", displayParams[DISPLAY_DEPARTURE].enable? "true" : "false");
    trace_info_P("minLinesDeparture = %d",  displayParams[DISPLAY_DEPARTURE].minLines);
    trace_info_P("headerDeparture = %s",  displayParams[DISPLAY_DEPARTURE].header.c_str());
}

// Restart to apply message
void restartToApply(void) {
    #ifdef VERSION_FRANCAISE
        trace_info_P("*** Relancer l'ESP pour prise en compte ***", NULL);
    #else
        trace_info_P("*** Restart ESP to apply changes ***", NULL);
    #endif
}

// Read settings
bool readSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    File settingsFile = LittleFS.open(SETTINGS_FILE, "r");          // Open settings file
    if (!settingsFile) {                                            // Error opening?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut ouvrir %s", SETTINGS_FILE);
        #else
            trace_error_P("Failed to %s", SETTINGS_FILE);
        #endif
        return false;
    }

    JsonDocument settings;
    auto error = deserializeJson(settings, settingsFile);           // Read settings
    settingsFile.close();                                           // Close file
    if (error) {                                                    // Error reading JSON?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut décoder %s", SETTINGS_FILE);
        #else
            trace_error_P("Failed to parse %s", SETTINGS_FILE);
        #endif
        return false;
    }

    // Load all settings into corresponding variables
    traceEnter = settings["traceEnter"].as<bool>();
    traceDebug = settings["traceDebug"].as<bool>();
    traceVerbose = settings["traceVerbose"].as<bool>();
    traceJava = settings["traceJava"].as<bool>();
    traceSyslog = settings["traceSyslog"].as<bool>();
    traceTable = settings["traceTable"].as<bool>();
    ssid = settings["ssid"].as<String>();
    pwd = settings["pwd"].as<String>();
    accessPointPwd = settings["accessPointPwd"].as<String>();
    espName = settings["name"].as<String>();
    serverLanguage = settings["serverLanguage"].as<String>();
    syslogServer = settings["syslogServer"].as<String>();
    syslogPort = settings["syslogPort"].as<uint16_t>();
    startTimeHour = settings["startTimeHour"].as<uint16_t>();
    startTimeMinute = settings["startTimeMinute"].as<uint16_t>();
    endTimeHour = settings["endTimeHour"].as<uint16_t>();
    endTimeMinute = settings["endTimeMinute"].as<uint16_t>();
    cycleTime = settings["cycleTime"].as<float>();
    fileToStart = settings["fileToStart"].as<String>();
    displayParams[DISPLAY_ARRIVAL].maxDelay = settings["maxDelayArrival"].as<uint16_t>();
    displayParams[DISPLAY_ARRIVAL].maxTrackDelay = settings["maxTrackDelayArrival"].as<uint16_t>();
    displayParams[DISPLAY_ARRIVAL].oddColor = settings["oddColorArrival"].as<uint16_t>();
    displayParams[DISPLAY_ARRIVAL].evenColor = settings["evenColorArrival"].as<uint16_t>();
    displayParams[DISPLAY_ARRIVAL].displayDuration = settings["displayDurationArrival"].as<uint16_t>();
    displayParams[DISPLAY_ARRIVAL].enable = settings["enableArrival"].as<bool>();
    displayParams[DISPLAY_ARRIVAL].minLines = settings["minLinesArrival"].as<uint8_t>();
    displayParams[DISPLAY_ARRIVAL].header = settings["headerArrival"].as<String>();
    displayParams[DISPLAY_DEPARTURE].maxDelay = settings["maxDelayDeparture"].as<uint16_t>();
    displayParams[DISPLAY_DEPARTURE].maxTrackDelay = settings["maxTrackDelayDeparture"].as<uint16_t>();
    displayParams[DISPLAY_DEPARTURE].oddColor = settings["oddColorDeparture"].as<uint16_t>();
    displayParams[DISPLAY_DEPARTURE].evenColor = settings["evenColorDeparture"].as<uint16_t>();
    displayParams[DISPLAY_DEPARTURE].displayDuration = settings["displayDurationDeparture"].as<uint16_t>();
    displayParams[DISPLAY_DEPARTURE].enable = settings["enableDeparture"].as<bool>();
    displayParams[DISPLAY_DEPARTURE].minLines = settings["minLinesDeparture"].as<uint8_t>();
    displayParams[DISPLAY_DEPARTURE].header = settings["headerDeparture"].as<String>();

    
    // Use syslog port default value if needed
    if (syslogPort == 0) {
        syslogPort = 514;
    }

    // Dump settings on screen
    dumpSettings();
    return true;
}

// Write settings
void writeSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    JsonDocument settings;

    // Load settings in JSON
    settings["ssid"] = ssid.c_str();
    settings["pwd"] = pwd.c_str();
    settings["accessPointPwd"] = accessPointPwd.c_str();
    settings["name"] = espName.c_str();
    settings["traceEnter"] = traceEnter;
    settings["traceDebug"] = traceDebug;
    settings["traceVerbose"] = traceVerbose;
    settings["traceJava"] = traceJava;
    settings["traceSyslog"] = traceSyslog;
    settings["traceTable"] = traceTable;
    settings["serverLanguage"] = serverLanguage.c_str();
    settings["syslogServer"] = syslogServer.c_str();
    settings["syslogPort"] = syslogPort;
    settings["startTimeHour"] = startTimeHour;
    settings["startTimeMinute"] = startTimeMinute;
    settings["endTimeHour"] = endTimeHour;
    settings["endTimeMinute"] = endTimeMinute;
    settings["cycleTime"] = cycleTime;
    settings["fileToStart"] = fileToStart.c_str();
    settings["maxDelayArrival"] = displayParams[DISPLAY_ARRIVAL].maxDelay;
    settings["maxTrackDelayArrival"] = displayParams[DISPLAY_ARRIVAL].maxTrackDelay;
    settings["oddColorArrival"] = displayParams[DISPLAY_ARRIVAL].oddColor;
    settings["evenColorArrival"] = displayParams[DISPLAY_ARRIVAL].evenColor;
    settings["displayDurationArrival"] = displayParams[DISPLAY_ARRIVAL].displayDuration;
    settings["enableArrival"] = displayParams[DISPLAY_ARRIVAL].enable;
    settings["minLinesArrival"] = displayParams[DISPLAY_ARRIVAL].minLines;
    settings["headerArrival"] = displayParams[DISPLAY_ARRIVAL].header.c_str();
    settings["maxDelayDeparture"] = displayParams[DISPLAY_DEPARTURE].maxDelay;
    settings["maxTrackDelayDeparture"] = displayParams[DISPLAY_DEPARTURE].maxTrackDelay;
    settings["oddColorDeparture"] = displayParams[DISPLAY_DEPARTURE].oddColor;
    settings["evenColorDeparture"] = displayParams[DISPLAY_DEPARTURE].evenColor;
    settings["displayDurationDeparture"] = displayParams[DISPLAY_DEPARTURE].displayDuration;
    settings["enableDeparture"] = displayParams[DISPLAY_DEPARTURE].enable;
    settings["minLinesDeparture"] = displayParams[DISPLAY_DEPARTURE].minLines;
    settings["headerDeparture"] = displayParams[DISPLAY_DEPARTURE].header.c_str();

    File settingsFile = LittleFS.open(SETTINGS_FILE, "w");          // Open settings file
    if (!settingsFile) {                                            // Error opening?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut ouvrir %s en écriture", SETTINGS_FILE);
        #else
            trace_error_P("Can't open %s for write", SETTINGS_FILE);
        #endif
        return;
    }

    uint16_t bytes = serializeJsonPretty(settings, settingsFile);   // Write JSON structure to file
    if (!bytes) {                                                   // Error writting?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut écrire %s", SETTINGS_FILE);
        #else
            trace_error_P("Can't write %s", SETTINGS_FILE);
        #endif
    }
    settingsFile.flush();                                           // Flush file
    settingsFile.close();                                           // Close it
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Envoi settings", NULL);
    #else
        trace_debug_P("Sending settings event", NULL);
    #endif
    events.send("Ok", "settings");                                  // Send a "settings" (changed) event
}

//  ---- Web server routines ----

//  Perform URL percent decoding
void percentDecode(char *src) {
    char *dst = src;
    while (*src) {
        if (*src == '+') {
            src++;
            *dst++ = ' ';
        } else if (*src == '%') {
            // handle percent escape
            *dst = '\0';
            src++;
            if (*src >= '0' && *src <= '9') {*dst = *src++ - '0';}
            else if (*src >= 'A' && *src <= 'F') {*dst = 10 + *src++ - 'A';}
            else if (*src >= 'a' && *src <= 'f') {*dst = 10 + *src++ - 'a';}
            *dst <<= 4;
            if (*src >= '0' && *src <= '9') {*dst |= *src++ - '0';}
            else if (*src >= 'A' && *src <= 'F') {*dst |= 10 + *src++ - 'A';}
            else if (*src >= 'a' && *src <= 'f') {*dst |= 10 + *src++ - 'a';}
            dst++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

//  Parse an URL parameters list and return each parameter and value in a given table
int parseUrlParams (char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl) {
    int ct = 0;

    while (queryString && *queryString && ct < resultsMaxCt) {
    results[ct][0] = strsep(&queryString, "&");
    results[ct][1] = strchr(results[ct][0], '=');
    if (*results[ct][1]) *results[ct][1]++ = '\0';
    if (decodeUrl) {
        percentDecode(results[ct][0]);
        percentDecode(results[ct][1]);
    }
    ct++;
    }
    return ct;
}

// Called when /setup is received
void setupReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "setup.htm", "text/html");
    request->send(response);                                        // Send setup.htm
}

// Called when /rest is received
void restReceived (AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    if (request->url() == "/rest/restart") {
        request->send(200, "text/plain", "Restarting...");
        restartMe = true;
        return;
    }
}

// Called when /settings is received
void settingsReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, SETTINGS_FILE, "application/json");
    request->send(response);                                        // Send settings.json
}

// Called when /debug is received
void debugReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    // Send a json document with interresting variables
    JsonDocument answer;
    answer["version"] = VERSION;
    answer["wifiState"] = wifiState.c_str();
    answer["traceEnter"] = traceEnter;
    answer["traceDebug"] = traceDebug;
    answer["traceVerbose"] = traceVerbose;
    answer["traceJava"] = traceJava;
    answer["startTimeHour"] = startTimeHour;
    answer["startTimeMinute"] = startTimeMinute;
    answer["endTimeHour"] = endTimeHour;
    answer["endTimeMinute"] = endTimeMinute;
    answer["cycleTime"] = cycleTime;
    answer["simulationTime"] = simulationTime;
    answer["simulationStart"] = simulationStart;
    answer["simulationStop"] = simulationStop;
    answer["minuteDuration"] = minuteDuration;
    answer["simulationActive"] = simulationActive;
    answer["agendaError"] = agendaError;
    answer["agendaIndex"] = agendaIndex;
    #ifdef ESP32
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxAllocHeap();
        answer["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    String  buffer;
    serializeJsonPretty(answer, buffer);
    request->send(200, "application/json", buffer);
}

// Called when /upload data is received
void startUpload(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    // Load file name
    if(request->hasParam("file", true, true)) {
        const AsyncWebParameter* fileParameter = request->getParam("file", true, true);
        if (fileParameter) {
            String fileName = fileParameter->value();
            // Do we have a .txt file?
            if (fileName.endsWith(".txt")) {
                lastUploadStatus = 200;
                request->send(lastUploadStatus, "Starting upload");
                return;
            }
            // File name is not supported
            lastUploadStatus = 412;
            request->send(lastUploadStatus, "Unsupported file name");
            #ifdef VERSION_FRANCAISE
                trace_error_P("Nom de fichier %s illégal", fileParameter->value().c_str());
            #else
                trace_error_P("Illegal file name %s", fileParameter->value().c_str());
            #endif
            return;
        }
    }
    // File parameter not found in POST request
    lastUploadStatus = 400;
    request->send(lastUploadStatus, "No file parameter");
}

// Called when a /upload data
void handleUpload(AsyncWebServerRequest *request, String fileName, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        request->_tempFile = LittleFS.open("/tmpfile.tmp", "w");
    }

    if (len) {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data, len);
    }

    if (final) {
        // close the file handle as the upload is now done
        request->_tempFile.close();
        request->send(200, "Upload ok");
        // Delete existing file
        if (LittleFS.exists(fileName)) {
            LittleFS.remove(fileName);
        }
        // Rename file
        LittleFS.rename("/tmpfile.tmp", fileName);
        lastUploadedFile = fileName;
    }
}

// Called when a /status click is received
void statusReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    // Send a json document with data correponding to current status
    JsonDocument answer;
    char buffer[512];
    formatTime(simulationTime, buffer, sizeof(buffer));
    answer["simulationTime"] = buffer;
    formatTime(simulationStart, buffer, sizeof(buffer));
    answer["simulationStart"] = buffer;
    formatTime(simulationStop, buffer, sizeof(buffer));
    answer["simulationStop"] = buffer;
    answer["cycleTime"] = cycleTime;
    answer["agendaError"] = agendaError;
    answer["agendaCount"] = agendaCount;
    answer["dataSize"] = (agendaCount * sizeof(agendaTable_s));
    #ifdef ESP32
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxAllocHeap();
        answer["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    serializeJsonPretty(answer, buffer, sizeof(buffer));
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    request->send(200, "application/json", buffer);
}

// Called when /changed/<variable name>/<variable value> is received
void setChangedReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    bool dontWriteSettings = false;
    String position = request->url().substring(1);
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Reçu %s", position.c_str());
    #else
        trace_debug_P("Received %s", position.c_str());
    #endif
    int separator1 = position.indexOf("/");                         // Position of first "/"
    if (separator1 >= 0) {
        int separator2 = position.indexOf("/", separator1+1);       // Position of second "/"
        if (separator2 > 0) {
            // Extract field name and value
            String fieldName = position.substring(separator1+1, separator2);
            String fieldValue = position.substring(separator2+1);
            // Check against known field names and set value accordingly
            if (fieldName == "traceEnter") {
                traceEnter = (fieldValue == "true");
            } else if (fieldName == "traceDebug") {
                traceDebug = (fieldValue == "true");
            } else if (fieldName == "traceVerbose") {
                traceVerbose = (fieldValue == "true");
            } else if (fieldName == "traceJava") {
                traceJava = (fieldValue == "true");
            } else if (fieldName == "traceSyslog") {
                traceSyslog = (fieldValue == "true");
            } else if (fieldName == "traceTable") {
                traceTable = (fieldValue == "true");
            } else if (fieldName == "ssid") {
                ssid = fieldValue;
                restartToApply();
            } else if (fieldName == "pwd") {
                restartToApply();
                pwd = fieldValue;
            } else if (fieldName == "accessPointPwd") {
                restartToApply();
                accessPointPwd = fieldValue;
            } else if (fieldName == "name") {
                restartToApply();
                espName = fieldValue;
            } else if (fieldName == "serverLanguage") {
                serverLanguage = fieldValue;
            } else if (fieldName == "syslogServer") {
                #ifdef FF_TRACE_USE_SYSLOG
                    if (fieldValue != "") {
                        if (syslogServer != fieldValue) {
                            syslog.server(fieldValue.c_str(), syslogPort);
                        }
                    }
                #endif
                syslogServer = fieldValue;
            } else if (fieldName == "syslogPort") {
                #ifdef FF_TRACE_USE_SYSLOG
                    if (fieldValue.toInt() > 0 && syslogServer != "") {
                        if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 65535) {
                            if (syslogPort != fieldValue.toInt()) {
                                syslog.server(syslogServer.c_str(), fieldValue.toInt());
                            }
                        }
                    }
                #endif
                syslogPort = fieldValue.toInt();
            } else if (fieldName == "startTimeHour") {
                if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 23) {
                    startTimeHour = atol(fieldValue.c_str());
                    displayParameterChanged();
                }
            } else if (fieldName == "startTimeMinute") {
                if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 59) {
                    startTimeMinute = atol(fieldValue.c_str());
                    displayParameterChanged();
                }
            } else if (fieldName == "endTimeHour") {
                if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 23) {
                    endTimeHour = atol(fieldValue.c_str());
                    displayParameterChanged();
                }
            } else if (fieldName == "endTimeMinute") {
                if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 59) {
                    endTimeMinute = atol(fieldValue.c_str());
                    displayParameterChanged();
                }
            } else if (fieldName == "cycleTime") {
                cycleTime = atof(fieldValue.c_str());
            } else if (fieldName == "fileToStart") {
                if (fieldValue != fileToStart) {
                    fileToStart = fieldValue;
                    writeSettings();
                    loadAgenda();
                }
            } else if (fieldName == "maxDelayArrival") {
                displayParams[DISPLAY_ARRIVAL].maxDelay = fieldValue.toInt();
            } else if (fieldName == "maxTrackDelayArrival") {
                displayParams[DISPLAY_ARRIVAL].maxTrackDelay = fieldValue.toInt();
            } else if (fieldName == "oddColorArrival") {
                displayParams[DISPLAY_ARRIVAL].oddColor = fieldValue.toInt();
            } else if (fieldName == "evenColorArrival") {
                displayParams[DISPLAY_ARRIVAL].evenColor = fieldValue.toInt();
            } else if (fieldName == "displayDurationArrival") {
                displayParams[DISPLAY_ARRIVAL].displayDuration = fieldValue.toInt();
            } else if (fieldName == "enableArrival") {
                displayParams[DISPLAY_ARRIVAL].enable = (fieldValue == "true");
            } else if (fieldName == "minLinesArrival") {
                displayParams[DISPLAY_ARRIVAL].minLines = fieldValue.toInt();
            } else if (fieldName == "headerArrival") {
                displayParams[DISPLAY_ARRIVAL].header = fieldValue;
            } else if (fieldName == "maxDelayDeparture") {
                displayParams[DISPLAY_DEPARTURE].maxDelay = fieldValue.toInt();
            } else if (fieldName == "maxTrackDelayDeparture") {
                displayParams[DISPLAY_DEPARTURE].maxTrackDelay = fieldValue.toInt();
            } else if (fieldName == "oddColorDeparture") {
                displayParams[DISPLAY_DEPARTURE].oddColor = fieldValue.toInt();
            } else if (fieldName == "evenColorDeparture") {
                displayParams[DISPLAY_DEPARTURE].evenColor = fieldValue.toInt();
            } else if (fieldName == "displayDurationDeparture") {
                displayParams[DISPLAY_DEPARTURE].displayDuration = fieldValue.toInt();
            } else if (fieldName == "enableDeparture") {
                displayParams[DISPLAY_DEPARTURE].enable = (fieldValue == "true");
            } else if (fieldName == "minLinesDeparture") {
                displayParams[DISPLAY_DEPARTURE].minLines = fieldValue.toInt();
            } else if (fieldName == "headerDeparture") {
                displayParams[DISPLAY_DEPARTURE].header = fieldValue;
            } else if (fieldName == "start") {
                startDisplay();
                dontWriteSettings = true;
            } else if (fieldName == "stop") {
                stopDisplay();
                dontWriteSettings = true;
            } else if (fieldName == "restart") {
                restartMe = true;
            } else {
                // This is not a known field
                #ifdef VERSION_FRANCAISE
                    trace_error_P("Donnée >%s< inconnue, valeur >%s<", fieldName.c_str(), fieldValue.c_str());
                #else
                    trace_error_P("Can't set field >%s<, value >%s<", fieldName.c_str(), fieldValue.c_str());
                #endif
                char msg[70];                                       // Buffer for message
                snprintf_P(msg, sizeof(msg),PSTR("<status>Bad field name %s</status>"), fieldName.c_str());
                checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
                request->send(400, emptyChar, msg);
                return;
            }
            if (!dontWriteSettings) writeSettings();
        } else {
            // This is not a known field
            #ifdef VERSION_FRANCAISE
                trace_error_P("Pas de nom de donnée", NULL);
            #else
                trace_error_P("No field name", NULL);
            #endif
            char msg[70];                                           // Buffer for message
            snprintf_P(msg, sizeof(msg),PSTR("<status>No field name</status>"));
            checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
            request->send(400, emptyChar, msg);
            return;
        }
    }
    request->send(200, emptyChar, "<status>Ok</status>");
}

// Called when /languages command is received
void languagesReceived(AsyncWebServerRequest *request){
    if (traceEnter) enterRoutine(__func__);
    String path = "/";
#ifdef ESP32
    File dir = LittleFS.open(path);
#else
    Dir dir = LittleFS.openDir(path);
    path = String();
#endif
    String output = "[";
#ifdef ESP32
    File entry = dir.openNextFile();
    while(entry){
#else
    while(dir.next()){
        fs::File entry = dir.openFile("r");
#endif
        String fileName = String(entry.name());
        if (fileName.startsWith("lang_")) {
            #ifdef ESP32
                fileName = path + fileName;
            #endif
            File languageFile = LittleFS.open(fileName, "r");       // Open language file
            if (languageFile) {                                     // Open ok?
                JsonDocument jsonData;
                auto error = deserializeJson(jsonData, languageFile); // Read settings
                languageFile.close();                               // Close file
                if (!error) {                                       // Reading JSON ok?
                    if (output != "[") output += ',';
                    output += "{\"code\":\"";
                    output += jsonData["code"].as<String>();
                    output += "\",\"text\":\"";
                    output += jsonData["text"].as<String>();
                    output += "\"}";
                } else {
                    #ifdef VERSION_FRANCAISE
                        trace_error_P("Ne peut decoder %s", fileName.c_str());
                    #else
                        trace_error_P("Can't decode %s", fileName.c_str());
                    #endif
                }
            } else {
                #ifdef VERSION_FRANCAISE
                    trace_error_P("Ne peut ouvrir %s", fileName.c_str());
                #else
                    trace_error_P("Can't open %s", fileName.c_str());
                #endif
            }
        }
        #ifdef ESP32
            entry = dir.openNextFile();
        #else
            entry.close();
        #endif
        }
#ifdef ESP32
    dir.close();
#endif
    output += "]";
    request->send(200, "application/json", output);
}

// Called when /configs command is received
void configsReceived(AsyncWebServerRequest *request){
    if (traceEnter) enterRoutine(__func__);
    String path = "/";
#ifdef ESP32
    File dir = LittleFS.open(path);
#else
    Dir dir = LittleFS.openDir(path);
    path = String();
#endif
    String output = "[";
#ifdef ESP32
    File entry = dir.openNextFile();
    while(entry){
#else
    while(dir.next()){
        fs::File entry = dir.openFile("r");
#endif
        String fileName = String(entry.name());
        if (fileName.endsWith(".txt")) {
            #ifdef ESP32
                fileName = path + fileName;
            #endif
            if (output != "[") output += ',';
            output += "\"" + fileName + "\"";
        }
        #ifdef ESP32
            entry = dir.openNextFile();
        #else
            entry.close();
        #endif
        }
#ifdef ESP32
    dir.close();
#endif
    output += "]";
    request->send(200, "application/json", output);
}

// Called when /command/<command name>/<commandValue> is received
void commandReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    String position = request->url().substring(1);
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Reçu %s", position.c_str());
    #else
        trace_debug_P("Received %s", position.c_str());
    #endif
    String commandName = emptyChar;
    String commandValue = emptyChar;
    int separator1 = position.indexOf("/");                         // Position of first "/"
    if (separator1 >= 0) {
        int separator2 = position.indexOf("/", separator1+1);       // Position of second "/"
        if (separator2 > 0) {
            // Extract field name and value
            commandName = position.substring(separator1+1, separator2);
            commandValue = position.substring(separator2+1);
        } else {
            commandName = position.substring(separator1+1);
        }
        // Check against known command names
        if (commandName == "xxx") {
        } else {
            // This is not a known field
            #ifdef VERSION_FRANCAISE
                trace_error_P("Commande >%s< inconnue", commandName.c_str());
            #else
                trace_error_P("Can't execute command >%s<", commandName.c_str());
            #endif
            char msg[70];                                           // Buffer for message
            snprintf_P(msg, sizeof(msg),PSTR("<status>Bad command name %s</status>"), commandName.c_str());
            checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
            request->send(400, emptyChar, msg);
            return;
        }
    } else {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Pas de commande", NULL);
        #else
            trace_error_P("No command name", NULL);
        #endif
        char msg[70];                                               // Buffer for message
        snprintf_P(msg, sizeof(msg),PSTR("<status>No command name</status>"));
        checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
        request->send(400, emptyChar, msg);
        return;
    }
    request->send(200, emptyChar, "<status>Ok</status>");
}

// Called when /log is received - Send saved log, line by line
void logReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain; charset=utf-8",
            [](uint8_t *logResponseBuffer, size_t maxLen, size_t index) -> size_t {
        // For all log lines
        while (logRequestNextLog < LOG_MAX_LINES) {
            // Get message
            String message = getLogLine(logRequestNextLog++);
            // If not empty
            if (message != emptyChar) {
                // Compute message len (adding a "\n" at end)
                size_t chunkSize = min(message.length(), maxLen-1)+1;
                // Copy message
                memcpy(logResponseBuffer, message.c_str(), chunkSize-1);
                // Add "\n" at end
                logResponseBuffer[chunkSize-1] = '\n';
                // Return size (and message loaded)
                return chunkSize;
            }
        }
        // That's the end
        return 0;
    });
    logRequestNextLog = 0;
    request->send(response);
}

// Called when /tables is received
void tableReceived(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain",
            [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (index == 0) {                                           // Init pointers if index is null
            tablePtr = 0;
            tableRow = 0;
        }
        memset(buffer, 0, maxLen);                                  // Clear buffer
        char header[100] = {0};                                     // Clear header
        if (tablePtr ==  0) {                                       // Room table
            if (!tableRow)  {                                       // Set header
                snprintf_P(header, sizeof(header), "Row Flg Time Text Fore Message (%s)\n", agendaName);
            }
            if (agendaCount) {
                snprintf_P((char*) buffer, maxLen, "%s%3d %3d %4d %04x %04x %s\n", header, tableRow,
                    agendaTable[tableRow].lineType, agendaTable[tableRow].time,
                    agendaTable[tableRow].textColor, agendaTable[tableRow].backgroundColor,
                    agendaTable[tableRow].message);
            } else {
                snprintf_P((char*) buffer, maxLen, "%s", header);
            }
            tableRow++;                                             // Next table index
            if (tableRow >= agendaCount) {                          // Are we at end of table?
                tablePtr++;                                         // Next table
                tableRow = 0;                                       // Clear index
            }
        } else if (tablePtr ==  1) {                                // Screen table
            if (!tableRow)  {                                       // Set header
                snprintf_P(header, sizeof(header), "Row Flg TxtC OddC  EveC Message (Screen)\n");
            }
            snprintf_P((char*) buffer, maxLen, "%s%3d %3d %04x %04x %04x %s\n", header, tableRow,
                screenLines[tableRow].flags, screenLines[tableRow].textColor,
                screenLines[tableRow].oddBackgroundColor, screenLines[tableRow].evenBackgroundColor,
                screenLines[tableRow].text);
            tableRow++;                                             // Next table index
            if (tableRow >= SCREEN_LINES) {                         // Are we at end of table?
                tablePtr++;                                         // Next table
                tableRow = 0;                                       // Clear index
            }
        } else {
            tablePtr = 0;                                           // Clear table
            tableRow = 0;                                           // Clear index
            return 0;                                               // Finished!
        }
      return strlen((char*) buffer);
    });
    request->send(response);
}

// Called when a request can't be mapped to existing ones
void notFound(AsyncWebServerRequest *request) {
    char msg[120];
    #ifdef VERSION_FRANCAISE
        snprintf_P(msg, sizeof(msg), PSTR("Fichier %s inconnu"), request->url().c_str());
    #else
        snprintf_P(msg, sizeof(msg), PSTR("File %s not found"), request->url().c_str());
    #endif
    trace_debug(msg);
    checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
    request->send(404, "text/plain", msg);
    trace_info(msg);
}

//  ---- OTA routines ----

// Called when OTA starts
void onStartOTA(void) {
    if (traceEnter) enterRoutine(__func__);
    if (ArduinoOTA.getCommand() == U_FLASH) {                       // Program update
        #ifdef VERSION_FRANCAISE
            trace_info_P("Début MAJ firmware", NULL);
        #else
            trace_info_P("Starting firmware update", NULL);
        #endif
    } else {                                                        // File system update
        #ifdef VERSION_FRANCAISE
            trace_info_P("Début MAJ fichiers", NULL);
        #else
            trace_info_P("Starting file system update", NULL);
        #endif
        LittleFS.end();
    }
}

// Called when OTA ends
void onEndOTA(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Fin de MAJ", NULL);
    #else
        trace_info_P("End of update", NULL);
    #endif
}

// Called when OTA error occurs
void onErrorOTA(const ota_error_t erreur) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        String msg = "Erreur OTA(";
        msg += String(erreur);
        msg += ") : Erreur ";
        if (erreur == OTA_AUTH_ERROR) {
            msg += "authentification";
        } else if (erreur == OTA_BEGIN_ERROR) {
            msg += "lancement";
        } else if (erreur == OTA_CONNECT_ERROR) {
            msg += "connexion";
        } else if (erreur == OTA_RECEIVE_ERROR) {
            msg += "réception";
        } else if (erreur == OTA_END_ERROR) {
            msg += "fin";
        } else {
            msg += "inconnue !";
        }
    #else
        String msg = "OTA error(";
        msg += String(erreur);
        msg += ") : Error ";
        if (erreur == OTA_AUTH_ERROR) {
            msg += "authentication";
        } else if (erreur == OTA_BEGIN_ERROR) {
            msg += "starting";
        } else if (erreur == OTA_CONNECT_ERROR) {
            msg += "connecting";
        } else if (erreur == OTA_RECEIVE_ERROR) {
            msg += "receiving";
        } else if (erreur == OTA_END_ERROR) {
            msg += "terminating";
        } else {
            msg += "unknown !";
        }
    #endif
    trace_error(msg.c_str());
}

// --- User's routines ---

// Looks for string into a list of strings
bool inString(const String candidate, const String listOfValues, const String separator) {
    int endPosition = 0;
    int startPosition = 0;
    String allValues = listOfValues + separator;
    while (endPosition >= 0) {
        endPosition = allValues.indexOf(separator, startPosition);
        // Compare sending number with extracted one
        if (candidate.equalsIgnoreCase(allValues.substring(startPosition, endPosition))) {
            return true;
        }
        startPosition = endPosition+1;
    }
    return false;
}

// Returns part of a string, giving index and delimiter
String extractItem(const String candidate, const uint16_t index, const String separator) {
    int endPosition = 0;
    int startPosition = 0;
    int i = 0;
    String allValues = candidate + separator;
    while (endPosition >= 0) {
        endPosition = allValues.indexOf(separator, startPosition);
        if (i == index) {
            // Return part corresponding to required item
            return allValues.substring(startPosition, endPosition);
        }
        startPosition = endPosition+1;
    }
    return emptyChar;
}

// Check for remaining space into a buffer
void checkFreeBufferSpace(const char *function, const uint16_t line, const char *bufferName,
        const size_t bufferSize, const size_t bufferLen) {
    if ((bufferSize - bufferLen) < 0 || bufferSize <= 0) {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Taille %d et longueur %d pour %s dans %s:%d", bufferSize, bufferLen,
                bufferName, function, line);
        #else
            trace_error_P("Invalid size %d and length %d for %s in %s:%d", bufferSize, bufferLen,
                bufferName, function, line);
        #endif
    } else {
        size_t freeSize = bufferSize - bufferLen;
        uint8_t percent = (bufferLen * 100)/ bufferSize;
        if (percent > 90) {
            #ifdef VERSION_FRANCAISE
                trace_debug_P("%s:%d: %s rempli à %d\%%, %d octets libres (taille %d, longueur %d))",
            #else
                trace_debug_P("%s:%d: %s is %d\%% full, %d bytes remaining (size %d, length %d))",
            #endif
                function, line, bufferName, percent, freeSize, bufferSize, bufferLen);
        }
    }
}

// Execute a debug command, received either by Serial or MQTT
bool isDebugCommand(const String givenCommand) {
    if (traceEnter) enterRoutine(__func__);
    String command = String(givenCommand);
    command.toLowerCase();
    // enable/disable trace/debug
    if (command == "enable debug") {
        traceDebug = true;
    } else if (command == "disable debug") {
        traceDebug = false;
    } else if (command == "enable verbose") {
        traceVerbose = true;
    } else if (command == "disable verbose") {
        traceVerbose = false;
    } else if (command == "enable enter") {
        traceEnter = true;
    } else if (command == "disable enter") {
        traceEnter = false;
    } else if (command == "enable java") {
        traceJava = true;
    } else if (command == "disable java") {
        traceJava = false;
    } else if (command == "enable syslog") {
        traceSyslog = true;
    } else if (command == "disable syslog") {
        traceSyslog = false;
    } else if (command == "enable table") {
        traceTable = true;
    } else if (command == "disable table") {
        traceTable = false;
    } else {
        return false;
    }
    return true;
}

// Update state on web server
void updateWebServerData(void) {
    // Flag update needed
    sendAnUpdateFlag = true;
}

// Send web server data to clients
void sendWebServerUpdate(void) {
    if (traceEnter) enterRoutine(__func__);
    char buffer[512];
    // Send new state to connected users
    JsonDocument data;
    data["serverName"] = espName.c_str();
    data["serverVersion"] = VERSION;
    data["wifiState"] = wifiState.c_str();
    char buffer2[20];
    if (simulationActive) {
        formatTime(simulationTime, buffer2, sizeof(buffer2));       // Format time
    } else {
        #ifdef VERSION_FRANCAISE
            formatTime(simulationTime, buffer2, sizeof(buffer2), (char*) "Arrêté à ");
        #else
            formatTime(simulationTime, buffer2, sizeof(buffer2), (char*) "Stopped at ");
        #endif
    }
    data["currentTime"] = buffer2;
    #ifdef ESP32
        data["freeMemory"] = ESP.getFreeHeap();
        data["largestChunk"] = ESP.getMaxAllocHeap();
        data["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        data["freeMemory"] = ESP.getFreeHeap();
        data["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    if (agendaError == -1) {
        #ifdef VERSION_FRANCAISE
            data["agendaState"] = "*** Agenda pas encore chargé ***";
        #else
            data["agendaState"] = "*** Agenda not yet loaded ***";
        #endif
    } else if (agendaError == 0) {
        #ifdef VERSION_FRANCAISE
            data["agendaState"] = fileToStart + " chargé";
        #else
            data["agendaState"] = fileToStart + " loaded";
        #endif
    } else {
        data["agendaState"] = lastErrorMessage;
    }
    serializeJson(data, buffer, sizeof(buffer));
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    events.send(buffer, "data");
    sendAnUpdateFlag = false;
}

//  ---- Display routines ----

// Clear a message buffer (should be at least LINE_CHARACTERS+1 in size)
void clearMessage(char* message) {
    for (uint8_t i = 0; i < LINE_CHARACTERS; i++) {
        message[i] = 32;                                            // Set character to space
    }
    message[LINE_CHARACTERS] = 0;                                   // Set end character
}

// Set part of message starting at offset on legngth characters to data
void setMessage(char* message, const char* data, const uint16_t offset, const uint16_t length) {
    uint8_t i;
    for (i = 0; i < min((uint16_t) strlen(data), length); i++) {    // Copy data char limited to length
        message[i+offset] = data[i];
    }
    for (; i < length; i++) {                                       // Clear remaining chars
        message[i+offset] = 32;
    } 
}

// Copy a screen line
void myStrncpy(char* destination, const char* source, const size_t size){
    strncpy(destination, source, size);                // Copy source up to size
    destination[size-1] = 0;                           // Force a \0 at end of string
}

// Set default out of service text
void setDefaultOutOfService(const char* message, const uint16_t textColor, const uint16_t backgroundColor) {
    setOutOfService((char *) DEFAULT_OUT_OF_SERVICE, message, textColor, backgroundColor);
}

// Set out of service text
void setOutOfService(const char* text, const char* message, const uint16_t textColor, const uint16_t backgroundColor) {
    myStrncpy(screenLines[OUT_OF_SERVICE_MESSAGE].text, message, LINE_CHARACTERS+1);
    screenLines[OUT_OF_SERVICE_MESSAGE].textColor = ST7735_WHITE;
    screenLines[OUT_OF_SERVICE_MESSAGE].oddBackgroundColor = ST7735_BLACK;
    screenLines[OUT_OF_SERVICE_MESSAGE].evenBackgroundColor = ST7735_BLACK;
    myStrncpy(screenLines[OUT_OF_SERVICE_TEXT].text, text, LINE_CHARACTERS+1);
    screenLines[OUT_OF_SERVICE_TEXT].textColor = textColor;
    screenLines[OUT_OF_SERVICE_TEXT].oddBackgroundColor = backgroundColor;
    screenLines[OUT_OF_SERVICE_TEXT].evenBackgroundColor = backgroundColor;
    // Reset switcht imer to avoid too short time before switching
    lastSwitchTime = millis();
    updateScreen();
}

// Return top position of text line (starts at 0)
uint8_t getLineTopPixel(const uint8_t row) {
    return row * (PIXEL_HEIGHT + 1);
}

// Return start horizontal pixel of character (starts at 0)
uint8_t getCharLeftPixel(const uint8_t column) {
    return column * PIXEL_WIDTH;
}

// Set screen title (first 2 lignes)
void setTitle(const char* title) {
    setCenter(title, 0, ST7735_WHITE, ST7735_BLACK);
    setCenter(emptyChar, 1, ST7735_BLACK, ST7735_BLACK);
}

// Center a line on screen
void setCenter(const char* text, const uint8_t row, const uint16_t textColor, const uint16_t backgroundColor,
        const bool dontTrace, const bool centerBackground) {
    setText(text, row, textColor, backgroundColor, backgroundColor, false, dontTrace, true, centerBackground);
}

// Overwrite a buffer with scrolled text giving a pointer (return true on pointer overflow)
bool setScroll(char* scrolledText, const char* text, const uint8_t pointer) {
    if (!pointer) return false;                                     // Return if pointer is null
    uint16_t scrolledTextLength = strlen(scrolledText);             // Len of output text
    if (pointer > (scrolledTextLength + strlen(text))) return true; // Pointer outside limits
    int offset = max(scrolledTextLength - pointer, 0);              // Where to start text insertion
    int textStart = max(pointer - scrolledTextLength, 0);           // Where to start in text to insert
    setMessage(scrolledText, &text[textStart], offset, scrolledTextLength-offset); // Copy text at right place
    return false;
}

// Test if a line start is not space or zero
bool isNotEmpty(char* toTest){
    if (!toTest[0]) return false;
    for (uint8_t i=0; i<LINE_CHARACTERS; i++) {
        if (toTest[i] != 32) return true;
    }    
    return false;
}

// Set text line given a text color, and a background color for odd and even row
void setText(const char* text, const int row, const int textColor, const int oddRowColor, const int evenRowColor,
        const bool timeTableLine, const bool dontTrace, const bool center, const bool centerBackground) {
    myStrncpy(screenLines[row].text, text, LINE_CHARACTERS+1);
    screenLines[row].textColor = textColor;
    screenLines[row].oddBackgroundColor = oddRowColor;
    screenLines[row].evenBackgroundColor = evenRowColor;
    screenLines[row].flags =
        (timeTableLine? screenFlagTimeTableLine : screenFlagNone) |
        (dontTrace? screenFlagDontTrace : screenFlagNone) |
        (center? screenFlagCentered : screenFlagNone) |
        (centerBackground? screenFlagCenterBackground : screenFlagNone);
}

// Setup for displays
void displaySetup(void) {
    if (traceEnter) enterRoutine(__func__);
    tft.initR(INITR_MINI160x80_PLUGIN);                             // Init display as 160x80
    tft.setTextWrap(false);                                         // Disable text wrapping (in case of error)
    tft.setRotation(3);                                             // Landscape, wires on left
    tft.setTextSize(1);                                             // 10 lines of 26 chars (27th is a bit truncated)
    for (uint8_t i=0; i<SCREEN_LINES; i++) {                        // Clear all screenLines data
        screenLines[i].text = (char *) &currentTexts[i];            // Set current line pointer
        clearMessage(screenLines[i].text);
        screenLines[i].textColor = 0;
        screenLines[i].oddBackgroundColor = 0;
        screenLines[i].evenBackgroundColor = 0;
        screenLines[i].flags = 0;
        previousLines[i].text = (char *) &previousTexts[i];         // Set previous line pointer
        clearMessage(previousLines[i].text);
        previousLines[i].textColor = 0;
        previousLines[i].oddBackgroundColor = 0;
        previousLines[i].evenBackgroundColor = 0;
        previousLines[i].flags = 0;
    }
    setDefaultOutOfService();                                       // Set default out of message
    updateScreen(true);                                             // Display OOS message on screen
}

// Loop for display
void displayLoop(void) {
    unsigned long now = millis();
    if (simulationActive && !agendaError) {
        // Does switch timeout?
        if ((now - lastSwitchTime) > switchDelay) {
            // If arrival and departure aren't active, set OOS
            if (!displayParams[DISPLAY_ARRIVAL].enable && !displayParams[DISPLAY_DEPARTURE].enable) {
                setDefaultOutOfService();
            } else {
                if (currentType == DISPLAY_ARRIVAL) {
                    if (displayParams[DISPLAY_DEPARTURE].enable) {
                        currentType = DISPLAY_DEPARTURE;
                    }
                } else {
                    if (displayParams[DISPLAY_ARRIVAL].enable) {
                        currentType = DISPLAY_ARRIVAL;
                    }
                }
                lastSwitchTime = now;
                switchDelay = displayParams[currentType].displayDuration * 1000; 
            }
        }
        if ((now - displayLastRun) >= minuteDuration) {          // Last run more than a simulated minute?
            if (events.count() && !events.avgPacketsWaiting()) {    // If any clients connected and no packets pending
                char buffer[128];
                // Send new state to connected users
                JsonDocument data;
                char buffer2[6];
                formatTime(simulationTime, buffer2, sizeof(buffer2)); // Format time
                data["currentTime"] = buffer2;
                #ifdef ESP32
                    data["freeMemory"] = ESP.getFreeHeap();
                    data["largestChunk"] = ESP.getMaxAllocHeap();
                    data["memoryLowMark"] = ESP.getMinFreeHeap();
                #else
                    data["freeMemory"] = ESP.getFreeHeap();
                    data["largestChunk"] = ESP.getMaxFreeBlockSize();
                #endif
                serializeJson(data, buffer, sizeof(buffer));
                checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
                events.send(buffer, "data");
            }
            // Execute actions from current position in simulation up to current time
            if (agendaIndex < agendaCount && agendaTable[agendaIndex].time <= simulationTime) {
                char buffer[6];
                formatTime(simulationTime, buffer, sizeof(buffer));
                #ifdef VERSION_FRANCAISE
                    trace_info_P("Agenda %d, maintenant %s", agendaIndex+AGENDA_OFFSET, buffer);
                #else
                    trace_info_P("Agenda %d, now %s", agendaIndex+AGENDA_OFFSET, buffer);
                #endif
                while (agendaIndex < agendaCount && agendaTable[agendaIndex].time <= simulationTime) {
                    agendaIndex++;
                }
                if (additionalMessageChanged) {
                    trace_debug_P("Message >%s<, type %d", additionalMessage, additionalMessageType);
                    displayAdditionalMessage();
                }
            }
            simulationTime++;                                       // Simulation is one minute later
            if (simulationTime > simulationStop) {                  // After end of simulation time?
                char buffer[6], buffer2[6];
                simulationTime = simulationStart;                   // Reinit time
                formatTime(simulationStop, buffer, sizeof(buffer));
                formatTime(simulationTime, buffer2, sizeof(buffer2));
                #ifdef VERSION_FRANCAISE
                    trace_info_P("On passe de %s à %s", buffer, buffer2);
                #else
                    trace_info_P("Going from %s to %s", buffer, buffer2);
                #endif
                agendaIndex = 0;                                    // Reset agendaIndex
                additionalMessageChanged = false;
                // Position index to start time
                while (agendaIndex < agendaCount && agendaTable[agendaIndex].time < simulationStart) {
                    setAgendaLine(agendaIndex);
                    agendaIndex++;
                }
                if (additionalMessageChanged) {
                    trace_debug_P("Message >%s<, type %d", additionalMessage, additionalMessageType);
                    displayAdditionalMessage();
                }
            }
            refreshPanel();
            displayLastRun = now;                                   // Set last run time
        }
    }
    // Advance additional message index every second
    if ((now - additionalMessageLastTime) > additionalMessageStepDuration) {
        displayAdditionalMessage();
    }
}

// Display additional message if needed
void displayAdditionalMessage() {
    if (additionalMessageType == fixed) {
        setCenter(additionalMessage, MAX_DETAIL_LINES + 1, additionalMessageTextColor,
            additionalMessageBackgroundColor, true);
    } 
    if (additionalMessageType == blinking) {
        if (additionalMessageIndex & 1) {
            setCenter(emptyChar, MAX_DETAIL_LINES + 1, additionalMessageTextColor,
                additionalMessageBackgroundColor, true);
        } else {
            setCenter(additionalMessage, MAX_DETAIL_LINES + 1, additionalMessageTextColor,
                additionalMessageBackgroundColor, true);
        }
    } 
    if (additionalMessageType == scrolling) {
        char message[LINE_CHARACTERS+1];
        clearMessage(message);
        if (setScroll(message, additionalMessage, additionalMessageIndex)) {
            additionalMessageIndex = -1;
        } else {
            setCenter(message, MAX_DETAIL_LINES + 1, additionalMessageTextColor,
                additionalMessageBackgroundColor, true);
        }
    } 
    additionalMessageIndex++;
    additionalMessageLastTime = millis();
    refreshPanel();
}

// Called when display parameters changed
void displayParameterChanged(void) {
    if (traceEnter) enterRoutine(__func__);
    simulationStart = (startTimeHour * 60) + startTimeMinute;
    simulationStop = (endTimeHour * 60) + endTimeMinute;
    minuteDuration = cycleTime * 60000.0 / (float) (simulationStop + 1 - simulationStart);
}

// Format a time in minutes as hh:mm
void formatTime(const int time, char* buffer, const size_t bufferLen, const char* prefix) {
    snprintf_P(buffer, bufferLen, "%s%02d:%02d", prefix, time / 60, time - ((time / 60) *60));
}

// Activate display simulation
void startDisplay(void) {
    if (traceEnter) enterRoutine(__func__);
    clearDisplay();                                                 // Clear display
    displayParameterChanged();                                      // Compute parameters
    if (!agendaError) {
        simulationActive = true;                                    // Simulation is active
        #ifdef VERSION_FRANCAISE
            trace_info("Début simulation");
        #else
            trace_info("Starting simulation");
        #endif
        // Turn everything off
        setOutOfService(emptyChar);
        agendaIndex = 0;
        additionalMessageChanged = false;
        // Play one time all events
        while (agendaIndex < agendaCount) {
            setAgendaLine(agendaIndex);
            agendaIndex++;
        }
        agendaIndex = 0;
        // Position index to start time
        while (agendaIndex < agendaCount && agendaTable[agendaIndex].time < simulationStart) {
            setAgendaLine(agendaIndex);
            agendaIndex++;
        }
        if (additionalMessageChanged) {
            trace_debug_P("Message >%s<, type %d", additionalMessage, additionalMessageType);
            displayAdditionalMessage();
        }
    }
    sendAnUpdateFlag = true;
}

// Stop display simulation
void stopDisplay(void) {
    if (traceEnter) enterRoutine(__func__);
    simulationActive = false;                                       // Simulation is not active
    #ifdef VERSION_FRANCAISE
        trace_info("Fin simulation");
    #else
        trace_info("Stoping simulation");
    #endif
    sendAnUpdateFlag = true;
}

// Load additional line from agenda index
void setAgendaLine(uint16_t index){
    if (agendaTable[index].lineType == typeFixedMessage) {
        additionalMessageChanged = true;
        if (agendaTable[index].message[0]) {
            additionalMessageType = fixed;
            additionalMessageStepDuration = 1000;
            myStrncpy(additionalMessage, agendaTable[index].message, LINE_CHARACTERS+1);
            additionalMessageBackgroundColor = agendaTable[index].backgroundColor;
            additionalMessageTextColor = agendaTable[index].textColor;
        } else {
            additionalMessageType = unknown;
            additionalMessage[0] = 0;
        }
    } else if (agendaTable[index].lineType == typeBlinkingMessage) {
        additionalMessageChanged = true;
        if (agendaTable[index].message[0]) {
            additionalMessageType = blinking;
            additionalMessageStepDuration = 1000;
            additionalMessageIndex = 0;
            myStrncpy(additionalMessage, agendaTable[index].message, LINE_CHARACTERS+1);
            additionalMessageBackgroundColor = agendaTable[index].backgroundColor;
            additionalMessageTextColor = agendaTable[index].textColor;
        } else {
            additionalMessageType = unknown;
            additionalMessage[0] = 0;
        }
    } else if (agendaTable[index].lineType == typeScrollingMessage) {
        additionalMessageChanged = true;
        if (agendaTable[index].message[0]) {
            additionalMessageType = scrolling;
            additionalMessageStepDuration = 200;
            additionalMessageIndex = 0;
            myStrncpy(additionalMessage, agendaTable[index].message, LINE_CHARACTERS+1);
            additionalMessageBackgroundColor = agendaTable[index].backgroundColor;
            additionalMessageTextColor = agendaTable[index].textColor;
        } else {
            additionalMessageType = unknown;
            additionalMessage[0] = 0;
        }
    } else if (agendaTable[index].lineType == typeOutOfService) {
        setOutOfService(agendaTable[index].message, emptyChar, agendaTable[index].textColor, agendaTable[index].backgroundColor);
    }
}

// Clear display
void clearDisplay(void) {
    if (screenCleared) return;                                      // Avoid blinking screen if 2 screen clearing are done w/o change between them
    if (traceEnter) enterRoutine(__func__);
    tft.fillScreen(ST7735_BLACK);
    screenCleared = true;
    for (uint8_t i=0; i<OUT_OF_SERVICE_MESSAGE; i++) {              // For all lines but out of service text (else clears screen every time)
        previousLines[i].text[0] = 255;                             // Set first char to impossible character
    }
}

// Compute delta time between a candidate time and simulation time
uint16_t deltaTime (const uint16_t candidateTime) {
    int delta = candidateTime - simulationTime;
    return delta < 0 ? delta+1440: delta;
}

// Update display panel
void refreshPanel(void) {
    // Do nothing if agenda error (state already set)
    if (agendaError) return;
    // Display header if type change
    setTitle(displayParams[currentType].header.c_str());
    // Scan agenda starting at agendaIndex until we exceed .maxDelay and .minLine or looped on agenda
    uint16_t i = agendaIndex;
    uint8_t line = 0;
    uint8_t maxLine = additionalMessageType != unknown ? MAX_DETAIL_LINES-1 : MAX_DETAIL_LINES;
    while (deltaTime(agendaTable[i].time) < displayParams[currentType].maxDelay || line < displayParams[currentType].minLines) {
        // Load agenda element
        agendaTable_s agendaData = agendaTable[i];
        // Are we on the right line type?
        if ((agendaData.lineType == typeArrival && currentType == DISPLAY_ARRIVAL)
                || (agendaData.lineType == typeDeparture && currentType == DISPLAY_DEPARTURE)) {
            char message[LINE_CHARACTERS+1];                        // Create a message
            clearMessage(message);                                  // Clear it
            myStrncpy(message, agendaData.message, LINE_CHARACTERS+1); // Copy message
            // Clear track if line too far from now
            if (deltaTime(agendaTable[i].time) > displayParams[currentType].maxTrackDelay) {
                setMessage(message, " ", TRACK_OFFSET, TRACK_LENGTH);
            }
            setText(message, line+DETAIL_LINES_OFFSET,              // Display line at right place with right color
                ST7735_WHITE, displayParams[currentType].oddColor, displayParams[currentType].evenColor, true);
            line++;                                                 // Increment line count
        }
        i++;                                                        // Next agenda line
        if (i == agendaIndex) break;                                // Check if we looped on agenda, exit if so
        if (i >= agendaCount) i = 0;                                // Return to zero if at end of agenda
        if (line >= maxLine) break;                                 // Exit if we fully loaded table
    }
    for (; line < maxLine; line++) {
        setText(emptyChar, line+DETAIL_LINES_OFFSET,                       // Display line at right place with right color
            ST7735_WHITE, displayParams[currentType].oddColor, displayParams[currentType].evenColor, true);

    }
    updateScreen();
}

// Check if something changed on a screen line
bool didLineChanged(const uint8_t row) {
    return
        (strncmp(previousLines[row].text, screenLines[row].text, LINE_CHARACTERS+1)) ||
        (previousLines[row].textColor != screenLines[row].textColor) ||
        (previousLines[row].oddBackgroundColor != screenLines[row].oddBackgroundColor) ||
        (previousLines[row].evenBackgroundColor != screenLines[row].evenBackgroundColor) ||
        (previousLines[row].flags != screenLines[row].flags);
}

// Save new values of a screen line
void saveScreen(const uint8_t row) {
    myStrncpy(previousLines[row].text, screenLines[row].text, LINE_CHARACTERS+1);
    previousLines[row].textColor = screenLines[row].textColor;
    previousLines[row].oddBackgroundColor = screenLines[row].oddBackgroundColor;
    previousLines[row].evenBackgroundColor = screenLines[row].evenBackgroundColor;
    previousLines[row].flags = screenLines[row].flags;
}

// Update screen when changed (or forced)
void updateScreen (const bool forced) {
    if (forced) clearDisplay();
    // Are we in an out of service state?
    if (isNotEmpty(screenLines[OUT_OF_SERVICE_TEXT].text)) {
        // Does something change (or forced)?
        if (forced || didLineChanged(OUT_OF_SERVICE_TEXT)) {
            if (traceVerbose) {
                #ifdef VERSION_FRANCAISE
                    trace_debug_P("Hors service >%s<", screenLines[OUT_OF_SERVICE_TEXT].text);
                #else
                    trace_debug_P("OutOfService >%s<", screenLines[OUT_OF_SERVICE_TEXT].text);
                #endif
            }
            if (!forced) clearDisplay();
            // Draw a 3 pixels cross
            screenCleared = false;
            tft.drawLine(1, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-2, ST7735_RED);
            tft.drawLine(0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, ST7735_RED);
            tft.drawLine(0, 1, SCREEN_WIDTH-2, SCREEN_HEIGHT-1, ST7735_RED);
            tft.drawLine(0, SCREEN_HEIGHT-2, SCREEN_WIDTH-2, 0, ST7735_RED);
            tft.drawLine(0, SCREEN_HEIGHT-1, SCREEN_WIDTH-1, 0, ST7735_RED);
            tft.drawLine(1, SCREEN_HEIGHT-1, SCREEN_WIDTH-1, 1, ST7735_RED);
            uint8_t centerRow = (SCREEN_HEIGHT/2) - (PIXEL_HEIGHT/2);   // Screen center - half character height
            uint8_t messageLen = strlen(screenLines[OUT_OF_SERVICE_TEXT].text) * PIXEL_WIDTH;
            uint8_t startCol = (SCREEN_WIDTH - messageLen) / 2;     // Compute start column (center text)
            const uint8_t colOffset = 2;                            // Offset before and after text
            uint8_t startFill = max(startCol - colOffset, 0);       // Column to start filling background
            uint8_t fillLen = min(messageLen + (colOffset << 1), SCREEN_WIDTH); // Length to fill
            tft.fillRect(startFill, centerRow-1, fillLen, PIXEL_HEIGHT+2, 
                screenLines[OUT_OF_SERVICE_TEXT].oddBackgroundColor); // Clear zone under text, limit to text + offset
            tft.setCursor(startCol, centerRow);                         // Position cursor
            tft.setTextColor(screenLines[OUT_OF_SERVICE_TEXT].textColor);
            tft.print(screenLines[OUT_OF_SERVICE_TEXT].text);  // Display OOS message
            saveScreen(OUT_OF_SERVICE_TEXT);
        }
        // Do we have an out of service message?
        if (forced || didLineChanged(OUT_OF_SERVICE_MESSAGE)) {
            if (isNotEmpty(screenLines[OUT_OF_SERVICE_MESSAGE].text)) {
            updateLine(OUT_OF_SERVICE_MESSAGE, MAX_LINES-1, forced);
            }
            saveScreen(OUT_OF_SERVICE_MESSAGE);
        }
        return;
    } else {
        // Were we in an out of service state?
        if (isNotEmpty(previousLines[OUT_OF_SERVICE_TEXT].text)) {
            if (!forced) clearDisplay();
            saveScreen(OUT_OF_SERVICE_TEXT);
        }
    }
    // Scan all displayed lines
    for (uint8_t i=0; i<MAX_LINES; i++) {
        // Line changed or forced?
        if (forced || didLineChanged(i)) {
            updateLine(i, i, forced);
            saveScreen(i);
        }
    }
}

// Return a screen line length (trimming ending spaces)
size_t lineLen(char* line) {
    for (uint8_t i=LINE_CHARACTERS; i>0; i--) {
        if (line[i-1] != 32) return i;
    }
    return 0;
}

// Display a new line at a given row and index
void updateLine(const uint8_t row, const uint8_t index, const bool forced) {
    // Trace if requested
    if (forced || !(screenLines[index].flags & screenFlagDontTrace)) {
        if (traceVerbose) {
            #ifdef VERSION_FRANCAISE
                trace_debug_P("Ligne %d >%s< flags %d", index, screenLines[index].text, screenLines[index].flags);
            #else
                trace_debug_P("Ligne %d >%s< flags %d", index, screenLines[index].text, screenLines[index].flags);
            #endif
        }
    }
    // Screen is not anymore cleared
    screenCleared = false;
    // Fill a full text line with color
    tft.fillRect(0, getLineTopPixel(index), SCREEN_WIDTH, PIXEL_HEIGHT, 
        index&1 ? screenLines[index].evenBackgroundColor : screenLines[index].oddBackgroundColor);

    // Clear inter column text for timetable lines
    if ((screenLines[index].flags & screenFlagTimeTableLine)) {
        tft.fillRect(getCharLeftPixel(5) + (PIXEL_WIDTH / 2) -2, getLineTopPixel(index), 2, PIXEL_HEIGHT, ST7735_BLACK);
        tft.fillRect(getCharLeftPixel(19) + (PIXEL_WIDTH / 2) -2, getLineTopPixel(index), 2, PIXEL_HEIGHT, ST7735_BLACK);
        tft.fillRect(getCharLeftPixel(21) + (PIXEL_WIDTH / 2) -2, getLineTopPixel(index), 2, PIXEL_HEIGHT, ST7735_BLACK);
    }
    // Do we have something to display?
    if (isNotEmpty(screenLines[index].text)) {
        uint8_t startCol = (screenLines[index].flags & screenFlagCentered) ?
            (SCREEN_WIDTH - (strlen(screenLines[index].text) * PIXEL_WIDTH)) / 2 : 0;
        tft.setCursor(startCol, getLineTopPixel(index));
        tft.setTextColor(screenLines[index].textColor);
        tft.print(screenLines[index].text);
    }
}

// Work with files just after they're uploaded
void uploadLoop(void) {
    if (lastUploadedFile != emptyChar) {
        #ifdef VERSION_FRANCAISE
            trace_info_P("Reçu %s", lastUploadedFile.c_str());
        #else
            trace_info_P("Just received %s", lastUploadedFile.c_str());
        #endif
        #ifdef VERSION_FRANCAISE
            trace_debug_P("Envoi loadSelect", NULL);
        #else
            trace_debug_P("Sending loadSelect event", NULL);
        #endif
        events.send("loadSelect", "execute");                       // Send execute event to destination
        if (lastUploadedFile != fileToStart) {
            fileToStart = lastUploadedFile;
            writeSettings();
        }
        lastUploadedFile = emptyChar;
        loadAgenda();
        startDisplay();
        refreshPanel();
    }
}

// Convert 2 hexadecimal characeters to 8 bits value
uint8_t decodeHex(const char* hexa) {
    return (uint8_t) strtol(hexa, 0, 16);
}

//  ---- Agenda routines ----
// Signal an error()
int signalError(const int errorCode, const int integerValue,  const char* stringValue) {
    memset(lastErrorMessage, 0, sizeof(lastErrorMessage));
    switch (errorCode) {
        case 100:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Ne peut ouvrir %s ***", stringValue);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Can't open file %s ***", stringValue);
            #endif
            break;
        case 101:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Entête >%s< incorrecte dans %s ***",
                    stringValue, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Bad file header >%s< in %s ***",
                    stringValue, configurationName);
            #endif
            break;
        case 102:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Partie %s manquante ***", stringValue);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Part missing for %s ***", stringValue);
            #endif
            break;
        case 103:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Entête >%s< déjà définie dans %s, avant la ligne %d ***",
                    stringValue, configurationName, fileLineNumber);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** >%s< header already defined in %s, before line %d ***",
                    stringValue, configurationName, fileLineNumber);
            #endif
            break;
        case 104:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Nombre de zones (%d) incorrect ligne %d de %s ***",
                    integerValue, fileLineNumber, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage), "*** Bad field count (%d) line %d of %s ***",
                    integerValue, fileLineNumber, configurationName);
            #endif
            break;
        case 105:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Valeur >%s< incorrecte, zone %d, ligne %d de %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Illegal number >%s< field %d line %d of %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #endif
            break;
        case 106:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Valeur >%s< hors limite, zone %d, ligne %d de %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Out of range >%s< field %d line %d of %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #endif
            break;
        case 109:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Heure >%s< incorrecte, zone %d, ligne %d de %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Illegal time >%s<, field %d line %d of %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #endif
            break;
        case 110:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Couleur >%s< inconnue, zone %d, ligne %d de %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Iunknown color >%s<, field %d line %d of %s ***",
                    stringValue, integerValue, fileLineNumber, configurationName);
            #endif
            break;
        default:
            #ifdef VERSION_FRANCAISE
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Erreur %d inconnue, fichier %s, ligne %d, entier %d, chaîne >%s< ***",
                    errorCode, configurationName, fileLineNumber, integerValue, stringValue);
            #else
                snprintf_P(lastErrorMessage, sizeof(lastErrorMessage),
                    "*** Unkown error %d, file %s, line %d, integer %d, string >%s< ***",
                    errorCode, configurationName, fileLineNumber, integerValue, stringValue);
            #endif
    }
    trace_error(lastErrorMessage);
    return errorCode;
}

// Check if a string starts with another
bool startWith(const char* stringToTest, const char* compareWith) {
    return strncmp(stringToTest, compareWith, strlen(compareWith)) == 0;
}

// Convert string to uint8_t, check value range or set default
int checkValueRange(const char* stringValue, const int fieldNumber, uint8_t *valueToWrite,
        const uint8_t minValue, const uint8_t maxValue, const uint8_t defaultValue) {
    size_t stringLen = strlen(stringValue);                         // Get value length
    if (stringLen) {                                                // Not null string
        long value = 0;                                             // Extracted value
        for (int i=0; i < (int) stringLen; i++) {                   // Check each character
            if (stringValue[i] >= '0' && stringValue[i] <= '9') {   // Numeric value?
                value = (value * 10) + (stringValue[i] - '0');      // Compute new value
            } else {
                return signalError(105, fieldNumber, stringValue);
            }
        }
        if (value < minValue || value > maxValue) {
            return signalError(106, fieldNumber, stringValue);
        }
        *valueToWrite = value;
    } else {
        *valueToWrite = defaultValue;
    }
    return 0;
}

// Convert string to uint16_t, check value range or set default
int checkValueRange(const char* stringValue, const int fieldNumber, uint16_t *valueToWrite,
        const uint16_t minValue, const uint16_t maxValue, const uint16_t defaultValue) {
    size_t stringLen = strlen(stringValue);                         // Get value length
    if (stringLen) {                                                // Not null string
        long value = 0;                                             // Extracted value
        for (int i=0; i < (int) stringLen; i++) {                   // Check each character
            if (stringValue[i] >= '0' && stringValue[i] <= '9') {   // Numeric value?
                value = (value * 10) + (stringValue[i] - '0');      // Compute new value
            } else {
                return signalError(105, fieldNumber, stringValue);
            }
        }
        if (value < minValue || value > maxValue) {
            return signalError(106, fieldNumber, stringValue);
        }
        *valueToWrite = value;
    } else {
        *valueToWrite = defaultValue;
    }
    return 0;
}

// Convert color to uint16_t, check value range or set default
int checkColorRange(const char* stringValue, const int fieldNumber, uint16_t *valueToWrite,
        const uint16_t minValue, const uint16_t maxValue, const uint16_t defaultValue) {
    size_t stringLen = strlen(stringValue);                         // Get value length
    if ((stringValue[0] >= 'a' && stringValue[0] <= 'z') || (stringValue[0] >= 'A' && stringValue[0] <= 'Z')) {
        char colorName[stringLen+1];                                // Make a copy of field
        for (size_t i = 0; i < stringLen; i++) {                    // For all chars
            if (stringValue[i] >= 'A' && stringValue[i] <= 'Z') {   // Is this a capital letter?
                colorName[i] = stringValue[i] + ('a' - 'A');        // Make it lower case
            } else {
                colorName[i] = stringValue[i];                      // Copy as is
            }
        }
        colorName[stringLen] = 0;                                   // End string
        for (uint8_t i = 0; i < sizeof(colorNames); i++) {          // Scan color table
            if (!strcmp(colorName, colorNames[i].color)) {          // Are we on a known color?
                *valueToWrite = colorNames[i].value;                // Copy value
                return 0;
            }
        }
        return signalError(110, fieldNumber, stringValue);
    } else {
        return checkValueRange(stringValue, fieldNumber, valueToWrite, minValue, maxValue, defaultValue);
    }
}

// Read a file, calling a callback with each line
int readFile(const char* readFileName, int (*callback)(READ_FILE_PARAMETERS)) {
    fileLineNumber = 0;
    fileFormat = unknownFileFormat;
    strncpy(configurationName, readFileName, sizeof(configurationName));
    File fileStream = LittleFS.open(configurationName, "r");
    if (!fileStream) {
        return signalError(100, 0, configurationName);
    }
    char lineContent[128];
    int errorCode = 0;
    while (fileStream.available()) {
        memset(lineContent, 0, sizeof(lineContent));
        int lengthRead = fileStream.readBytesUntil('\n', lineContent, sizeof(lineContent));
        if (lengthRead) {
            // Ends line on <CR>, <LF> or "["
            for (int i = 0; i < lengthRead; i++) {
                if (lineContent[i] == '\n' || lineContent[i] == '\r' || lineContent[i] == '#') {
                    lineContent[i] = 0;
                    break;
                }
            }
            // Remove trailing spaces and ";"
            for(int i=strlen(lineContent)-1; i > 0; i--) {
                if (lineContent[i] != ' ' && lineContent[i] != ';') break;
                lineContent[i] = 0;
            }
            fileLineNumber++;
            // Ignore empty lines
            if (lineContent[0]) {
                errorCode = callback(lineContent);
                if (errorCode) break;
            } else {
                fileFormat = unknownFileFormat;
            }
        }
    }
    fileStream.close();
    return errorCode;
}

// Read file header callback
int readAllHeaders(READ_FILE_PARAMETERS) {
    if (fileFormat == unknownFileFormat) {                          // We're not in a table description
        if (startWith(fileLineData, "Time;") || startWith(fileLineData, "Heure;")) {
            if (agendaCount) {
                return signalError(103, 0, agendaName);
            }
            fileFormat = agendaFileFormat;
        } else {
            return signalError(101, 0, fileLineData);
        }
    } else {
        // File format is known
        if (fileFormat == agendaFileFormat) agendaCount++;
    }
    return 0;
}

// Read all tables callback
int readAllTables(READ_FILE_PARAMETERS) {
    int errorCode = 0;
    if (fileFormat == unknownFileFormat) {                          // We're not in a table description
        if (startWith(fileLineData, "Heure;") || startWith(fileLineData, "Time;")) {
            fileFormat = agendaFileFormat;
        }
        tableLineNumber = 0;                                        // Reset table line number
    } else {                                                        // We're in a table definition
        tableLineNumber++;                                          // Increment table line  number
        // Copy line and split it on ";"
        char lineCopy[strlen(fileLineData)+1];                      // Copy of line
        memset(lineCopy, 0, sizeof(lineCopy));                      // Init line
        strncpy(lineCopy, fileLineData, strlen(fileLineData));      // Do copy line
        char* field = strtok(lineCopy, separator);                  // Get first field
        int fieldCount = 0;                                         // Contain field number
        // Loop on separator
        int index = tableLineNumber -1;                             // Compute index
        while (field != NULL) {                                     // Still to do?
            fieldCount++;                                           // Bump field number
            if (fileFormat == agendaFileFormat) {
                if (fieldCount == 1) {                              // Time
                    char timeCopy[7];                               // Copy of time string
                    uint8_t hoursValue;                             // Hours
                    uint8_t minutesValue;                           // Minutes
                    strncpy(timeCopy, field, sizeof(timeCopy)-1);   // Copy time value
                    if (timeCopy[2] != ':') return signalError(106, fieldCount, field);
                    timeCopy[2] = 0;                                // Force null after hours
                    errorCode = checkValueRange (timeCopy, fieldCount, &hoursValue, 0, 23, 0);
                    if (errorCode) return errorCode;
                    errorCode = checkValueRange (&timeCopy[3], fieldCount, &minutesValue, 0, 59, 0);
                    if (errorCode) return errorCode;
                    agendaTable[index].time = (hoursValue * 60) + minutesValue;
                    // Check if this time is less thant previous one (time not sorted)
                    if (agendaTable[index].time < agendaPreviousTime) {
                        return signalError(109, fieldCount, field);
                    }
                    clearMessage(agendaTable[index].message);
                    setMessage(agendaTable[index].message, field, TIME_OFFSET, TIME_LENGTH);
                } else if (fieldCount == 2) {                       // Type
                    if (!strcmp(field, "A")) {                      // Arrivée/Arrival
                        agendaTable[index].lineType = typeArrival;
                    } else if (!strcmp(field, "D")) {               // Départ/Departure
                        agendaTable[index].lineType = typeDeparture;
                    } else if (!strcmp(field, "MF") || !strcmp(field, "FM")) {  // Message fixe/Fixed message
                        agendaTable[index].lineType = typeFixedMessage;
                    } else if (!strcmp(field, "MC") || !strcmp(field, "FM")) {  // Message clignotant/Fixed message
                        agendaTable[index].lineType = typeBlinkingMessage;
                    } else if (!strcmp(field, "MD") || !strcmp(field, "SM")) {  // Message défilant/Scrolling message
                        agendaTable[index].lineType = typeScrollingMessage;
                    } else if (!strcmp(field, "HS") || !strcmp(field, "OOS")) { // Hors Service/Out of service
                        agendaTable[index].lineType = typeOutOfService;
                    } else {
                        return signalError(105, 2, field);
                    }
                } else if (fieldCount == 3) {                       // City
                    if (agendaTable[index].lineType == typeArrival || agendaTable[index].lineType == typeDeparture) {
                        setMessage(agendaTable[index].message, field, CITY_OFFSET, CITY_LENGTH);
                    } else if (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService) {
                        myStrncpy(agendaTable[index].message, field, LINE_CHARACTERS+1);
                    }
                } else if (fieldCount == 4) {                       // Track
                    if (agendaTable[index].lineType == typeArrival || agendaTable[index].lineType == typeDeparture) {
                        setMessage(agendaTable[index].message, field, TRACK_OFFSET, TRACK_LENGTH);
                    } else if (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService) {
                        errorCode = checkColorRange(field, fieldCount, &agendaTable[index].textColor, 
                            (uint16_t) 0, (uint16_t) 65535, (uint16_t) ST7735_WHITE);
                        if (errorCode) return errorCode;
                    }
                } else if (fieldCount == 5) {                       // Train #
                    if (agendaTable[index].lineType == typeArrival || agendaTable[index].lineType == typeDeparture) {
                        setMessage(agendaTable[index].message, field, TRAIN_OFFSET, TRAIN_LENGTH);
                    } else if (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService) {
                        errorCode = checkColorRange(field, fieldCount, &agendaTable[index].backgroundColor, 
                            (uint16_t) 0, (uint16_t) 65535, (uint16_t) ST7735_BLACK);
                        if (errorCode) return errorCode;
                    }
                }
            }
            field = strtok(NULL, separator);                        // Get next field
        }
        // Check for field count
        if (fileFormat == agendaFileFormat) {
            if (fieldCount == 2
                    && (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService)) {
                agendaTable[index].message[0] = 0;
                agendaTable[index].textColor = ST7735_WHITE;
                agendaTable[index].backgroundColor = ST7735_BLACK;
            } else if (fieldCount == 3
                    && (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService)) {
                agendaTable[index].textColor = ST7735_WHITE;
                agendaTable[index].backgroundColor = ST7735_BLACK;
            } else if (fieldCount == 4
                    && (agendaTable[index].lineType == typeFixedMessage 
                            || agendaTable[index].lineType == typeBlinkingMessage
                            || agendaTable[index].lineType == typeScrollingMessage
                            || agendaTable[index].lineType == typeOutOfService)) {
                agendaTable[index].backgroundColor = ST7735_BLACK;
            } else if (fieldCount != 5) {
                return signalError(104, fieldCount);
            }
        }
    }
    return 0;
}

// Wait until event queue empty for 100 ms max (return true if timeout occured)
bool waitForEventsEmpty(void) {
    int loopCount = 0;
    // Wait for enpty event queue for 100 ms
    while (events.avgPacketsWaiting() && loopCount < 100) {
        delay(1);
        loopCount++;
    }
    return (events.avgPacketsWaiting());
}

//  Load all data from configuration file (Error management)
int loadAgenda(void) {
    if (traceEnter) enterRoutine(__func__);
    unsigned long startTime = millis();
    agendaError = loadAgendaDetails();
    if (agendaError) {
        char buffer[LINE_CHARACTERS+1] = {0};
        #ifdef VERSION_FRANCAISE
            trace_error_P("Erreur %d dans %s - Simulation impossible", agendaError, fileToStart.c_str());
            snprintf_P(buffer, sizeof(buffer), "Erreur %d dans %s", agendaError, fileToStart.c_str());
        #else
            trace_error_P("Error %d loading %s - No actions will be done!", agendaError, fileToStart.c_str());
            snprintf_P(buffer, sizeof(buffer), "Error %d in %s", agendaError, fileToStart.c_str());
        #endif
        setDefaultOutOfService(buffer);
    } else {
        #ifdef VERSION_FRANCAISE
            trace_info_P("%s chargé en %d ms", fileToStart.c_str(), millis() - startTime);
        #else
            trace_info_P("%s loaded in %d ms", fileToStart.c_str(),millis() - startTime);
        #endif
        clearDisplay();
        setOutOfService(emptyChar);
    }
    updateWebServerData();
    return agendaError;
}

//  Load all data from configuration file (Do the job)
int loadAgendaDetails(void) {
    int errorCode = 0;

    agendaCount = 0;
    agendaError = 0;
    agendaPreviousTime = 0;

    errorCode = readFile(fileToStart.c_str(), &readAllHeaders);     // Load all headers
    if (errorCode) return errorCode;

    // Check if all headers have been found
    if (!agendaCount) {
        return signalError(102, 0, agendaName);
    }

    delete[] agendaTable;                                           // Clear tables

    agendaTable = new agendaTable_s[agendaCount];                   // Create agenda table
    memset(agendaTable, 0, agendaCount * sizeof(agendaTable_s));

    errorCode = readFile(fileToStart.c_str(), &readAllTables);      // Load all tables
    if (errorCode) return errorCode;

    checkAgenda();

    if (traceTable) {
        trace_debug_P("Row Flg Time Text Fore Message (%s)", agendaName);
        for (int i = 0; i < agendaCount; i++) {
            trace_debug_P("%3d %3d %4d %04x %04x %s", i,
                agendaTable[i].lineType, agendaTable[i].time,
                agendaTable[i].textColor, agendaTable[i].backgroundColor,
                agendaTable[i].message);
            waitForEventsEmpty();                                   // Wait for 100 ms or event queue empty
        }
    }
    return 0;
}

// Check agenda for warnings/errors/inconsistencies
void checkAgenda(void){
    if (traceEnter) enterRoutine(__func__);
}

//          -------------------------------------
//          ---- Program initialization code ----
//          -------------------------------------

// Setup routine
void setup(void) {
    traceEnter = true;
    if (traceEnter) enterRoutine(__func__);
    logSetup();                                                     // Init log
    traceSetup();                                                   // Register trace
    #ifdef ESP8266
        Serial.begin(74880);
    #else
        Serial.begin(115200);
    #endif
    Serial.setDebugOutput(false);                                   // To allow Serial.swap() to work properly

    Serial.println(emptyChar);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Initialise %s V%s", __FILENAME__, VERSION);
    #else
        trace_info_P("Initializing %s V%s", __FILENAME__, VERSION);
    #endif
    resetCause = getResetCause();                                   // Get reset cause
    trace_info_P("Cause : %s", resetCause.c_str());

    #ifdef ESP32
        // Stop Blutooth
        btStop();
    #endif

    displaySetup();                                                 // Start LDC
    yield();

    // Starts flash file system
    LittleFS.begin();

    #define DUMP_FILE_SYSTEM
    #ifdef DUMP_FILE_SYSTEM
        String path = "/";
        #ifdef ESP32
            File dir = LittleFS.open(path);
            #ifdef VERSION_FRANCAISE
                trace_info_P("Contenu flash", NULL);
            #else
                trace_info_P("FS content", NULL);
            #endif
            File entry = dir.openNextFile();
            while(entry){
        #else
            Dir dir = LittleFS.openDir(path);
            path = String();
            #ifdef VERSION_FRANCAISE
                trace_info_P("Contenu flash", NULL);
            #else
                trace_info_P("FS content", NULL);
            #endif
            while(dir.next()){
                fs::File entry = dir.openFile("r");
        #endif
                String fileName = String(entry.name());
                #ifdef ESP32
                    fileName = path + fileName;
                #endif
                trace_info_P("%s", fileName.c_str());
                #ifdef ESP32
                    entry = dir.openNextFile();
                #else
                    entry.close();
                #endif
                }
        #ifdef ESP32
            dir.close();
        #endif
    #endif

    // Load preferences
    if (!readSettings()) {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Pas de configuration, stop !", NULL);
            setDefaultOutOfService((char*) "PAS DE CONFIG !");
        #else
            trace_error_P("No settings, stopping!", NULL);
            setDefaultOutOfService((char*) "NO SETTINGS!");
        #endif
        refreshPanel();
        while (true) {
            yield();
        }
    };
    yield();

    hostName = espName;                                             // Set host name to ESP name
    hostName.replace(" ", "-");                                     // Replace spaces by dashes
    WiFi.hostname(hostName.c_str());                                // Define this module name for client network
    WiFi.setAutoReconnect(true);                                    // Reconnect automatically

    #ifdef ESP32
        WiFi.IPv6(false);                                           // Disable IP V6 for AP
    #endif

    #ifdef ESP32
        WiFi.onEvent(onWiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(onWiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        WiFi.onEvent(onWiFiStationGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    #endif
    #ifdef  ESP8266
        onStationModeConnectedHandler = WiFi.onStationModeConnected(&onWiFiStationConnected); // Declare connection callback
        onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected(&onWiFiStationDisconnected); // Declare disconnection callback
        onStationModeGotIPHandler = WiFi.onStationModeGotIP(&onWiFiStationGotIp); // Declare got IP callback
    #endif

    ssid.trim();
    if (ssid != emptyChar) {                                               // If SSID is given, try to connect to
        #ifdef ESP32
            WiFi.mode(WIFI_MODE_STA);                               // Set station mode
        #endif
        #ifdef ESP8266
            WiFi.mode(WIFI_STA);                                    // Set station mode
        #endif
        #ifdef VERSION_FRANCAISE
            trace_info_P("Recherche %s", ssid.c_str());
        #else
            trace_info_P("Searching %s", ssid.c_str());
        #endif
        WiFi.begin(ssid.c_str(), pwd.c_str());                      // Start to connect to existing SSID
        uint16_t loopCount = 0;
        while (WiFi.status() != WL_CONNECTED && loopCount < 10) {   // Wait for connection for 10 seconds
            delay(1000);                                            // Wait for 1 s
            loopCount++;
        }                                                           // Loop
        if (WiFi.status() == WL_CONNECTED) {                        // If we're not connected
            #ifdef VERSION_FRANCAISE
                trace_info_P("Connexion à %s par http://%s/ ou http://%s/ ",
                    ssid.c_str(), WiFi.localIP().toString().c_str(), hostName.c_str());
            #else
                trace_info_P("Connect to %s with http://%s/ or http://%s/ ",
                    ssid.c_str(), WiFi.localIP().toString().c_str(), hostName.c_str());
            #endif
        } else {
            #ifdef VERSION_FRANCAISE
                trace_info_P("Pas connecté, passe en mode point d'accès ...", NULL);
            #else
                trace_info_P("Not connected, starting access point...", NULL);
            #endif
        }
    }

    if (WiFi.status() != WL_CONNECTED) {                            // If not connected, start access point
        #ifdef ESP32
            WiFi.mode(WIFI_MODE_AP);                                // Set access point mode
        #endif
        #ifdef ESP8266
            WiFi.mode(WIFI_AP);                                     // Set access point mode
        #endif
        char buffer[80];
        // Load this Wifi access point name as ESP name plus ESP chip Id
        #ifdef ESP32
            snprintf_P(buffer, sizeof(buffer),PSTR("%s_%X"), hostName.c_str(), getChipId());
        #else
            snprintf_P(buffer, sizeof(buffer),PSTR("%s_%X"), hostName.c_str(), ESP.getChipId());
        #endif
        checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
        #ifdef VERSION_FRANCAISE
            trace_info_P("Creation du point d'accès %s (%s)", buffer, accessPointPwd.c_str());
        #else
            trace_info_P("Creating %s access point (%s)", buffer, accessPointPwd.c_str());
        #endif
        WiFi.softAP(buffer, accessPointPwd.c_str());                // Starts Wifi access point
        #ifdef VERSION_FRANCAISE
            trace_info_P("Connexion à %s par http://%s/", buffer, WiFi.softAPIP().toString().c_str());
            snprintf_P(buffer, sizeof(buffer), "Point d'accès %s actif (%s)",
                ssid.c_str(), WiFi.softAPIP().toString().c_str());
        #else
            trace_info_P("Connect to %s with http://%s/", buffer, WiFi.softAPIP().toString().c_str());
            snprintf_P(buffer, sizeof(buffer), "WiFi access point %s active (%s)",
                ssid.c_str(), WiFi.softAPIP().toString().c_str());
        #endif
        checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
        wifiState = String(buffer);
    }

    updateWebServerData();

    // Start syslog
    syslogSetup();                                                  // Init log

    // OTA trace
    ArduinoOTA.onStart(onStartOTA);
    ArduinoOTA.onEnd(onEndOTA);
    ArduinoOTA.onError(onErrorOTA);

    //ArduinoOTA.setPassword("my OTA password");                    // Uncomment to set an OTA password
    ArduinoOTA.begin();                                             // Initialize OTA

    #ifdef VERSION_FRANCAISE
        trace_info_P("%s V%s lancé", __FILENAME__, VERSION);
        trace_info_P("Cause : %s", resetCause.c_str());
    #else
        trace_info_P("Starting %s V%s", __FILENAME__, VERSION);
        trace_info_P("Reset cause: %s", resetCause.c_str());
    #endif

    // List of URL to be intercepted and treated locally before a standard treatment
    //  These URL can be used as API
    webServer.on("/status", HTTP_GET, statusReceived);              // /status request
    webServer.on("/setup", HTTP_GET, setupReceived);                // /setup request
    webServer.on("/command", HTTP_GET, commandReceived);            // /command request
    webServer.on("/languages", HTTP_GET, languagesReceived);        // /languages request
    webServer.on("/configs", HTTP_GET, configsReceived);            // /configs received
    webServer.on("/settings", HTTP_GET, settingsReceived);          // /settings request
    webServer.on("/debug", HTTP_GET, debugReceived);                // /debug request
    webServer.on("/rest", HTTP_GET, restReceived);                  // /rest request
    webServer.on("/log", HTTP_GET, logReceived);                    // /log request
    webServer.on("/upload", HTTP_POST, startUpload, handleUpload);  // /download received
    webServer.on("/tables", HTTP_GET, tableReceived);               // /tables received
    // These URL are used internally by setup.htm - Use them at your own risk!
    webServer.on("/changed", HTTP_GET, setChangedReceived);         // /changed request

    // Other webserver stuff
    webServer.addHandler(&events);                                  // Define web events
    webServer.addHandler(new LittleFSEditor());                     // Define file system editor
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.htm"); // Serve "/", default page = index.htm
    webServer.onNotFound (notFound);                                // To be called when URL is not known

    events.onConnect([](AsyncEventSourceClient *client){            // Routine called when a client connects
        #ifdef VERSION_FRANCAISE
            trace_debug_P("Client connecté", NULL);
        #else
            trace_debug_P("Event client connected", NULL);
        #endif
        // Set send an update flag
        sendAnUpdateFlag = true;
        // Send last log lines
        for (uint16_t i=0; i < LOG_MAX_LINES; i++) {
            String logLine = getLogLine(i, true);
            if (logLine != emptyChar) {
                client->send(logLine, "info");
            }
        }
        char msg[20];
        snprintf_P(msg, sizeof(msg),"%016lx", millis());
        client->send(msg, "time");
    });

    events.onDisconnect([](AsyncEventSourceClient *client){         // Routine called when a client connects
        #ifdef VERSION_FRANCAISE
            trace_debug_P("Client déconnecté", NULL);
        #else
            trace_debug_P("Event client disconnected", NULL);
        #endif
    });

    webServer.begin();                                              // Start Web server
    loadAgenda();
    startDisplay();                                                 // Start displays when booting

    #ifdef VERSION_FRANCAISE
        trace_info_P("Fin lancement", NULL);
    #else
        trace_info_P("Init done", NULL);
    #endif
}

//      ------------------------
//      ---- Permanent loop ----
//      ------------------------

// Main loop
void loop(void) {
    displayLoop();                                                   // Work with LEDs
    uploadLoop();                                                   // Check for an uploaded file
    ArduinoOTA.handle();                                            // Give hand to OTA
    #ifdef SERIAL_COMMANDS
        serialLoop();                                               // Scan for serial commands
    #endif
    // Send an update to clients if needed
    if (sendAnUpdateFlag) {
        sendWebServerUpdate();                                      // Send updated data to clients
    }
    #ifdef FF_TRACE_USE_SYSLOG
        if ((micros() - lastSyslogMessageMicro) > 600000000) {      // Last syslog older than 10 minutes?
            #ifdef VERSION_FRANCAISE
               trace_info_P("Toujours vivant ...", NULL);
            #else
               trace_info_P("I'm still alive...", NULL);
            #endif
        }
    #endif
    if (restartMe) {
        #ifdef VERSION_FRANCAISE
            trace_info_P("Relance l'ESP ...", NULL);
        #else
            trace_info_P("Restarting ESP ...", NULL);
        #endif
        delay(1000);
        ESP.restart();
    }
    delay(1);
}