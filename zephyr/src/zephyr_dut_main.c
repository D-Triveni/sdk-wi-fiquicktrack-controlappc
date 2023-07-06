/* Copyright (c) 2020 Wi-Fi Alliance                                                */

/* Permission to use, copy, modify, and/or distribute this software for any         */
/* purpose with or without fee is hereby granted, provided that the above           */
/* copyright notice and this permission notice appear in all copies.                */

/* THE SOFTWARE IS PROVIDED 'AS IS' AND THE AUTHOR DISCLAIMS ALL                    */
/* WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                    */
/* WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL                     */
/* THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR                       */
/* CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING                        */
/* FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF                       */
/* CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT                       */
/* OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS                          */
/* SOFTWARE. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <zephyr/net/socket.h>
#include <unistd.h>
#include <errno.h>

#include "vendor_specific.h"
#include "eloop.h"
#include "indigo_api.h"
#include "utils.h"

#include <zephyr/posix/pthread.h>

void qt_main(void);
K_THREAD_DEFINE(qt_main_tid,
                CONFIG_WFA_QT_THREAD_STACK_SIZE,
                qt_main,
                NULL,
                NULL,
                NULL,
                0,
                0,
                0);

static pthread_t main_thread;
#define STACK_SIZE 4096
K_THREAD_STACK_DEFINE(main_thread_stack, STACK_SIZE); 

/* Internal functions */
static void control_receive_message(int sock, void *eloop_ctx, void *sock_ctx);

/* Initiate the service port. */
static int control_socket_init(int port) {
    int s = -1;
    char cmd[S_BUFFER_LEN];
    struct sockaddr_in addr;

    /* Open UDP socket */
    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        indigo_logger(LOG_LEVEL_ERROR, "Failed to open server socket: %s", strerror(errno));
        return -1;
    }

    /* Bind specific port */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        indigo_logger(LOG_LEVEL_ERROR, "Failed to bind server socket: %s", strerror(errno));
        if (errno == EADDRINUSE) {
            sprintf(cmd, "netstat -lunatp | grep %d", port);
            system(cmd);
        }
        close(s);
        return -1;
    }

    /* Register to eloop and ready for the socket event */
    if (qt_eloop_register_read_sock(s, control_receive_message, NULL, NULL)) {
        indigo_logger(LOG_LEVEL_ERROR, "Failed to initiate ControlAppC");
        close(s);
        return -1;
    }
    return s;
}

void *main_thread_handler() {
    int service_socket = -1;	

    /* Bind the service port and register to eloop */
    service_socket = control_socket_init(get_service_port());
    if (service_socket >= 0) {
        qt_eloop_run();
    } else {
        indigo_logger(LOG_LEVEL_INFO, "Failed to initiate the UDP socket");
    }

    /* Stop eloop */
    qt_eloop_destroy();
    indigo_logger(LOG_LEVEL_INFO, "ControlAppC stops");
    if (service_socket >= 0) {
        indigo_logger(LOG_LEVEL_INFO, "Close service port: %d", get_service_port());
        close(service_socket);
    }

    return 0;
}

struct sockaddr_in *tool_addr; // For HTTP Post
/* Callback function of the QuickTrack API. */
static void control_receive_message(int sock, void *eloop_ctx, void *sock_ctx) {
    int ret;                          // return code
    int fromlen, len;                 // structure size and received length
    struct sockaddr_storage from;     // source address of the message
    char buffer[BUFFER_LEN]; // buffer to receive the message
    struct packet_wrapper req, resp;  // packet wrapper for the received message and response
    struct indigo_api *api = NULL;    // used for API search, validation and handler call

    (void) eloop_ctx;
    (void) sock_ctx;

    /* Receive request */
    fromlen = sizeof(from);
    len = recvfrom(sock, buffer, BUFFER_LEN, 0, (struct sockaddr *) &from, (socklen_t*)&fromlen);
    if (len < 0) {
        indigo_logger(LOG_LEVEL_ERROR, "Server: Failed to receive the packet");
        return ;
    } else {
        indigo_logger(LOG_LEVEL_DEBUG, "Server: Receive the packet");
    }
    tool_addr = (struct sockaddr_in *)&from;

    /* Parse request to HDR and TLV. Response NACK if parser fails. Otherwises, ACK. */
    memset(&req, 0, sizeof(struct packet_wrapper));
    memset(&resp, 0, sizeof(struct packet_wrapper));
    ret = parse_packet(&req, buffer, len);
    if (ret == 0) {
        indigo_logger(LOG_LEVEL_DEBUG, "Server: Parsed packet successfully");
    } else {
        indigo_logger(LOG_LEVEL_ERROR, "Server: Failed to parse the packet");
        fill_wrapper_ack(&resp, req.hdr.seq, 0x31, "Unable to parse the packet");
        len = assemble_packet(buffer, BUFFER_LEN, &resp);

        sendto(sock, (const char *)buffer, len, MSG_CONFIRM, (const struct sockaddr *) &from, fromlen);
        goto done;
    }

    /* Find API by ID. If API is not supported, assemble NACK. */
    api = get_api_by_id(req.hdr.type);
    if (api) {
        indigo_logger(LOG_LEVEL_DEBUG, "API %s: Found handler", api->name);
    } else {
        indigo_logger(LOG_LEVEL_ERROR, "API Unknown (0x%04x): No registered handler", req.hdr.type);
        fill_wrapper_ack(&resp, req.hdr.seq, 0x31, "Unable to find the API handler");
        len = assemble_packet(buffer, BUFFER_LEN, &resp);
        sendto(sock, (const char *)buffer, len, MSG_CONFIRM, (const struct sockaddr *) &from, fromlen);
        goto done;
    }

    /* Verify. Optional. If validation is failed, then return NACK. */
    if (api->verify == NULL || (api->verify && api->verify(&req, &resp) == 0)) {
        indigo_logger(LOG_LEVEL_INFO, "API %s: Return ACK", api->name);
        fill_wrapper_ack(&resp, req.hdr.seq, 0x30, "ACK: Command received");
        len = assemble_packet(buffer, BUFFER_LEN, &resp);
        sendto(sock, (const char *)buffer, len, MSG_CONFIRM, (const struct sockaddr *) &from, fromlen);
        free_packet_wrapper(&resp);
    } else {
        indigo_logger(LOG_LEVEL_ERROR, "API %s: Failed to verify and return NACK", api->name);
        fill_wrapper_ack(&resp, req.hdr.seq, 1, "Unable to find the API handler");
        len = assemble_packet(buffer, BUFFER_LEN, &resp);
        sendto(sock, (const char *)buffer, len, MSG_CONFIRM, (const struct sockaddr *) &from, fromlen);
        goto done;
    }

    /* Optional, use timer to handle the execution */
    /* Handle & Response. Call API handle(), assemble packet by response wrapper and send back to source address. */
    if (api->handle && api->handle(&req, &resp) == 0) {
        indigo_logger(LOG_LEVEL_INFO, "API %s: Return execution result", api->name);
        len = assemble_packet(buffer, BUFFER_LEN, &resp);
        sendto(sock, (const char *)buffer, len, MSG_CONFIRM, (const struct sockaddr *) &from, fromlen);
    } else {
        indigo_logger(LOG_LEVEL_DEBUG, "API %s (0x%04x): No handle function", api ? api->name : "Unknown", req.hdr.type);
    }

done:
    /* Clean up resource */
    free_packet_wrapper(&req);
    free_packet_wrapper(&resp);
    indigo_logger(LOG_LEVEL_DEBUG, "API %s: Complete", api ? api->name : "Unknown");
}

