#include "input.h"

button_t create_button(uint16_t pin) {

    pinMode((uint8_t)pin, INPUT_PULLDOWN);

    return (button_t) {
        .is_down = false,
        .being_held = false,
        .pin = pin,
        .start = 0
    };

}

void update_button(button_t* button) {

    if (digitalRead(button->pin)) {

        if (button->being_held == false) {
            button->being_held = true;
            button->start = millis();
            return;
        }

        uint32_t elapsed = millis() - button->start;

        if (elapsed >= BUTTON_DEBOUNCE_TIME_MS)
            button->is_down = true;
        return;

    }

    button->is_down = false;
    button->being_held = false;
    button->start = 0;

}

bool is_button_down(button_t* button) { 

    return button->is_down; 

}

bool is_button_held_for_ms(button_t* button, uint32_t ms) {

    if (button->being_held) {
        uint32_t elapsed = millis() - button->start;
        return (elapsed >= ms);
    }

    return false;
        
}