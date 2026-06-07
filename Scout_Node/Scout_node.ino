#include <Forest_Guard_inferencing.h>

//#include <soundai_inferencing.h>
#include <driver/i2s.h>
#include <SPI.h>
#include <LoRa.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>

// ==========================================
// 1. HARDWARE PINS
// ==========================================
// Wake-Up Sensors
#define PIN_MQ2_GAS 1 
#define PIN_KY037_SOUND 2

// I2S Microphone
#define I2S_SCK 4  
#define I2S_WS  5  
#define I2S_SD  6  

// RTC (I2C)
#define I2C_SDA 8
#define I2C_SCL 9

// SD Card (FSPI)
#define SD_MOSI 11
#define SD_MISO 13
#define SD_SCK  12
#define SD_CS   10

// LoRa (HSPI)
#define LORA_MOSI 14
#define LORA_MISO 15
#define LORA_SCK  16
#define LORA_CS   17
#define LORA_RST  18
#define LORA_DIO0 7

#define NODE_ID "NODE_2" 
#define MAX_MEMORY 10
String recent_messages[MAX_MEMORY];
int memory_index = 0;

String SECRET_KEY = "NSBM2026";

// ==========================================
// 2. GLOBAL SETTINGS & BUFFERS
// ==========================================
SPIClass sdSPI(FSPI);
SPIClass loraSPI(HSPI);
RTC_DS3231 rtc;

#define SAMPLE_RATE 16000
#define AI_BUFFER_SIZE 16000 // 1 second of audio

int16_t ai_audio_buffer[AI_BUFFER_SIZE];
int32_t i2sBuffer[256];

// ==========================================
// 3. VIVA-FRIENDLY HELPER FUNCTIONS
// ==========================================

// Gets a clean timestamp string from the RTC
String getTimestamp() {
    DateTime now = rtc.now();
    char buf[25];
    sprintf(buf, "%04d%02d%02d_%02d%02d%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    return String(buf);
}

// Feeds audio buffer to Edge Impulse
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)ai_audio_buffer[offset + i];
    }
    return 0;
}

// Records exactly 1 second of fresh audio from the microphone
void recordOneSecondClip() {
    // Flush out any old/stale audio from before the ESP32 went to sleep
    i2s_zero_dma_buffer(I2S_NUM_0);
    
    int samples_recorded = 0;
    size_t bytesRead;
    
    Serial.println("Recording 1 second clip...");
    while(samples_recorded < AI_BUFFER_SIZE) {
        i2s_read(I2S_NUM_0, i2sBuffer, sizeof(i2sBuffer), &bytesRead, portMAX_DELAY);
        int new_samples = bytesRead / 4;
        for(int i = 0; i < new_samples && samples_recorded < AI_BUFFER_SIZE; i++) {
            ai_audio_buffer[samples_recorded++] = i2sBuffer[i] >> 15;
        }
    }
}

// Packages the audio array into a playable .WAV file and saves it to the SD card
void saveWavToSD(String threatType, String timestamp) {
    String filename = "/" + threatType + "_" + timestamp + ".wav";
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("ERR: Could not create WAV on SD.");
        return;
    }

    // 44-Byte WAV Header format
    uint32_t sampleRate = 16000;
    uint32_t byteRate = 16000 * 2; // SampleRate * NumChannels * BitsPerSample/8
    uint32_t dataSize = AI_BUFFER_SIZE * 2;
    uint32_t chunkSize = 36 + dataSize;

    byte header[44] = {
        'R','I','F','F',
        (byte)(chunkSize & 0xff), (byte)((chunkSize >> 8) & 0xff), (byte)((chunkSize >> 16) & 0xff), (byte)((chunkSize >> 24) & 0xff),
        'W','A','V','E',
        'f','m','t',' ',
        16, 0, 0, 0, 1, 0, 1, 0,
        (byte)(sampleRate & 0xff), (byte)((sampleRate >> 8) & 0xff), (byte)((sampleRate >> 16) & 0xff), (byte)((sampleRate >> 24) & 0xff),
        (byte)(byteRate & 0xff), (byte)((byteRate >> 8) & 0xff), (byte)((byteRate >> 16) & 0xff), (byte)((byteRate >> 24) & 0xff),
        2, 0, 16, 0,
        'd','a','t','a',
        (byte)(dataSize & 0xff), (byte)((dataSize >> 8) & 0xff), (byte)((dataSize >> 16) & 0xff), (byte)((dataSize >> 24) & 0xff)
    };

    file.write(header, 44);
    file.write((const uint8_t*)ai_audio_buffer, AI_BUFFER_SIZE * 2);
    file.close();
    Serial.println("   -> Audio saved: " + filename);
}

