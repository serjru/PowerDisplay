#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Install ArduinoJson library via PlatformIO
#include "epd_driver.h"
#include "firasans.h"
#include <time.h>

// Wi-Fi credentials
const char *ssid = "ubi5";
const char *password = "ortoclas";

// Wi-Fi hostname
const char *host = "lilygo";

// Configuration structure for Home Assistant
struct HomeAssistantConfig {
    const char* host = "192.168.88.138";  // Home Assistant IP
    const int port = 8123;                // Home Assistant port
    const char* token = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIxZmRmNWM2MjkyMDQ0ZDRhYjU0YmJjYmQ3Zjk4Y2QxNiIsImlhdCI6MTczMDU1NzIzMCwiZXhwIjoyMDQ1OTE3MjMwfQ.1MWtEUeTWBCm6IHP7UsfbuDx5rztzHA-ZhjXvgeGjrs";
};

// Structure to hold sensor data
struct SensorData {
    const char* entity_id;    // Home Assistant entity ID
    String state;            // Current state value
    String unit;            // Unit of measurement (optional)
    bool valid;             // Indicates if the data is valid
};

// Define your sensors
SensorData sensors[] = {
    {"sensor.inverter_solar_power", "", "W", false},
    {"sensor.inverter_output_power", "", "W", false},
    {"sensor.inverter_battery_state", "", "%", false}
};

// Display text area configurations
const Rect_t line1Area = {
    .x = 0,
    .y = 0,
    .width = 960,
    .height = 51,
};

// Structure to define text positioning and style
struct TextArea {
    int x;
    int y;
    int width;
    int height;
};

// Display area configurations
const Rect_t headerArea = { .x = 0, .y = 10, .width = 960, .height = 50 };
const Rect_t progressBarArea = { .x = 0, .y = 60, .width = 960, .height = 100 };
const Rect_t valueArea = { .x = 0, .y = 200, .width = 960, .height = 50 };

uint8_t *framebuffer;

class HomeAssistantClient {
private:
    HomeAssistantConfig config;
    HTTPClient http;

    String buildUrl(const char* entity_id) {
        return String("http://") + config.host + ":" + String(config.port) + 
               "/api/states/" + entity_id;
    }

public:
    HomeAssistantClient(const HomeAssistantConfig& conf) : config(conf) {}

