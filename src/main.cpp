#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "epd_driver.h"
#include "firasans.h"
#include <time.h>
#include "icons.h"  // Include our new icons header
#include "battery_icon.h"  // Include our new icons header


// Wi-Fi and Home Assistant Configuration
struct Config {
    // WiFi settings
    const char* wifi_ssid = "ubi5";
    const char* wifi_password = "ortoclas";
    const char* wifi_hostname = "lilygo";
    
    // Home Assistant settings
    const char* ha_host = "192.168.88.138";
    const int ha_port = 8123;
    const char* ha_token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIxZmRmNWM2MjkyMDQ0ZDRhYjU0YmJjYmQ3Zjk4Y2QxNiIsImlhdCI6MTczMDU1NzIzMCwiZXhwIjoyMDQ1OTE3MjMwfQ.1MWtEUeTWBCm6IHP7UsfbuDx5rztzHA-ZhjXvgeGjrs";

    // Display update interval (in milliseconds)
    const unsigned long update_interval = 10000;
};


// Sensor data structure
struct SensorData {
    const char* entity_id;
    String state;
    String unit;
    bool valid;
    bool is_numeric;  // Add this field to distinguish between numeric and string values
};

// Structure for text positioning (must be defined BEFORE Layout)
struct TextArea {
    int x;
    int y;
    int width;
    int height;
};

// Layout structure for text and graphics positioning
struct Layout {
    // Text areas
    TextArea solarPowerArea;
    TextArea outputPowerArea;
    TextArea batteryStateArea;
    TextArea inverterModeArea;
    
    // Progress bar areas
    Rect_t solarBarArea;
    Rect_t consumptionBarArea;
    Rect_t batteryBarArea;

    Layout() {
        // Initialize text areas
        solarPowerArea   = { 0, 10,  960, 50 };
        outputPowerArea  = { 0, 120, 960, 50 };
        batteryStateArea = { 0, 240, 960, 50 };
        inverterModeArea = { 0, 350, 960, 150 };
        
        // Initialize progress bar areas
        solarBarArea       = { .x = 0, .y = 60,  .width = 960, .height = 50 };
        consumptionBarArea = { .x = 0, .y = 170, .width = 960, .height = 50 };
        batteryBarArea     = { .x = 0, .y = 290, .width = 960, .height = 50 };
    }
};

// Global variables
Config config;
Layout layout;
uint8_t *framebuffer = nullptr;
uint8_t *old_framebuffer = nullptr;
bool first_run = true;

// Define sensors
SensorData sensors[] = {
    {"sensor.inverter_solar_power", "", "W", false, true},
    {"sensor.inverter_output_power", "", "W", false, true},
    {"sensor.inverter_battery_state", "", "%", false, true},
    {"sensor.inverter_actual_mode", "", "", false, false}
};

// Helper function to calculate text bounds
void getTextBounds(GFXfont *font, const char* text, int* width) {
    *width = 0;
    for (const char* p = text; *p != '\0'; p++) {
        if (font->glyph) {
            GFXglyph glyph = font->glyph[(uint8_t)*p];
            *width += glyph.advance_x;
        }
    }
}

// Drawing functions
void drawIcon(const unsigned char* icon, int x, int y, const char* label) {
    // Create a rectangle for clearing the area
    Rect_t clearRect = {
        .x = x,
        .y = y,
        .width = ICON_SIZE + 20,
        .height = ICON_SIZE + FiraSans.advance_y + 20
    };
    
    // Clear the area first (white)
    epd_fill_rect(clearRect.x, clearRect.y, clearRect.width, clearRect.height, 0xFF, framebuffer);
    
    // Draw the icon byte by byte
    for (int row = 0; row < ICON_SIZE; row++) {
        for (int byte_col = 0; byte_col < (ICON_SIZE / 8); byte_col++) {
            uint8_t byte = icon[row * (ICON_SIZE / 8) + byte_col];
            
            // Draw each pixel with its grayscale value
            for (int bit = 0; bit < 8; bit++) {
                int pixel_x = x + (byte_col * 8) + bit;
                int pixel_y = y + row;
                uint8_t pixel_value = byte; // Use the full byte value for grayscale
                epd_draw_pixel(pixel_x, pixel_y, pixel_value, framebuffer);
            }
        }
    }
    
    // Center and draw the label below the icon
    int32_t cursor_x = x;
    int32_t cursor_y = y + ICON_SIZE + 10 + FiraSans.advance_y;
    
    int text_width = 0;
    getTextBounds((GFXfont *)&FiraSans, label, &text_width);
    cursor_x = x + (ICON_SIZE - text_width) / 2;
    
    writeln((GFXfont *)&FiraSans, label, &cursor_x, &cursor_y, NULL);
}

void newDrawIcon(int x, int y, const uint8_t* icon_data, uint32_t width, uint32_t height) {
    Rect_t area = {
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };
    
    epd_copy_to_framebuffer(area, (uint8_t *)icon_data, framebuffer);
}

