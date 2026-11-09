#if !defined(INPUT_H)
#define INPUT_HPP

#include "Arduino.h"

typedef struct button_t button_t;
struct button_t {
    
    bool        is_down, 
                being_held;
    uint16_t    pin;
    uint32_t    start;

    #define BUTTON_DEBOUNCE_TIME_MS         (50U)
    

};
#define empty_button_t                      (button_t) {0}

button_t        create_button               (uint16_t pin);
void            update_button               (button_t* button);
bool            is_button_down              (button_t* button);
bool            is_button_held_for_ms       (button_t* button, uint32_t ms);

#endif // INPUT_HPP