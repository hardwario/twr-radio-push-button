#include <application.h>

// Service mode interval defines how much time
#define SERVICE_MODE_INTERVAL (15 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)
#define TEMPERATURE_PUB_INTERVAL (15 * 60 * 1000)
#define TEMPERATURE_PUB_DIFFERENCE 0.2f
#define TEMPERATURE_UPDATE_SERVICE_INTERVAL (1 * 1000)
#define TEMPERATURE_UPDATE_NORMAL_INTERVAL (10 * 1000)
#define ACCELEROMETER_UPDATE_SERVICE_INTERVAL (1 * 1000)
#define ACCELEROMETER_UPDATE_NORMAL_INTERVAL (10 * 1000)

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

// Thermometer instance
bc_tmp112_t tmp112;

// Accelerometer instance
bc_lis2dh12_t lis2dh12;

// Dice instance
bc_dice_t dice;

// Counters for button events
uint16_t button_click_count = 0;
uint16_t button_hold_count = 0;

// Time of button has press
bc_tick_t tick_start_button_press;
// Flag for button hold event
bool button_hold_event;

// Time of next temperature report
bc_tick_t tick_temperature_report = 0;

// This function dispatches button events
void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    if (event == BC_BUTTON_EVENT_CLICK)
    {
        // Pulse LED for 100 milliseconds
        bc_led_pulse(&led, 100);

        // Increment press count
        button_click_count++;

        bc_log_info("APP: Publish button press count = %u", button_click_count);

        // Publish button message on radio
        bc_radio_pub_push_button(&button_click_count);
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        // Pulse LED for 250 milliseconds
        bc_led_pulse(&led, 250);

        // Increment hold count
        button_hold_count++;

        bc_log_info("APP: Publish button hold count = %u", button_hold_count);

        // Publish message on radio
        bc_radio_pub_event_count(BC_RADIO_PUB_EVENT_HOLD_BUTTON, &button_hold_count);

        // Set button hold event flag
        button_hold_event = true;
    }
    else if (event == BC_BUTTON_EVENT_PRESS)
    {
        // Reset button hold event flag
        button_hold_event = false;

        tick_start_button_press = bc_tick_get();
    }
    else if (event == BC_BUTTON_EVENT_RELEASE)
    {
        if (button_hold_event)
        {
            int hold_duration = bc_tick_get() - tick_start_button_press;

            bc_log_info("APP: Publish button hold duration = %d", hold_duration);

            bc_radio_pub_value_int(BC_RADIO_PUB_VALUE_HOLD_DURATION_BUTTON, &hold_duration);
        }
    }
}

// This function dispatches battery events
void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        float voltage;

        // Read battery voltage
        if (bc_module_battery_get_voltage(&voltage))
        {
            bc_log_info("APP: Battery voltage = %.2f", voltage);

            // Publish battery voltage
            bc_radio_pub_battery(&voltage);
        }
    }
}

// This function dispatches thermometer events
void tmp112_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_TMP112_EVENT_UPDATE)
    {
        float temperature;

        // Successfully read temperature?
        if (bc_tmp112_get_temperature_celsius(self, &temperature))
        {
            bc_log_info("APP: Temperature = %0.1f C", temperature);

            // Implicitly do not publish message on radio
            bool publish = false;

            // Is time up to report temperature?
            if (bc_tick_get() >= tick_temperature_report)
            {
                // Publish message on radio
                publish = true;
            }

            // Last temperature value used for change comparison
            static float last_published_temperature = NAN;

            // Temperature ever published?
            if (last_published_temperature != NAN)
            {
                // Is temperature difference from last published value significant?
                if (fabsf(temperature - last_published_temperature) >= TEMPERATURE_PUB_DIFFERENCE)
                {
                    bc_log_info("APP: Temperature change threshold reached");

                    // Publish message on radio
                    publish = true;
                }
            }

            // Publish message on radio?
            if (publish)
            {
                bc_log_info("APP: Publish temperature");

                // Publish temperature message on radio
                bc_radio_pub_temperature(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE, &temperature);

                // Schedule next temperature report
                tick_temperature_report = bc_tick_get() + TEMPERATURE_PUB_INTERVAL;

                // Remember last published value
                last_published_temperature = temperature;
            }
        }
    }
    // Error event?
    else if (event == BC_TMP112_EVENT_ERROR)
    {
        bc_log_error("APP: Thermometer error");
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    // Update event?
    if (event == BC_LIS2DH12_EVENT_UPDATE)
    {
        bc_lis2dh12_result_g_t result;

        // Successfully read accelerometer vectors?
        if (bc_lis2dh12_get_result_g(self, &result))
        {
            bc_log_info("APP: Acceleration = [%.2f,%.2f,%.2f]", result.x_axis, result.y_axis, result.z_axis);

            // Update dice with new vectors
            bc_dice_feed_vectors(&dice, result.x_axis, result.y_axis, result.z_axis);

            // This variable holds last dice face
            static bc_dice_face_t last_face = BC_DICE_FACE_UNKNOWN;

            // Get current dice face
            bc_dice_face_t face = bc_dice_get_face(&dice);

            // Did dice face change from last time?
            if (last_face != face)
            {
                // Remember last dice face
                last_face = face;

                // Convert dice face to integer
                int orientation = face;

                bc_log_info("APP: Publish orientation = %d", orientation);

                // Publish orientation message on radio
                // Be careful, this topic is only development state, can be change in future.
                bc_radio_pub_int("orientation", &orientation);
            }
        }
    }
    // Error event?
    else if (event == BC_LIS2DH12_EVENT_ERROR)
    {
        bc_log_error("APP: Accelerometer error");
    }
}

// This function is run as task and exits service mode
void exit_service_mode_task(void *param)
{
    // Set thermometer update interval to normal
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_NORMAL_INTERVAL);

    // Set accelerometer update interval to normal
    bc_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_NORMAL_INTERVAL);

    // Unregister current task (it has only one-shot purpose)
    bc_scheduler_unregister(bc_scheduler_get_current_task_id());
}

void application_init(void)
{
    // Initialize log
    bc_log_init(BC_LOG_LEVEL_INFO, BC_LOG_TIMESTAMP_ABS);
    bc_log_info("APP: Reset");

    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_SERVICE_INTERVAL);

    // Initialize accelerometer
    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    bc_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_SERVICE_INTERVAL);

    // Initialize dice
    bc_dice_init(&dice, BC_DICE_FACE_UNKNOWN);

    // Initialize radio
    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    // Send radio pairing request
    bc_radio_pairing_request("push-button", VERSION);

    bc_scheduler_register(exit_service_mode_task, NULL, SERVICE_MODE_INTERVAL);

    // Pulse LED
    bc_led_pulse(&led, 2000);
}