    bool fetchSensorData(SensorData& sensor) {
        String url = buildUrl(sensor.entity_id);
        
        http.begin(url);
        http.addHeader("Authorization", "Bearer " + String(config.token));
        http.addHeader("Content-Type", "application/json");
        
        int httpCode = http.GET();
        bool success = false;
        
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(1024);
            
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

    bool fetchAllSensors() {
        bool allSuccess = true;
        for (auto& sensor : sensors) {
            if (!fetchSensorData(sensor)) {
                allSuccess = false;
            }
            delay(100);  // Small delay between requests
        }
        return allSuccess;
    }

    // Helper method to get sensor value as float
    float getSensorValue(const char* entity_id) {
        for (const auto& sensor : sensors) {
            if (strcmp(sensor.entity_id, entity_id) == 0) {
                return sensor.valid ? sensor.state.toFloat() : 0.0f;
            }
        }
        return 0.0f;
    }
};

void displayData(String data) {
    int32_t cursor_x = line1Area.x;
    int32_t cursor_y = line1Area.y + FiraSans.advance_y + FiraSans.descender;

    epd_clear_area(line1Area);

    String displayText = "Home Assistant Data: " + data;
    writeln((GFXfont *)&FiraSans, displayText.c_str(), &cursor_x, &cursor_y, NULL);
}

void drawParameter(const char* label, const String& value, const char* unit, const TextArea& area) {
    // Clear the area first
    Rect_t clearArea = {
        .x = area.x,
        .y = area.y,
        .width = area.width,
        .height = area.height
    };
    epd_clear_area(clearArea);

    // Calculate text positions
    int32_t cursor_x = area.x + 10;  // Initial padding
    int32_t cursor_y = area.y + FiraSans.advance_y + FiraSans.descender;

    // Prepare the complete text with proper formatting
    String displayText;
    if (unit != nullptr && strlen(unit) > 0) {
        displayText = String(label) + ": " + value + " " + unit;
    } else {
        displayText = String(label) + ": " + value;
    }

    // Draw the text
    writeln((GFXfont *)&FiraSans, displayText.c_str(), &cursor_x, &cursor_y, NULL);
}

void drawHeader(const char *text) {
    int32_t cursor_x = headerArea.x + 10;
    int32_t cursor_y = headerArea.y + FiraSans.advance_y + FiraSans.descender;
    epd_clear_area(headerArea);
    writeln((GFXfont *)&FiraSans, text, &cursor_x, &cursor_y, NULL);
}

void drawProgressBar(float value, int maxValue, int majorStep, int minorStep, const Rect_t &barArea) {
    // Configuration parameters
    const int BORDER_WIDTH = 5;          // Border thickness in pixels
    const int INNER_SPACING = 5;         // Space between border and bar
    const int SCALE_MAJOR_WIDTH = 3;     // Width of 1000-unit marks
    const int SCALE_MINOR_WIDTH = 2;     // Width of 500-unit marks
    const float SCALE_MAJOR_HEIGHT = 0.5; // 50% of bar height
    const float SCALE_MINOR_HEIGHT = 0.25;// 25% of bar height
    const int SCALE_MAJOR_STEP = 1000;   // Major scale step (1000 units)
    const int SCALE_MINOR_STEP = 500;    // Minor scale step (500 units)
    
    // Ensure value is within bounds
    value = constrain(value, 0, maxValue);
    
    // Calculate inner bar dimensions (accounting for border and spacing)
    Rect_t innerBar = {
        .x = barArea.x + BORDER_WIDTH + INNER_SPACING,
        .y = barArea.y + BORDER_WIDTH + INNER_SPACING,
        .width = barArea.width - 2 * (BORDER_WIDTH + INNER_SPACING),
        .height = barArea.height - 2 * (BORDER_WIDTH + INNER_SPACING)
    };
    
    // Calculate bar width (from left to right)
    int barWidth = (innerBar.width * value) / maxValue;
    
    // First clear the entire progress bar area
    epd_clear_area(barArea);
    
    // Draw outer border
    // Top border
    epd_fill_rect(barArea.x, barArea.y, 
                  barArea.width, BORDER_WIDTH, 0x00, framebuffer);
    // Bottom border
    epd_fill_rect(barArea.x, barArea.y + barArea.height - BORDER_WIDTH, 
                  barArea.width, BORDER_WIDTH, 0x00, framebuffer);
    // Left border
    epd_fill_rect(barArea.x, barArea.y, 
                  BORDER_WIDTH, barArea.height, 0x00, framebuffer);
    // Right border
    epd_fill_rect(barArea.x + barArea.width - BORDER_WIDTH, barArea.y, 
                  BORDER_WIDTH, barArea.height, 0x00, framebuffer);
    
    // Draw the filled (black) part of the bar from left side
    epd_fill_rect(innerBar.x, 
                  innerBar.y,
                  barWidth,
                  innerBar.height,
                  0x00,
                  framebuffer);
    
    // Draw the empty (white) part of the bar
    epd_fill_rect(innerBar.x + barWidth,
                  innerBar.y,
                  innerBar.width - barWidth,
                  innerBar.height,
                  0xFF,
                  framebuffer);
    
    // Draw scale marks
    for (int unit = 0; unit <= maxValue; unit += minorStep) {
        // Calculate x position for this mark
        int markX = innerBar.x + (unit * innerBar.width / maxValue);
        
        // Determine mark properties based on if it's a major or minor mark
        bool isMajorMark = (unit % majorStep == 0);
        int markWidth = isMajorMark ? SCALE_MAJOR_WIDTH : SCALE_MINOR_WIDTH;
        float heightRatio = isMajorMark ? SCALE_MAJOR_HEIGHT : SCALE_MINOR_HEIGHT;
        int markHeight = barArea.height * heightRatio;
        
        // Center the mark on its position
        markX -= markWidth / 2;
        
        // Draw the mark from the bottom of the progress bar
        epd_fill_rect(markX,
                     barArea.y + barArea.height - markHeight,
                     markWidth,
                     markHeight,
                     0x00,
                     framebuffer);
    }
}

void drawValue(String value) {
    int32_t cursor_x = valueArea.x + (valueArea.width / 2) - ((FiraSans.advance_y / 2) * value.length());
    int32_t cursor_y = valueArea.y + FiraSans.advance_y + FiraSans.descender;

    // Clear the area and display the centered value
    epd_clear_area(valueArea);
    int roundedValue = round(value.toFloat());
    String displayText = String(roundedValue) + " W";
    writeln((GFXfont *)&FiraSans, displayText.c_str(), &cursor_x, &cursor_y, NULL);
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    // Initialize the display
    epd_init();
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("Memory allocation failed for framebuffer!");
        while (1); // Stop if memory allocation fails
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2); // Set display to white
    epd_poweron();
    epd_clear();

