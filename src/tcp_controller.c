#include "tcp_controller.h"
#include "tcp_c_events.h"
#include "err_controller.h"
#include "wifi_controller.h"

#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "freertos/task.h"
#include "inttypes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


static const char* TAG = "tcp_controller";


static void tcp_c_prepare_sock_addr(struct sockaddr_storage *dest_addr);
static int tcp_c_prepare_listen_socket(void);
static int tcp_c_bind_socket(int socket, struct sockaddr_storage *dest_addr);
static int tcp_c_listen_on_socket(int socket);
static int tcp_c_accept_socket(int socket);
static int tcp_c_close_socket(int socket);

static EventGroupHandle_t tcp_event_group;

static int listen_socket = 0;
static int connected_socket = 0;

int tcp_c_send(const char tx_buffer[], int buflen) {
    volatile err_c_t err = ERR_C_OK;
    int to_write = buflen;
    while (to_write > 0) {
        int written = send(connected_socket, tx_buffer+(buflen-to_write), to_write, 0);
        if (written < 0) {
            err = errno;
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            TCP_C_LOG_ERR(err)
            tcp_c_close_socket(connected_socket);
            xEventGroupSetBits(tcp_event_group, TCP_C_FINISHED_TRANSMISSION); //there was error, finish current transmission
            return err;
        }
        to_write -= written;
    }
    xEventGroupSetBits(tcp_event_group, TCP_C_SENDED_DATA_BIT);
    return err;
}

int tcp_c_receive(char rx_buffer[]) {
    volatile err_c_t err = ERR_C_OK;
    int len;
    memset(rx_buffer, 0, TCP_C_RECEIVE_BUFLEN);
    Try {
        do {
            len = recv(connected_socket, rx_buffer, TCP_C_RECEIVE_BUFLEN-1, 0);
            if(len < 0) {
                ERR_C_SET_AND_THROW_ERR(err, errno);
            } else if (len == 0) {
                ESP_LOGD(TAG, "Connection closed.");
            } else {
                rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
                ESP_LOGD(TAG, "Received %d bytes.", len);
                ESP_LOGD(TAG, "%s", rx_buffer);
            }
        } while (len > 0);

    } Catch(err) {
        err = errno;
        ESP_LOGE(TAG, "Error during receiving data, errno: %d", err);
        TCP_C_LOG_ERR(err)
        tcp_c_close_socket(connected_socket);
        xEventGroupSetBits(tcp_event_group, TCP_C_FINISHED_TRANSMISSION); //there was error, finish current transmission
    }
    xEventGroupSetBits(tcp_event_group, TCP_C_RECEIVED_DATA_BIT);
    return err;
}

static void tcp_c_prepare_sock_addr(struct sockaddr_storage *dest_addr) {
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(TCP_C_PORT);
}

static int tcp_c_prepare_listen_socket(void) {
    int listen_sock = socket(TCP_C_ADDR_FAMILY, SOCK_STREAM, TCP_C_IP_PROTOCOL);
    volatile err_c_t err = ERR_C_OK;
    Try {
        if (listen_sock < 0) {
            ERR_C_SET_AND_THROW_ERR(err, errno);
        }
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    } Catch(err) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", err);
        TCP_C_LOG_ERR(err)
        Throw(err);
    }

    return listen_sock;
}

static int tcp_c_bind_socket(int socket, struct sockaddr_storage *dest_addr) {
    volatile err_c_t err = ERR_C_OK;
    Try {
        err = bind(socket, (struct sockaddr *)dest_addr, sizeof(struct sockaddr_storage));
        ERR_C_CHECK_AND_THROW_ERR(err);
    } Catch(err) {
        err = errno;
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", err);
        TCP_C_LOG_ERR(err)
    }

    return err;
}

static int tcp_c_listen_on_socket(int socket) {
    volatile err_c_t err = ERR_C_OK;
    Try {
        err = listen(socket, TCP_C_BACKLOG_NUM);
        ERR_C_CHECK_AND_THROW_ERR(err);
    } Catch(err) {
        err = errno;
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", err);
        TCP_C_LOG_ERR(err)
    }

    return err;
}

static int tcp_c_accept_socket(int socket) {
    volatile err_c_t err = ERR_C_OK;
    volatile int sock;
    char addr_str[128];
    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t addr_len = sizeof(source_addr);
    int keep_alive = TCP_C_KEEPALIVE;
    int keep_idle = TCP_C_KEEPALIVE_IDLE;
    int keep_interval = TCP_C_KEEPALIVE_INTERVAL;
    int keep_count = TCP_C_KEEPALIVE_COUNT;

    Try {
        sock = accept(socket, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ERR_C_SET_AND_THROW_ERR(err, errno);
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(int));
        
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }

        ESP_LOGD(TAG, "Socket accepted ip address: %s", addr_str);
    } Catch (err) {
        ESP_LOGE(TAG, "Unable to accept connection: errno %d", err);
        TCP_C_LOG_ERR(err)
    }

    return sock;
}

static int tcp_c_close_socket(int socket) {
    //Close the connection.
    shutdown(socket, 0);
    close(socket);
    return 0;
}

void tcp_c_server_loop(void)
{
    volatile err_c_t err = ERR_C_OK;
    Try {
        while (1) {
            ESP_LOGD(TAG, "Socket listening");
            //Accept new connection
            connected_socket = tcp_c_accept_socket(listen_socket);
            //Inform that new connection was accepted.
            xEventGroupSetBits(tcp_event_group, TCP_C_ACCEPTED_SOCKET_BIT);
            //Wait till transmission is finished.
            xEventGroupWaitBits(tcp_event_group, TCP_C_FINISHED_TRANSMISSION, pdTRUE, pdFALSE, pdMS_TO_TICKS(3500));
            //Close the connection.
            tcp_c_close_socket(connected_socket);
        }
    } Catch(err) {
        ESP_LOGE(TAG, "error: %d", err);
    }
}

int tcp_c_start_tcp_server(EventGroupHandle_t event_group) {
    volatile err_c_t err = ERR_C_OK;
    tcp_event_group = event_group;
    struct sockaddr_storage dest_addr;
    Try {
        tcp_c_prepare_sock_addr(&dest_addr);
        listen_socket = tcp_c_prepare_listen_socket();
        ESP_LOGI(TAG, "Socket created");
        ERR_C_CHECK_AND_THROW_ERR(tcp_c_bind_socket(listen_socket, &dest_addr));
        ESP_LOGI(TAG, "Socket bound, port: %d", TCP_C_PORT);
        ERR_C_CHECK_AND_THROW_ERR(tcp_c_listen_on_socket(listen_socket));
    } Catch(err) {
        ESP_LOGE(TAG, "Error during starting tcp server: %d", err);
    }

    return listen_socket;
}