// Broadcasts the alert via LoRa
void transmitAlert(String threatType) {
    String timestamp = getTimestamp();
    
    // 1. Build the plain-text sentence
    String cleanPayload = "ALERT|" + threatType + "|" + timestamp + "|" + String(NODE_ID);
    
    // 2. Encrypt it into secure garbage!
    String encryptedPayload = scramblePayload(cleanPayload);

    // 3. Jitter: Wait randomly to avoid airwave collisions
    delay(random(660, 780)); 

    // 4. Broadcast the garbage
    LoRa.beginPacket();
    LoRa.print(encryptedPayload);
    LoRa.endPacket();
    
    // 5. Cache the garbage so we don't repeat our own echo
   // isNewMessage(encryptedPayload); 
    
    Serial.println("   -> [SECURE] Encrypted warning broadcasted!");
}

// Verifies if the MQ2 gas sensor detects smoke within a 3-second window
bool verifyFireWithGas() {
    Serial.println("AI detected Fire! Checking MQ2 sensor for 5 seconds...");
    unsigned long startTime = millis();
    
    // Check for 3000 milliseconds
    while (millis() - startTime < 5000) {
        // If MQ2 drops to 0 (LOW), smoke is confirmed!
        if (digitalRead(PIN_MQ2_GAS) == LOW) {
            Serial.println("Smoke verified!");
            return true;
        }
        delay(100); 
    }
    Serial.println("No smoke detected. False alarm discarded.");
    return false;
}

// The XOR Cipher: Scrambles and Unscrambles text
String scramblePayload(String payload) {
    String processed = "";
    for (int i = 0; i < payload.length(); i++) {
        processed += (char)(payload[i] ^ SECRET_KEY[i % SECRET_KEY.length()]);
    }
    return processed;
}

// 1. Checks if we've seen this packet before
bool isNewMessage(String msgID) {
    for (int i = 0; i < MAX_MEMORY; i++) {
        if (recent_messages[i] == msgID) return false; 
    }
    recent_messages[memory_index] = msgID;
    memory_index = (memory_index + 1) % MAX_MEMORY; 
    return true; 
}

void processIncomingMeshPacket() {
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String rawEncryptedPacket = "";
        while (LoRa.available()) {
            rawEncryptedPacket += (char)LoRa.read();
        }
        
        Serial.println("   -> [MESH] Caught encrypted packet.");

        String decrypted = scramblePayload(rawEncryptedPacket);

        // Check if we've seen this exact garbage string before
        if (isNewMessage(decrypted)) {
            
            // Jitter: Wait randomly to avoid collisions with other repeating nodes
            delay(random(10, 500)); 
            
            // Blindly relay the exact same encrypted text!
            LoRa.beginPacket();
            LoRa.print(rawEncryptedPacket); 
            LoRa.endPacket();
            isNewMessage(rawEncryptedPacket); 

            Serial.println("   -> [MESH] Blindly relayed secure packet!");
        } else {
            Serial.println("   -> [MESH] Duplicate packet. Ignored to save battery.");
        }
    }
}


