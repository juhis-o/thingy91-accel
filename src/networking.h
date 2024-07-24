#ifndef NETWORKING_H
#define NETWORKING_H

#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/net/tls_credentials.h>

#define COAP_TIMEOUT 3000
#define COAP_MSG_MAX_LEN 1280
#define APP_COAP_VERSION 1
#define SEC_TAG 123
#define POLL_IN 0
#define POLL_OUT 1

union received_msg {
  uint16_t integer;
  uint8_t bytes[2];
};

int connect_to_server(void);
int wait(int timeout, bool status);
int server_resolve(void);
int client_init(void);
void coap_send_fn(struct k_work * work);
int client_handle_get_response(uint8_t *buf, int received,uint8_t *pload);
int client_get_send(uint8_t * payload, uint16_t payloadSize, bool msg_type, uint16_t token);
bool check_list(uint16_t x, uint8_t msg_index);

#endif //NETWORKING_H