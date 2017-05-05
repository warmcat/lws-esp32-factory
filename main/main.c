/*
 * ESP32 "Factory" WLAN Config + Factory Setup app
 *
 * Copyright (C) 2017 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */
#include <libwebsockets.h>
#include <nvs_flash.h>
#include "soc/ledc_reg.h"
#include "driver/ledc.h"

void (*lws_cb_scan_done)(void *);
void *lws_cb_scan_done_arg;

/* protocol for scan updates over ws and saving wlan setup */
#include "protocol_esp32_lws_scan.c"
/* protocol for OTA update using POST / https / browser upload */
#include "protocol_esp32_lws_ota.c"

static int id_flashes;

static const struct lws_protocols protocols_ap[] = {
	{
		"http-only",
		lws_callback_http_dummy,
		0,	/* per_session_data_size */
		0, 0, NULL, 900
	},
	LWS_PLUGIN_PROTOCOL_ESPLWS_SCAN,
	LWS_PLUGIN_PROTOCOL_ESPLWS_OTA,

	{ NULL, NULL, 0, 0, 0, NULL, 0 } /* terminator */
};

static const struct lws_protocol_vhost_options pvo_headers = {
	NULL,
	NULL,
	"Keep-Alive:",
	"timeout=15, max=20",
};

static const struct lws_protocol_vhost_options ap_pvo2 = {
	NULL,
	NULL,
	"esplws-ota",
	""
};

static const struct lws_protocol_vhost_options ap_pvo = {
	&ap_pvo2,
	NULL,
	"esplws-scan",
	""
};

static const struct lws_http_mount mount_ota_post = {
	.mountpoint		= "/otaform",
	.origin			= "esplws-ota",
	.origin_protocol	= LWSMPRO_CALLBACK,
	.mountpoint_len		= 8,
};

static const struct lws_http_mount mount_ap_post = {
	.mount_next		= &mount_ota_post,
	.mountpoint		= "/factory",
	.origin			= "esplws-scan",
	.origin_protocol	= LWSMPRO_CALLBACK,
	.mountpoint_len		= 8,
};

static struct lws_http_mount mount_ap = {
	.mount_next		= &mount_ap_post,
	.mountpoint		= "/",
        .origin			= "/ap",
        .def			= "index.html",
        .origin_protocol	= LWSMPRO_FILE,
        .mountpoint_len		= 1,
	.cache_max_age		= 300000,
	.cache_reusable		= 1,
        .cache_revalidate       = 1,
        .cache_intermediaries   = 1,
};

static const uint16_t sineq16[] = {
	0x0000, 0x0191, 0x031e, 0x04a4, 0x061e, 0x0789, 0x08e2, 0x0a24,
	0x0b4e, 0x0c5c, 0x0d4b, 0x0e1a, 0x0ec6, 0x0f4d, 0x0faf, 0x0fea,
};

esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch(event->event_id) {
	case SYSTEM_EVENT_SCAN_DONE:
		if (lws_cb_scan_done)
			lws_cb_scan_done(lws_cb_scan_done_arg);
		break;

	default:
		return lws_esp32_event_passthru(ctx, event);
	}

	return ESP_OK;
}

#define GPIO_ID 23


/*
 * This is called when the user asks to "Identify physical device"
 * he is configuring, by pressing the Identify button on the AP
 * setup page for the device.
 *
 * It should do something device-specific that
 * makes it easy to identify which physical device is being
 * addressed, like flash an LED on the device on a timer for a
 * few seconds.
 */
void
lws_esp32_identify_physical_device(void)
{
	lwsl_notice("%s\n", __func__);

	id_flashes = 1;
}

static ledc_timer_config_t ledc_timer = {
        .bit_num = LEDC_TIMER_13_BIT,
        .freq_hz = 5000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0
};

void app_main(void)
{
	static struct lws_context_creation_info info;
	struct lws_context *context;
        ledc_channel_config_t ledc_channel = {
            .channel = LEDC_CHANNEL_0,
            .duty = 8191,
            .gpio_num = GPIO_ID,
            .intr_type = LEDC_INTR_FADE_END,
            .speed_mode = LEDC_HIGH_SPEED_MODE,
            .timer_sel = LEDC_TIMER_0,
        };
	struct timeval t, lt;
        unsigned long us;

	ledc_timer_config(&ledc_timer);
	ledc_channel_config(&ledc_channel);

	lws_esp32_set_creation_defaults(&info);

	info.vhost_name = "ap";
	info.protocols = protocols_ap;
	info.mounts = &mount_ap;
	info.pvo = &ap_pvo;
	info.headers = &pvo_headers;

	nvs_flash_init();
	lws_esp32_wlan_config();

	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL));

	lws_esp32_wlan_start_ap();
	context = lws_esp32_init(&info);

	if (info.port == 80) {
		lwsl_notice("setting mount default to factory\n");
		mount_ap.def = "factory.html";
	}

	gettimeofday(&lt, NULL);

	while (!lws_service(context, 50)) {
		gettimeofday(&t, NULL);
                us = (t.tv_sec * 1000000) + t.tv_usec -
                                ((lt.tv_sec * 1000000) + lt.tv_usec);
                if (us < 20000)
                        continue;

                lt = t;

		if (!id_flashes) {
			unsigned long r = ((t.tv_sec * 1000000) + t.tv_usec) /
                                                20000;
			int i = 0;
			switch ((r >> 4) & 3) {
			case 0: 
				i = 4096+ sineq16[r & 15];
				break;
			case 1:
				i = 4096 + sineq16[15 - (r & 15)];
				break;
			case 2:
				i = 4096 - sineq16[r & 15];
				break;
			case 3:
				i = 4096 - sineq16[15 - (r & 15)];
				break;
			}
	                ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, i);
		} else
	                ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0,
				!!((((t.tv_sec * 1000000) + t.tv_usec) /
						10000) & 2));

		ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);

		if (id_flashes) {
			id_flashes++;
			if (id_flashes == 500)
				id_flashes = 0;
		}

                taskYIELD();
	}
}
