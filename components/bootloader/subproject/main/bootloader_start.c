// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_log.h"
#include "rom/gpio.h"
#include "rom/spi_flash.h"
#include "bootloader_config.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "sdkconfig.h"
#include "esp_image_format.h"

static const char* TAG = "boot";

static bool check_force_button(void)
{
	volatile int n;

	gpio_pad_select_gpio(CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO);
	GPIO_DIS_OUTPUT(CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO);
	gpio_pad_unhold(CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO);
	gpio_pad_pullup(CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO);

//	*((volatile uint32_t *)PERIPHS_IO_MUX_GPIO34_U) = FUNC_GPIO34_GPIO34_0;
//	*((volatile uint32_t *)GPIO_ENABLE1_REG) &= ~(1 << (CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO - 32));
//	*((volatile uint32_t *)RTC_IO_TOUCH_PAD4_REG) |= RTC_IO_TOUCH_PAD4_TO_GPIO | RTC_IO_TOUCH_PAD4_FUN_IE | RTC_IO_TOUCH_PAD4_MUX_SEL;
	for (n = 0; n < 50; n++)
		;
	return !GPIO_INPUT_GET(CONFIG_BOOTLOADER_FACTORY_BUTTON_GPIO);
}

#define LWS_MAGIC_REBOOT_TYPE_ADS 0x50001ffc
#define LWS_MAGIC_REBOOT_TYPE_REQ_FACTORY 0xb00bcafe
#define LWS_MAGIC_REBOOT_TYPE_FORCED_FACTORY 0xfaceb00b
#define LWS_MAGIC_REBOOT_TYPE_FORCED_FACTORY_BUTTON 0xf0cedfac

static int select_partition_number (bootloader_state_t *bs);
static int selected_boot_partition(const bootloader_state_t *bs);
/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void call_start_cpu0()
{
    // 1. Hardware initialization
    if (bootloader_init() != ESP_OK) {
        return;
    }

    // 2. Select the number of boot partition
    bootloader_state_t bs = { 0 };
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        return;
    }

    /* we can leave a magic at end of fast RTC ram to force next boot into FACTORY */
	uint32_t *p_force_factory_magic = (uint32_t *)LWS_MAGIC_REBOOT_TYPE_ADS;

	if (*p_force_factory_magic == LWS_MAGIC_REBOOT_TYPE_REQ_FACTORY) {
		boot_index = FACTORY_INDEX;
		/* mark as having been forced... needed to fixup wrong reported boot part */
		*p_force_factory_magic = LWS_MAGIC_REBOOT_TYPE_FORCED_FACTORY;
	} else
		*p_force_factory_magic = 0;

	if (check_force_button()) {
		boot_index = FACTORY_INDEX;
		ESP_LOGE(TAG, "Force Button is Down 0x%x\n", gpio_input_get());
		*p_force_factory_magic = LWS_MAGIC_REBOOT_TYPE_FORCED_FACTORY_BUTTON;
	}

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);
}

// Select the number of boot partition
static int select_partition_number (bootloader_state_t *bs)
{
    // 1. Load partition table
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    // 2. Select the number of boot partition
    return selected_boot_partition(bs);
}

/*
 * Selects a boot partition.
 * The conditions for switching to another firmware are checked.
 */
static int selected_boot_partition(const bootloader_state_t *bs)
{
    int boot_index = bootloader_utility_get_selected_boot_partition(bs);
    if (boot_index == INVALID_INDEX) {
        return boot_index; // Unrecoverable failure (not due to corrupt ota data or bad partition contents)
    } else {
        // Factory firmware.
#ifdef CONFIG_BOOTLOADER_FACTORY_RESET
        if (bootloader_common_check_long_hold_gpio(CONFIG_BOOTLOADER_NUM_PIN_FACTORY_RESET, CONFIG_BOOTLOADER_HOLD_TIME_GPIO) == 1) {
            ESP_LOGI(TAG, "Detect a condition of the factory reset");
            bool ota_data_erase = false;
#ifdef CONFIG_BOOTLOADER_OTA_DATA_ERASE
            ota_data_erase = true;
#endif
            const char *list_erase = CONFIG_BOOTLOADER_DATA_FACTORY_RESET;
            ESP_LOGI(TAG, "Data partitions to erase: %s", list_erase);
            if (bootloader_common_erase_part_type_data(list_erase, ota_data_erase) == false) {
                ESP_LOGE(TAG, "Not all partitions were erased");
            }
            return bootloader_utility_get_selected_boot_partition(bs);
        }
#endif
       // TEST firmware.
#ifdef CONFIG_BOOTLOADER_APP_TEST
        if (bootloader_common_check_long_hold_gpio(CONFIG_BOOTLOADER_NUM_PIN_APP_TEST, CONFIG_BOOTLOADER_HOLD_TIME_GPIO) == 1) {
            ESP_LOGI(TAG, "Detect a boot condition of the test firmware");
            if (bs->test.offset != 0) {
                boot_index = TEST_APP_INDEX;
                return boot_index;
            } else {
                ESP_LOGE(TAG, "Test firmware is not found in partition table");
                return INVALID_INDEX;
            }
        }
#endif
        // Customer implementation.
        // if (gpio_pin_1 == true && ...){
        //     boot_index = required_boot_partition;
        // } ...
    }
    return boot_index;
}