    // Display connection message
    int32_t cursor_x = line1Area.x;
    int32_t cursor_y = line1Area.y + FiraSans.advance_y + FiraSans.descender;
    //const char *connectionMessage = "Connecting to Wi-Fi" + ssid;
    char connectionMessage[50]; // Adjust the size as needed
    sprintf(connectionMessage, "Connecting to Wi-Fi %s", ssid);
    writeln((GFXfont *)&FiraSans, connectionMessage, &cursor_x, &cursor_y, NULL);

    // Wi-Fi connection
    WiFi.disconnect();
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.printf("Connecting to %s...\n", ssid);

    // Wait for Wi-Fi to connect
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi connected.");

    // Initialize Home Assistant client
    HomeAssistantConfig ha_config;
    HomeAssistantClient ha_client(ha_config);

    // Set NTP server and timezone
    configTime(0, 0, "pool.ntp.org");  // NTP server to fetch UTC time
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Central European Time (adjust for your timezone)
    tzset();

    // Wait until time is synced
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    // // Display IP and current date/time on the screen
    // epd_clear_area(line1Area);
    // cursor_x = line1Area.x;
    // cursor_y = line1Area.y + FiraSans.advance_y + FiraSans.descender;

    // String ipMessage = "IP Address: " + WiFi.localIP().toString();
    // writeln((GFXfont *)&FiraSans, ipMessage.c_str(), &cursor_x, &cursor_y, NULL);

    // // Display current date and time
    // cursor_x = line1Area.x;
    // cursor_y += FiraSans.advance_y + FiraSans.descender;
    // char timeString[64];
    // strftime(timeString, sizeof(timeString), "Date: %Y-%m-%d %H:%M:%S", &timeinfo);
    // writeln((GFXfont *)&FiraSans, timeString, &cursor_x, &cursor_y, NULL);

    // Fetch all sensor data
    ha_client.fetchAllSensors();

    // Use the data
    float solarPower = ha_client.getSensorValue("sensor.inverter_solar_power");
    float outputPower = ha_client.getSensorValue("sensor.inverter_output_power");
    float batteryState = ha_client.getSensorValue("sensor.inverter_battery_state");

    // Define your text areas
    const TextArea solarPowerArea = { .x = 0, .y = 10, .width = 960, .height = 50 };
    const TextArea outputPowerArea = { .x = 0, .y = 170, .width = 960, .height = 50 };
    const TextArea batteryStateArea = { .x = 0, .y = 340, .width = 960, .height = 50 };



    // Draw header, progress bar, and value based on sensor data
    drawParameter("Solar Power", String(round(solarPower)), "W", solarPowerArea);
    drawProgressBar(solarPower, 4000, 1000, 500, progressBarArea);

    // For current power consumption (0-4000 w)
    drawParameter("Output Power", String(round(outputPower)), "W", outputPowerArea);
    Rect_t consumptionBarArea = { .x = 0, .y = 220, .width = 960, .height = 100 };
    drawProgressBar(outputPower, 4000, 1000, 500, consumptionBarArea);

    // For battery percentage (0-100%)
    drawParameter("Battery State", String(round(batteryState)), "%", batteryStateArea);
    Rect_t batteryBarArea = { .x = 0, .y = 390, .width = 960, .height = 100 };
    drawProgressBar(batteryState, 100, 10, 5, batteryBarArea);


    // Update display
    epd_draw_grayscale_image(epd_full_screen(), framebuffer); // Add this line

    epd_poweroff();

    // Halt further execution
    while (1);  
}

void loop() {
    // Nothing to do here for now
}