void drawParameter(const char* label, const String& value, const char* unit, const TextArea& area) {
    Rect_t clearRect = {
        .x = area.x,
        .y = area.y,
        .width = area.width,
        .height = area.height
    };
    epd_fill_rect(clearRect.x, clearRect.y, clearRect.width, clearRect.height, 0xFF, framebuffer);
    
    int32_t cursor_x = area.x + 10;
    int32_t cursor_y = area.y + FiraSans.advance_y + FiraSans.descender;
    
    String displayText = unit && strlen(unit) > 0 ? 
                        String(label) + ": " + value + " " + unit :
                        String(label) + ": " + value;
    
    writeln((GFXfont *)&FiraSans, displayText.c_str(), &cursor_x, &cursor_y, NULL);
}

void drawProgressBar(float value, int maxValue, int majorStep, int minorStep, const Rect_t &barArea) {
    const int BORDER_WIDTH = 5;
    const int INNER_SPACING = 5;
    const int SCALE_MAJOR_WIDTH = 3;
    const int SCALE_MINOR_WIDTH = 2;
    const float SCALE_MAJOR_HEIGHT = 0.5;
    const float SCALE_MINOR_HEIGHT = 0.25;
    
    value = constrain(value, 0, maxValue);
    
    Rect_t innerBar = {
        .x = barArea.x + BORDER_WIDTH + INNER_SPACING,
        .y = barArea.y + BORDER_WIDTH + INNER_SPACING,
        .width = barArea.width - 2 * (BORDER_WIDTH + INNER_SPACING),
        .height = barArea.height - 2 * (BORDER_WIDTH + INNER_SPACING)
    };
    
    int barWidth = (innerBar.width * value) / maxValue;
    
    // Draw outer border
    epd_fill_rect(barArea.x, barArea.y, 
                  barArea.width, BORDER_WIDTH, 0x00, framebuffer);
    epd_fill_rect(barArea.x, barArea.y + barArea.height - BORDER_WIDTH, 
                  barArea.width, BORDER_WIDTH, 0x00, framebuffer);
    epd_fill_rect(barArea.x, barArea.y, 
                  BORDER_WIDTH, barArea.height, 0x00, framebuffer);
    epd_fill_rect(barArea.x + barArea.width - BORDER_WIDTH, barArea.y, 
                  BORDER_WIDTH, barArea.height, 0x00, framebuffer);
    
    // Draw the filled part
    epd_fill_rect(innerBar.x, 
                  innerBar.y,
                  barWidth,
                  innerBar.height,
                  0x00,
                  framebuffer);
    
    // Draw the empty part
    epd_fill_rect(innerBar.x + barWidth,
                  innerBar.y,
                  innerBar.width - barWidth,
                  innerBar.height,
                  0xFF,
                  framebuffer);
    
    // Draw scale marks
    for (int unit = 0; unit <= maxValue; unit += minorStep) {
        int markX = innerBar.x + (unit * innerBar.width / maxValue);
        bool isMajorMark = (unit % majorStep == 0);
        int markWidth = isMajorMark ? SCALE_MAJOR_WIDTH : SCALE_MINOR_WIDTH;
        float heightRatio = isMajorMark ? SCALE_MAJOR_HEIGHT : SCALE_MINOR_HEIGHT;
        int markHeight = barArea.height * heightRatio;
        
        markX -= markWidth / 2;
        
        epd_fill_rect(markX,
                     barArea.y + barArea.height - markHeight,
                     markWidth,
                     markHeight,
                     0x00,
                     framebuffer);
    }
}

// Home Assistant Client Class
class HomeAssistantClient {
private:
    Config& config;
    HTTPClient http;
    
    String buildUrl(const char* entity_id) {
        return String("http://") + config.ha_host + ":" + 
               String(config.ha_port) + "/api/states/" + entity_id;
    }

public:
    HomeAssistantClient(Config& conf) : config(conf) {}

    bool fetchSensorData(SensorData& sensor) {
        String url = buildUrl(sensor.entity_id);
        
        http.begin(url);
        http.addHeader("Authorization", "Bearer " + String(config.ha_token));
        http.addHeader("Content-Type", "application/json");
        
        int httpCode = http.GET();
        bool success = false;
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                sensor.state = doc["state"].as<String>();
                sensor.valid = true;
                success = true;
                Serial.printf("Sensor %s: %s %s\n", 
                            sensor.entity_id, 
                            sensor.state.c_str(), 
                            sensor.unit.c_str());
            } else {
                Serial.printf("JSON parse error for %s: %s\n", 
                            sensor.entity_id, 
                            error.c_str());
            }
        } else {
            Serial.printf("HTTP error for %s: %d\n", 
                        sensor.entity_id, 
                        httpCode);
        }
        
        http.end();
        return success;
    }

    bool fetchAllSensors(SensorData* sensors, size_t sensorCount) {
        bool allSuccess = true;
        for (size_t i = 0; i < sensorCount; i++) {
            if (!fetchSensorData(sensors[i])) {
                allSuccess = false;
            }
            delay(100);
        }
        return allSuccess;
    }

    float getSensorValue(const char* entity_id, SensorData* sensors, size_t sensorCount) {
        for (size_t i = 0; i < sensorCount; i++) {
            if (strcmp(sensors[i].entity_id, entity_id) == 0) {
                return (sensors[i].is_numeric && sensors[i].valid) ? 
                       sensors[i].state.toFloat() : 0.0f;
            }
        }
        return 0.0f;
    }

    String getSensorString(const char* entity_id, SensorData* sensors, size_t sensorCount) {
        for (size_t i = 0; i < sensorCount; i++) {
            if (strcmp(sensors[i].entity_id, entity_id) == 0) {
                return sensors[i].valid ? sensors[i].state : "";
            }
        }
        return "";
    }
};

