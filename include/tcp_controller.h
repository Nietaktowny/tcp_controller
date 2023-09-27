#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TCP_C_PORT                        27015
#define TCP_C_KEEPALIVE_IDLE              5              ///< TCP keep-alive idle time(s).
#define TCP_C_KEEPALIVE_INTERVAL          5              ///< TCP keep-alive interval time(s).
#define TCP_C_KEEPALIVE_COUNT             3               ///< TCP keep-alive packet retry send counts.
#define TCP_C_KEEPALIVE                   1
#define TCP_C_RECEIVE_BUFLEN              512
#define TCP_C_SEND_BUFLEN                 512
#define TCP_C_ADDR_FAMILY                 AF_INET
#define TCP_C_IP_PROTOCOL                 IPPROTO_IP

int tcp_c_start_tcp_server(EventGroupHandle_t event_group);
void tcp_c_server_loop(void);
int tcp_c_receive(char rx_buffer[]);
int tcp_c_send(const char tx_buffer[], int buflen);