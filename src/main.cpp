#include "Arduino.h"
#include "driver/i2s.h"
#include "input/input.h"

#define internal                                    static

#define STATUS_LED_PIN_R                            (13U)
#define STATUS_LED_PIN_G                            (12U)
#define STATUS_LED_PIN_B                            (14U)

#define PLAY_BUTTON_PIN                             (23U)
#define RECORD_BUTTON_PIN                           (15U)

#define block_err                                   do { digitalWrite(STATUS_LED_PIN_R, HIGH); } while(true)

button_t play_button, record_button;

#define RING_BUFFER_BLOCK_SIZE                      (1024U)         // @note number of samples
#define RING_BUFFER_BLOCK_COUNT                     (8U)
#define RECORDED_BUFFER_SIZE                        (96U * 1024U)   // @note about 4.5 seconds of audio data
#define RECORDED_SAMPLE_COUNT                       (RECORDED_BUFFER_SIZE / sizeof(int16_t))

int32_t record_buffer[RING_BUFFER_BLOCK_SIZE]       = {0};
int32_t playback_buffer[RING_BUFFER_BLOCK_SIZE]     = {0};

int16_t* recorded_data                              = NULL;         // @note has to be heap allocated, does not fit in data segment
uint32_t recorded_data_size                         = 0;
bool recording                                      = false;

void initialize_devices(void) {

    #define I2S_CLOCK_PIN                           (22U)
    #define I2S_WS_PIN                              (21U)

    #define MICROPHONE_I2S_CLOCK_FREQUENCY          (16000U)
    #define MICROPHONE_I2S_DATA_PIN                 (19U)

    #define AMP_I2S_DATA_PIN                        (2U)
  
    i2s_config_t config = (i2s_config_t) {

        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
        .sample_rate = MICROPHONE_I2S_CLOCK_FREQUENCY,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                                // @note we want mono audio, we also need to short l/r pin to ground
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
        .dma_buf_count = RING_BUFFER_BLOCK_COUNT,
        .dma_buf_len = RING_BUFFER_BLOCK_SIZE, 
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0                                                             // @note we are not using master clock 
    
    };

    i2s_pin_config_t pin_config = {
    
        .bck_io_num = I2S_CLOCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = AMP_I2S_DATA_PIN,
        .data_in_num = MICROPHONE_I2S_DATA_PIN
    
    };

    // @note bind and start i2s clock lines
    i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_start(I2S_NUM_0);

}

void setup(void) {

    // @note begin serial connection
    Serial.begin(115200);

    // @note rgb-led pins
    pinMode(STATUS_LED_PIN_R, OUTPUT);
    pinMode(STATUS_LED_PIN_G, OUTPUT);
    pinMode(STATUS_LED_PIN_B, OUTPUT);

    play_button     = create_button(PLAY_BUTTON_PIN);
    record_button   = create_button(RECORD_BUTTON_PIN);

    // @note device pins    
    pinMode(I2S_CLOCK_PIN,              OUTPUT);
    pinMode(I2S_WS_PIN,                 OUTPUT);
    pinMode(MICROPHONE_I2S_DATA_PIN,    INPUT);
    pinMode(AMP_I2S_DATA_PIN,           OUTPUT);

    digitalWrite(STATUS_LED_PIN_G, HIGH);
    digitalWrite(STATUS_LED_PIN_R, HIGH);

    initialize_devices();

    recorded_data = (int16_t*)malloc(RECORDED_BUFFER_SIZE);
    if (recorded_data == NULL) {
        Serial.println("error: failed to allocated recorded data buffer");
        block_err;
    }

    digitalWrite(STATUS_LED_PIN_R, LOW);
    delay(500);

}

void update_status_led(void) {

    if (recording) {
        digitalWrite(STATUS_LED_PIN_R, HIGH);
        digitalWrite(STATUS_LED_PIN_G, LOW);
        digitalWrite(STATUS_LED_PIN_B, LOW);
        return;
    }

    digitalWrite(STATUS_LED_PIN_G, HIGH);
    digitalWrite(STATUS_LED_PIN_R, is_button_down(&record_button) || is_button_down(&play_button));
    digitalWrite(STATUS_LED_PIN_B, LOW);

}

void enter_playback_loop(void) {

    size_t bytes_written = 0;
    size_t samples_written = 0;

    delay(500);

    digitalWrite(STATUS_LED_PIN_R, LOW);
    digitalWrite(STATUS_LED_PIN_G, LOW);
    digitalWrite(STATUS_LED_PIN_B, HIGH);
    
    while (samples_written < recorded_data_size) {

        update_button(&play_button);
        update_button(&record_button);

        if (is_button_down(&play_button))
            return;

        size_t samples_to_write = recorded_data_size - samples_written;
        if (samples_to_write > RING_BUFFER_BLOCK_SIZE)
            samples_to_write = RING_BUFFER_BLOCK_SIZE;

        for (size_t i = 0; i < samples_to_write; i++)
            playback_buffer[i] = (int32_t)recorded_data[samples_written + i] << 16;

        esp_err_t result = i2s_write(I2S_NUM_0, playback_buffer, samples_to_write * sizeof(int32_t), &bytes_written, portMAX_DELAY);
        if (result != ESP_OK || bytes_written == 0)
            break;
        samples_written += bytes_written / sizeof(int32_t);

    }

}

void loop(void) {

    update_status_led();
    update_button(&play_button);
    update_button(&record_button);

    // @note if we press record while recording, stop and return to ready state.
    if (recording && is_button_down(&record_button)) 
        recording = false;

    // @note if we hold record for 3 seconds, start recording
    if (is_button_held_for_ms(&record_button, 3000))
        recording = true;

    if (is_button_down(&play_button) && !recording && recorded_data_size > 0) {
        enter_playback_loop();
        memset(playback_buffer, 0, RING_BUFFER_BLOCK_SIZE * sizeof(int32_t));
    }

    // @note if we hold the play button for 3 seconds it will clear the current recording
    if (is_button_held_for_ms(&play_button, 3000) && recorded_data_size > 0) {
        
        memset(recorded_data, 0, recorded_data_size * sizeof(int16_t));
        recorded_data_size = 0;

        // @note flash red to indicated completion 
        digitalWrite(STATUS_LED_PIN_R, HIGH);
        digitalWrite(STATUS_LED_PIN_G, LOW);
        digitalWrite(STATUS_LED_PIN_B, LOW);
        delay(500);
    
    }

    if (recording) {

        size_t bytes_read = 0;
        if (i2s_read(I2S_NUM_0, record_buffer, sizeof(record_buffer), &bytes_read, portMAX_DELAY) == ESP_OK && bytes_read > 0) {

            size_t samples_read = bytes_read / sizeof(int32_t);
            if (recorded_data_size + samples_read > RECORDED_SAMPLE_COUNT) {
                samples_read = RECORDED_SAMPLE_COUNT - recorded_data_size;
                recording = false;
            }

            for (size_t i = 0; i < samples_read; i++) {
                
                // @note because the microphone outputs the most significant bit first
                // we need to shift it back down to its normal value, the microphone
                // outputs a 24 bit value so we can just shift it down by 8 to get a
                // normal 32 bit value, but since we want a 16 bit value, we will
                // shift it down another 8 bits for a total shift of 16 bits.

                int16_t sample_16bit = (int16_t)(record_buffer[i] >> 16);
                recorded_data[recorded_data_size++] = sample_16bit;

            }

        }

    }

}