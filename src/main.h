#include "heatshrink_encoder.h"
#include "msg_encode.h"
#include "zcbor_common.h"

#define COAP_TIMEOUT 3000
#define COAP_MSG_MAX_LEN 1280
#define APP_COAP_VERSION 1
#define SEC_TAG 123
#define FIFO_SIZE 510
#define PAYLOAD_SIZE 1024
#define PAYLOAD_COUNT 60
#define ARRAY_BYTE 1
#define TABLE_BYTES 2
#define POLL_IN 0
#define POLL_OUT 1
#define WORKQ_STACK 4096

static int wait(int timeout, bool status);
static void coap_send_fn(struct k_work * work);
static void empty_fifo_fn(struct k_work *work);
static int server_resolve(void);
static int client_init(void);
static int client_handle_get_response(uint8_t *buf, int received,uint8_t *pload);
static int client_get_send(uint8_t * payload, uint16_t payloadSize, bool msg_type, uint16_t token);
static int modem_configure(void);
static void lte_handler(const struct lte_lc_evt *const evt);
static int configure_low_power(void);
static bool check_list(uint16_t x, uint8_t msg_index);

union received_msg {
  uint16_t integer;
  uint8_t bytes[2];
};

struct message_payload {
	uint16_t size;
	uint8_t payload[1100];
};