// ==========================================
// 4. SETUP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // A. Start RTC Clock
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!rtc.begin(&Wire)) { Serial.println("ERR: RTC Failed"); while(1); }
    
    // B. Start SD Card (FSPI)
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, sdSPI)) { Serial.println("ERR: SD Card Failed"); }
    
    // C. Start LoRa (HSPI)
    loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
    LoRa.setSPI(loraSPI);
    if (!LoRa.begin(433E6)) { Serial.println("ERR: LoRa Failed"); while(1); } 

    // D. Start Microphone
    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false
    };
    i2s_pin_config_t pins = {
        .bck_io_num = I2S_SCK, .ws_io_num = I2S_WS, 
        .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_SD
    };
    i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pins);

   // E. Configure Light Sleep Wake-Up Triggers

    // 1. Configure Sound Sensor (Idle = 0, Trigger = 1)
    pinMode(PIN_KY037_SOUND, INPUT_PULLDOWN); // Anchor to 0V
    gpio_wakeup_enable((gpio_num_t)PIN_KY037_SOUND, GPIO_INTR_HIGH_LEVEL);

    // 2. Configure Gas Sensor (Idle = 1, Trigger = 0)
    pinMode(PIN_MQ2_GAS, INPUT_PULLUP); // Anchor to 3.3V
    gpio_wakeup_enable((gpio_num_t)PIN_MQ2_GAS, GPIO_INTR_LOW_LEVEL);
    
    // Tell ESP32 that the LoRa chip is allowed to wake it up!
    pinMode(LORA_DIO0, INPUT_PULLDOWN);
    gpio_wakeup_enable((gpio_num_t)LORA_DIO0, GPIO_INTR_HIGH_LEVEL);

    esp_sleep_enable_gpio_wakeup();
}

// ==========================================
// 5. MAIN LOOP (The Master Logic)
// ==========================================
void loop() {
    Serial.println("\n[SLEEP] Entering low-power light sleep...");
    delay(100); // Give serial time to print before sleeping
    
    LoRa.receive();

    // 1. CPU Pauses Here
    esp_light_sleep_start(); 
 
    // CPU Resume Here




     delay(50);

    size_t bytesRead;
    int32_t dump[256];

    // Flush garbage samples
    for (int i = 0; i < 15; i++) {
        i2s_read(I2S_NUM_0, dump, sizeof(dump), &bytesRead, portMAX_DELAY);
    }

    // 2. Record 1 second clip immediately
    recordOneSecondClip();

    // 3. Ask AI what the sound is
    signal_t signal;
    signal.total_length = AI_BUFFER_SIZE;
    signal.get_data = &raw_feature_get_data;
    ei_impulse_result_t result = { 0 };
    run_classifier(&signal, &result, false);

    String topThreat = "background";
    float highestValue = 0;
    
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if(result.classification[ix].value > highestValue) {
            highestValue = result.classification[ix].value;
            topThreat = result.classification[ix].label;
        }
    }

    Serial.printf("AI Result: %s (Confidence: %.0f%%)\n", topThreat.c_str(), highestValue * 100);

    // 4. The Action Logic Matrix
    if (highestValue < 0.60 || topThreat == "Background") {
        Serial.println("Action: False alarm. Returning to sleep.");
         if (digitalRead(LORA_DIO0) == HIGH) {
        // A radio packet woke us up! Relay it.
        processIncomingMeshPacket();
    }else return;
        return; // Exits the loop early and goes straight back to sleep
    } 
    
    else if (topThreat == "Gunshot" || topThreat == "Chainsaw") {
        Serial.println("Action: Logging immediate threat!");
        String ts = getTimestamp();
        saveWavToSD(topThreat, ts);
        transmitAlert(topThreat);
    } 
    
    else if (topThreat == "Fire") {
        if (verifyFireWithGas()) { // Enters the 5-second verification loop
            String ts = getTimestamp();
            saveWavToSD("fire_verified", ts);
            transmitAlert("FIRE");
        }
    }

    if (digitalRead(LORA_DIO0) == HIGH) {
        // A radio packet woke us up! Relay it.
        processIncomingMeshPacket();
    }else{}
    


    // Small delay to prevent the sensors from infinitely re-triggering instantly
    delay(2000); 

}