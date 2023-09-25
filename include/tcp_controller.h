#pragma once

#define TCP_C_PORT                        27015
#define TCP_C_KEEPALIVE_IDLE              10              ///< TCP keep-alive idle time(s).
#define TCP_C_KEEPALIVE_INTERVAL          10              ///< TCP keep-alive interval time(s).
#define TCP_C_KEEPALIVE_COUNT             5               ///< TCP keep-alive packet retry send counts.
#define TCP_C_TRANSMIT_BUFLEN             128

void tcp_c_start_tcp_server(void);