/* Show the welcome message with role and version */
static void print_welcome() {
    printf("Welcome to use QuickTrack Control App DUT version");

    printf(".\n");
}

void qt_main(void) {
    int ret =0;

    pthread_attr_t ptAttr;
    struct sched_param ptSchedParam;
    int ptPolicy;

    /* Welcome message */
    print_welcome();

    /* Initiate the application */
    set_wireless_interface(WIRELESS_INTERFACE_DEFAULT);       // Set default wireless interface information
    set_hapd_full_exec_path(HAPD_EXEC_FILE_DEFAULT);          // Set default hostapd execution file path
    set_hapd_ctrl_path(HAPD_CTRL_PATH_DEFAULT);               // Set default hostapd control interface path
    set_hapd_global_ctrl_path(HAPD_GLOBAL_CTRL_PATH_DEFAULT); // Set default hostapd global control interface path
    set_hapd_conf_file(HAPD_CONF_FILE_DEFAULT);               // Set default hostapd configuration file path
    set_wpas_full_exec_path(WPAS_EXEC_FILE_DEFAULT);          // Set default wap_supplicant execution file path
    set_wpas_ctrl_path(WPAS_CTRL_PATH_DEFAULT);               // Set default wap_supplicant control interface path
    set_wpas_global_ctrl_path(WPAS_GLOBAL_CTRL_PATH_DEFAULT); // Set default wap_supplicant global control interface path
    set_wpas_conf_file(WPAS_CONF_FILE_DEFAULT);               // Set default wap_supplicant configuration file path

    /* Print the run-time information */
    indigo_logger(LOG_LEVEL_INFO, "QuickTrack control app running at: %d", get_service_port());
    indigo_logger(LOG_LEVEL_INFO, "Wireless Interface:" );
    show_wireless_interface_info();
    indigo_logger(LOG_LEVEL_INFO, "hostapd Path: %s (%s)", get_hapd_full_exec_path(), get_hapd_exec_file());
    indigo_logger(LOG_LEVEL_INFO, "wpa_supplicant Path: %s (%s)", get_wpas_full_exec_path(), get_wpas_exec_file());

    /* Register the callback */
    register_apis();

    /* Intiate the vendor's specific startup commands */
    vendor_init();

    /* Start eloop */
    qt_eloop_init(NULL);

    ret = pthread_attr_init(&ptAttr);
    if (ret != 0) {
	   printf("%s:%d ret: %d\n", __func__, __LINE__, ret);
	   return;
    }

    ptSchedParam.sched_priority = 10;
    pthread_attr_setschedparam(&ptAttr, &ptSchedParam);
    pthread_attr_getschedpolicy(&ptAttr, &ptPolicy);
    pthread_attr_setschedpolicy(&ptAttr, SCHED_RR);
    pthread_attr_getschedpolicy(&ptAttr, &ptPolicy);

    ret = pthread_attr_setstack(&ptAttr, &main_thread_stack, 4096);
    if (ret != 0) {
	   printf("%s:%d ret: %d\n", __func__, __LINE__, ret);
	   return;
    }

    pthread_create(&main_thread, &ptAttr, main_thread_handler, NULL);
}