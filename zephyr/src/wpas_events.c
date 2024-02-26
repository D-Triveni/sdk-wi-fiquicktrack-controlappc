/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief WPA SUPP events
 */

#include <stdio.h>
#include <zephyr/net/net_event.h>
#include <ctrl_iface_zephyr.h>
#include <supp_events.h>
#include <utils.h>
#include <supp_main.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
LOG_MODULE_REGISTER(wpas_event, CONFIG_WFA_QT_LOG_LEVEL);

#define WPA_SUPP_EVENTS (NET_EVENT_WPA_SUPP_READY)

static struct net_mgmt_event_callback net_wpa_supp_cb;
struct wpa_supplicant *wpa_s = NULL;

K_SEM_DEFINE(wpa_supp_ready_sem, 0, 1);

static void handle_wpa_supp_ready(struct net_mgmt_event_callback *cb)
{
	int retry_count = 0;

retry:
	wpa_s = z_wpas_get_handle_by_ifname("wlan0");
	if (!wpa_s && retry_count++ < 5) {
		LOG_ERR("%s: Unable to get wpa_s handle for %s\n",
			      __func__, "wlan0");
		goto retry;
	}

	if (!wpa_s) {
		LOG_ERR("%s: Unable to get wpa_s handle for %s\n",
				__func__, "wlan0");
	}

	if (wpa_s) {
		LOG_INF("Supplicant is ready");
		k_sem_give(&wpa_supp_ready_sem);
	}

}

static void wpa_supp_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t mgmt_event, struct net_if *iface)
{
        switch (mgmt_event) {
        case NET_EVENT_WPA_SUPP_READY:
		handle_wpa_supp_ready(cb);
                break;
        default:
                break;
        }
}

int wait_for_wpa_s_ready(void)
{
	if (wpa_s) {
		return 0;
	}

	k_sem_take(&wpa_supp_ready_sem, K_FOREVER);

	/* Check for ctrl_iface initialization */
	if (wpa_s->ctrl_iface->sock_pair[0] < 0) {
		return -1;
	}

	return 0;
}

int wpa_supp_events_register(void)
{
	net_mgmt_init_event_callback(&net_wpa_supp_cb,
                                     wpa_supp_event_handler,
                                     WPA_SUPP_EVENTS);
        net_mgmt_add_event_callback(&net_wpa_supp_cb);

	return 0;
}
