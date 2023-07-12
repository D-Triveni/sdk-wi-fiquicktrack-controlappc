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
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "vendor_specific.h"
#include "utils.h"

void interfaces_init() {
}
/* Be invoked when start controlApp */
void vendor_init() {
}

/* Be invoked when terminate controlApp */
void vendor_deinit() {
}

/* Called by reset_device_hander() */
void vendor_device_reset() {
}

/* Return addr of P2P-device if there is no GO or client interface */
int get_p2p_mac_addr(char *mac_addr, size_t size) {
#ifdef CONFIG_ZEPHYR_P2P
    FILE *fp;
    char buffer[S_BUFFER_LEN], *ptr, addr[32];
    int error = 1, match = 0;

    fp = popen("iw dev", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            ptr = strstr(buffer, "addr");
            if (ptr != NULL) {
                sscanf(ptr, "%*s %s", addr);
                while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                    ptr = strstr(buffer, "type");
                    if (ptr != NULL) {
                        ptr += 5;
                        if (!strncmp(ptr, "P2P-GO", 6) || !strncmp(ptr, "P2P-client", 10)) {
			                snprintf(mac_addr, size, "%s", addr);
                            error = 0;
                            match = 1;
                        } else if (!strncmp(ptr, "P2P-device", 10)) {
			                snprintf(mac_addr, size, "%s", addr);
                            error = 0;
                        }
                        break;
                    }
                }
                if (match)
                    break;
            }
        }
        pclose(fp);
    }

    return error;
#else
    (void) mac_addr;
    (void) size;
    return 1;
#endif /* CONFIG_ZEPHYR_P2P */
}

/* Get the name of P2P Group(GO or Client) interface */
int get_p2p_group_if(char *if_name, size_t size) {
#ifdef CONFIG_ZEPHYR_P2P
    FILE *fp;
    char buffer[S_BUFFER_LEN], *ptr, name[32];
    int error = 1;

    fp = popen("iw dev", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            ptr = strstr(buffer, "Interface");
            if (ptr != NULL) {
                sscanf(ptr, "%*s %s", name);
                while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                    ptr = strstr(buffer, "type");
                    if (ptr != NULL) {
                        ptr += 5;
                        if (!strncmp(ptr, "P2P-GO", 6) || !strncmp(ptr, "P2P-client", 10)) {
			                snprintf(if_name, size, "%s", name);
                            error = 0;
                        }
                        break;
                    }
                }
                if (!error)
                    break;
            }
        }
        pclose(fp);
    }

    return error;
#else
    (void) if_name;
    (void) size;
    return 1;
#endif /* CONFIG_ZEPHYR_P2P */
}

/* "iw dev" doesn't show the name of P2P device. The naming rule is based on wpa_supplicant */
int get_p2p_dev_if(char *if_name, size_t size) {
    snprintf(if_name, size, "p2p-dev-%s", get_wireless_interface());

    return 0;
}

/* Append IP range config and start dhcpd */
void start_dhcp_server(char *if_name, char *ip_addr)
{
    char buffer[S_BUFFER_LEN];
    char ip_sub[32], *ptr;
    FILE *fp;

    /* Avoid using system dhcp server service
       snprintf(buffer, sizeof(buffer), "sed -i -e 's/INTERFACESv4=\".*\"/INTERFACESv4=\"%s\"/g' /etc/default/isc-dhcp-server", if_name);
       system(buffer);
       snprintf(buffer, sizeof(buffer), "systemctl restart isc-dhcp-server.service");
       system(buffer);
     */
    /* Sample command from isc-dhcp-server: dhcpd -user dhcpd -group dhcpd -f -4 -pf /run/dhcp-server/dhcpd.pid -cf /etc/dhcp/dhcpd.conf p2p-wlp2s0-0 */

    /* Avoid apparmor check because we manually start dhcpd */
    memset(ip_sub, 0, sizeof(ip_sub));
    ptr = strrchr(ip_addr, '.');
    memcpy(ip_sub, ip_addr, ptr - ip_addr);
    system("cp QT_dhcpd.conf /etc/dhcp/QT_dhcpd.conf");
    fp = fopen("/etc/dhcp/QT_dhcpd.conf", "a");
    if (fp) {
        snprintf(buffer, sizeof(buffer), "\nsubnet %s.0 netmask 255.255.255.0 {\n", ip_sub);
        fputs(buffer, fp);
        snprintf(buffer, sizeof(buffer), "    range %s.50 %s.200;\n", ip_sub, ip_sub);
        fputs(buffer, fp);
        fputs("}\n", fp);
        fclose(fp);
    }
    system("touch /var/lib/dhcp/dhcpd.leases_QT");
    snprintf(buffer, sizeof(buffer), "dhcpd -4 -cf /etc/dhcp/QT_dhcpd.conf -lf /var/lib/dhcp/dhcpd.leases_QT %s", if_name);
    system(buffer);
}

void stop_dhcp_server()
{
    /* system("systemctl stop isc-dhcp-server.service"); */
    system("killall dhcpd 1>/dev/null 2>/dev/null");
}

void start_dhcp_client(char *if_name)
{
    char buffer[S_BUFFER_LEN];

    snprintf(buffer, sizeof(buffer), "dhclient -4 %s &", if_name);
    system(buffer);
}

void stop_dhcp_client()
{
    system("killall dhclient 1>/dev/null 2>/dev/null");
}

wps_setting *p_wps_setting = NULL;
wps_setting customized_wps_settings_sta[STA_SETTING_NUM];

void save_wsc_setting(wps_setting *s, char *entry, int len)
{
    char *p = NULL;

    (void) len;

    p = strchr(entry, '\n');
    if (p)
        p++;
    else
        p = entry;

    sscanf(p, "%[^:]:%[^:]:%s", s->wkey, s->value, s->attr);
}

wps_setting* __get_wps_setting(int len, char *buffer, enum wps_device_role role)
{
    char *token = strtok(buffer , ",");
    wps_setting *s = NULL;
    int i = 0;

    (void) len;

        memset(customized_wps_settings_sta, 0, sizeof(customized_wps_settings_sta));
        p_wps_setting = customized_wps_settings_sta;
        while (token != NULL) {
            s = &p_wps_setting[i++];
            save_wsc_setting(s, token, strlen(token));
            token = strtok(NULL, ",");
        }
    return p_wps_setting;
}

wps_setting* get_vendor_wps_settings(enum wps_device_role role)
{
    /*
     * Please implement the vendor proprietary function to get WPS OOB and required settings.
     * */
#define WSC_SETTINGS_FILE_STA "/tmp/wsc_settings_STAUT"
    int len = 0;
    char pipebuf[S_BUFFER_LEN];
    char *parameter_sta[] = {"cat", WSC_SETTINGS_FILE_STA, NULL, NULL};

    memset(pipebuf, 0, sizeof(pipebuf));
        if (0 == access(WSC_SETTINGS_FILE_STA, F_OK)) {
            // use customized sta wsc settings
            len = pipe_command(pipebuf, sizeof(pipebuf), "/usr/bin/cat", parameter_sta);
            if (len) {
                indigo_logger(LOG_LEVEL_INFO, "wsc settings STAUT:\n %s", pipebuf);
                return __get_wps_setting(len, pipebuf, WPS_STA);
            } else {
                indigo_logger(LOG_LEVEL_INFO, "wsc settings STAUT: no data");
            }
        } else {
            indigo_logger(LOG_LEVEL_ERROR, "STAUT: WPS Erorr. Failed to get settings.");
            return NULL;
        }

    return NULL;
}
