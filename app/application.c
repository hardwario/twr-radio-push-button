#include <application.h>

#define SERVICE_INTERVAL_INTERVAL (60 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL (60 * 60 * 1000)
#define TEMPERATURE_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define TEMPERATURE_PUB_VALUE_CHANGE 0.2f
#define TEMPERATURE_UPDATE_SERVICE_INTERVAL (5 * 1000)
#define TEMPERATURE_UPDATE_NORMAL_INTERVAL (10 * 1000)

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;
uint16_t button_event_count = 0;

// Thermometer instance
bc_tmp112_t tmp112;
event_param_t temperature_event_param = { .next_pub = 0 };

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    uint16_t *event_count = (uint16_t *) event_param;

    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_pulse(&led, 100);

        (*event_count)++;

        bc_radio_pub_push_button(event_count);
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event;
    (void) event_param;

    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        bc_radio_pub_battery(&voltage);
    }
}

void temperature_tag_event_handler(bc_tmp112_t *self, bc_tmp112_event_t event, void *event_param)
{
    float value;
    event_param_t *param = (event_param_t *)event_param;

    if (event == BC_TMP112_EVENT_UPDATE)
    {
        if (bc_tmp112_get_temperature_celsius(self, &value))
        {
            if ((fabs(value - param->value) >= TEMPERATURE_PUB_VALUE_CHANGE) || (param->next_pub < bc_scheduler_get_spin_tick()))
            {
                bc_radio_pub_temperature(param->channel, &value);
                param->value = value;
                param->next_pub = bc_scheduler_get_spin_tick() + TEMPERATURE_PUB_NO_CHANGE_INTEVAL;
            }
        }
    }
}

void switch_to_normal_mode_task(void *param)
{
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_NORMAL_INTERVAL);

    bc_scheduler_unregister(bc_scheduler_get_current_task_id());
}

void application_init(void)
{
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Initialize radio
    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, &button_event_count);

    // Initialize battery
    bc_module_battery_init(BC_MODULE_BATTERY_FORMAT_MINI);
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize thermometer sensor on core module
    temperature_event_param.channel = BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_ALTERNATE;
    bc_tmp112_init(&tmp112, BC_I2C_I2C0, 0x49);
    bc_tmp112_set_event_handler(&tmp112, temperature_tag_event_handler, &temperature_event_param);
    bc_tmp112_set_update_interval(&tmp112, TEMPERATURE_UPDATE_SERVICE_INTERVAL);

    bc_radio_pairing_request("kit-push-button", VERSION);

    bc_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

    bc_led_pulse(&led, 2000);
}
