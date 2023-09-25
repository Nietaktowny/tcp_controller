#include "tcp_controller.h"
#include "err_controller.h"
#include "wifi_controller.h"

#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "freertos/task.h"
#include "inttypes.h"


static const char* TAG = "tcp_controller";

static int tcp_c_handle_exchange(const int sock);
static void cli_tcp_server_task(void *pvParameters);

static int tcp_c_receive (void) {
    volatile err_c_t err = ERR_C_OK;
    Try {

    } Catch(err) {

    }

    return err;
}

static int tcp_c_handle_exchange(const int sock)
{
    int len;
    char rx_buffer[512];
    volatile bool status = false;
    volatile err_c_t err = ERR_C_OK;
    Try {
            do {
                len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                if (len < 0) {
                    ERR_C_SET_AND_THROW_ERR(err, errno);
                } else if (len == 0) {
                    ESP_LOGD(TAG, "Connection closed");
                } else {
                    rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
                    ESP_LOGD(TAG, "Received %d bytes.", len);
                    ESP_LOGI(TAG, "%s", rx_buffer);

                    if(strncmp(&rx_buffer[0], "scan", strlen(rx_buffer)) == 0) {
                        status = true;
                    }

                    // send() can return less bytes than supplied length.
                    // Walk-around for robust implementation.
                    int to_write = len;
                    while (to_write > 0) {

                        if(status) {
                            wifi_c_scan_result_t scan_result;
                            char temp_buf[312];
                            wifi_c_scan_all_ap(&scan_result);

                            memset(rx_buffer, 0, sizeof(rx_buffer));
                            memset(temp_buf, 0, sizeof(temp_buf));

                            strcat(&rx_buffer[0], "Scanned APs:");
                            wifi_c_store_scanned_ap(temp_buf, 312);

                            strncat(&rx_buffer[12], &temp_buf[0], strlen(temp_buf));

                            len = strlen(rx_buffer);
                            to_write = len;
                        }

                        int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                        if (written < 0) {
                            ERR_C_SET_AND_THROW_ERR(err, errno);
                            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                            // Failed to retransmit, giving up
                            return errno;
                        }
                        to_write -= written;
                    }
                }
        } while (len > 0);
    } Catch(err) {
        ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
    }

    return err;
}

static void cli_tcp_server_task(void *pvParameters)
{
    err_c_t err = ERR_C_OK;
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = TCP_C_KEEPALIVE_IDLE;
    int keepInterval = TCP_C_KEEPALIVE_INTERVAL;
    int keepCount = TCP_C_KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;


    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(TCP_C_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
    }
    ESP_LOGI(TAG, "Socket bound, port %d", TCP_C_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
    }

    while (1) {

        ESP_LOGD(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string

        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGD(TAG, "Socket accepted ip address: %s", addr_str);

        tcp_c_handle_exchange(sock);

        shutdown(sock, 0);
        close(sock);
    }
}

void tcp_c_start_tcp_server(void) {
    xTaskCreate(cli_tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}