bool has_changes(const uint8_t *buf1, const uint8_t *buf2, size_t size) {
    return memcmp(buf1, buf2, size) != 0;
}

void setupWiFi() {
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    Serial.printf("Connecting to %s...\n", config.wifi_ssid);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi connected.");

    configTime(0, 0, "pool.ntp.org");
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

// Display update function
void updateDisplay() {
    static float lastSolarPower = -1;
    static float lastOutputPower = -1;
    static float lastBatteryState = -1;
    static String lastInverterMode = "";
    
    // Create Home Assistant client and fetch data
    HomeAssistantClient ha_client(config);
    const size_t SENSOR_COUNT = sizeof(sensors) / sizeof(sensors[0]);
    
    // Fetch all sensor data
    bool needsUpdate = false;
    if (ha_client.fetchAllSensors(sensors, SENSOR_COUNT)) {
        float solarPower = ha_client.getSensorValue("sensor.inverter_solar_power", sensors, SENSOR_COUNT);
        float outputPower = ha_client.getSensorValue("sensor.inverter_output_power", sensors, SENSOR_COUNT);
        float batteryState = ha_client.getSensorValue("sensor.inverter_battery_state", sensors, SENSOR_COUNT);
        String inverterMode = ha_client.getSensorString("sensor.inverter_actual_mode", sensors, SENSOR_COUNT);
        
        if (abs(solarPower - lastSolarPower) >= 100 ||
            abs(outputPower - lastOutputPower) >= 100 ||
            abs(batteryState - lastBatteryState) >= 0.2 ||
            inverterMode != lastInverterMode) {
            
            needsUpdate = true;
            memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
            
            // Draw progress bars
            drawProgressBar(solarPower, 4000, 1000, 500, layout.solarBarArea);
            drawProgressBar(outputPower, 4000, 1000, 500, layout.consumptionBarArea);
            drawProgressBar(batteryState, 100, 10, 5, layout.batteryBarArea);

            // Update display
            epd_poweron();
            epd_clear();

            // Draw parameter values
            drawParameter("Solar Power", String(round(solarPower)), "W", layout.solarPowerArea);
            drawParameter("Output Power", String(round(outputPower)), "W", layout.outputPowerArea);
            drawParameter("Battery State", String(round(batteryState)), "%", layout.batteryStateArea);
            
            // Draw inverter mode icon
            if (inverterMode == "Battery") {
                /*drawIcon(ICON_BATTERY, 
                        layout.inverterModeArea.x + 20, 
                        layout.inverterModeArea.y + 10, 
                        "Working on battery");*/
                newDrawIcon(
                  layout.inverterModeArea.x + 20,  // x position
                  layout.inverterModeArea.y + 10,  // y position
                  battery_icon,                    // icon data
                  icon_width,                      // width
                  icon_height                      // height
                );
            } else if (inverterMode == "Line") {
                drawIcon(ICON_PLUG, 
                        layout.inverterModeArea.x + 20, 
                        layout.inverterModeArea.y + 10, 
                        "Working from line");
            }
            
            // Update last values
            lastSolarPower = solarPower;
            lastOutputPower = outputPower;
            lastBatteryState = batteryState;
            lastInverterMode = inverterMode;
            
            epd_draw_grayscale_image(epd_full_screen(), framebuffer);
            epd_poweroff();
        }
    }
}


void setup() {
    Serial.begin(115200);
    
    epd_init();
    
    size_t framebuffer_size = EPD_WIDTH * EPD_HEIGHT / 2;
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), framebuffer_size);
    old_framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), framebuffer_size);
    
    if (!framebuffer || !old_framebuffer) {
        Serial.println("Memory allocation failed!");
        while (1);
    }
    
    memset(framebuffer, 0xFF, framebuffer_size);
    memset(old_framebuffer, 0xFF, framebuffer_size);
    
    if (first_run) {
        epd_poweron();
        epd_clear();
        first_run = false;
    }
    
    setupWiFi();
    
    while (1) {
        updateDisplay();
        delay(10000);  // Update every 30 seconds
    }
}

void loop() {
    // Empty - main loop is in setup()
}