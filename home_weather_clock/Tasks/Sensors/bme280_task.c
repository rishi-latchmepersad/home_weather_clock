/*
 * bme280_task.c
 *
 *  Created on: Jul 17, 2025
 *      Author: rishi_latchmepersad
 */

#include "bme280_if.h"
#include "bme280_defs.h"
#include "bme280_task.h"
#include "bme280.h"
#include <stdio.h>
#include "main.h"
#ifdef UNIT_TESTING
    #include "FreeRTOS_stub.h"
#else
    #include "FreeRTOS.h"
#endif
#include "cmsis_os.h"

static uint8_t dev_addr = (BME280_I2C_ADDR_PRIM << 1);


static int8_t bme280_setup(struct bme280_dev *dev) {
	struct bme280_settings settings;
	int8_t result;

	printf("Initializing BME280...\r\n");
	dev->read = bme280_i2c_read;
	dev->write = bme280_i2c_write;
	dev->delay_us = bme280_delay_us;
	dev->intf = BME280_I2C_INTF;
	dev->intf_ptr = &dev_addr;

	result = bme280_init(dev);
	if (result != BME280_OK) {
		printf("BME280 initialization failed! Error code: %d\r\n", result);
		return result;
	}

	// Set up oversampling and filter settings
	settings.osr_h = BME280_OVERSAMPLING_1X;
	settings.osr_p = BME280_OVERSAMPLING_1X;
	settings.osr_t = BME280_OVERSAMPLING_1X;
	settings.filter = BME280_FILTER_COEFF_OFF;
	settings.standby_time = BME280_STANDBY_TIME_1000_MS;

	result = bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings,
			dev);
	if (result != BME280_OK) {
		printf("Failed to set BME280 settings! Error code: %d\r\n", result);
	}

	return result;
}

void bme280SensorTask(void *argument) {
	bme280_task_data_t task_data = { .state = BME280_STATE_INIT, .last_tick =
			xTaskGetTickCount() };

	while (1) {
		switch (task_data.state) {
		case BME280_STATE_INIT:
			// Initialize any variables if needed
			task_data.state = BME280_STATE_SETUP;
			break;

		case BME280_STATE_SETUP:
			task_data.result = bme280_setup(&task_data.dev);
			if (task_data.result == BME280_OK) {
				task_data.state = BME280_STATE_TRIGGER_MEASUREMENT;
			} else {
				printf("Failed to setup BME280.\n");
				task_data.state = BME280_STATE_ERROR;
			}
			break;

		case BME280_STATE_TRIGGER_MEASUREMENT:
			bme280_set_sensor_mode(BME280_ALL, &task_data.dev);
			task_data.last_tick = xTaskGetTickCount();
			task_data.state = BME280_STATE_WAIT_MEASUREMENT;
			break;

		case BME280_STATE_WAIT_MEASUREMENT:
			// wait until 120ms have passed to let sensor collect and process data
			if ((xTaskGetTickCount() - task_data.last_tick)
					>= pdMS_TO_TICKS(120)) {
				task_data.state = BME280_STATE_READ_DATA;
			}
			break;

		case BME280_STATE_READ_DATA:
			task_data.result = bme280_get_sensor_data(BME280_ALL,
					&task_data.data, &task_data.dev);
			if (task_data.result == BME280_OK) {
				printf("Temperature: %.2f °C\r\n", task_data.data.temperature);
				printf("Pressure: %.2f Pa\r\n", task_data.data.pressure);
				printf("Humidity: %.2f %%\r\n", task_data.data.humidity);
			} else {
				printf("Failed to read BME280 data! Error code: %d\r\n",
						task_data.result);
				task_data.state = BME280_STATE_ERROR;
			}
			task_data.last_tick = xTaskGetTickCount();
			task_data.state = BME280_STATE_DELAY;
			break;

		case BME280_STATE_DELAY:
			if ((xTaskGetTickCount() - task_data.last_tick)
					>= pdMS_TO_TICKS(2000)) {
				task_data.state = BME280_STATE_TRIGGER_MEASUREMENT;
			}
			break;

		case BME280_STATE_ERROR:
			// wait 5s and restart task loop
			printf("We ran into an error with the BME280 sensor. Restarting task loop in 5s.");
			vTaskDelay(pdMS_TO_TICKS(5000));
			task_data.state = BME280_STATE_INIT;
			break;
		}

		vTaskDelay(1); // Small delay to prevent tight loop
	}
}
