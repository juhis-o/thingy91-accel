#include <zephyr/kernel.h>

#define PAYLOAD_SIZE 1024
#define PAYLOAD_COUNT 60

struct message_payload {
	uint16_t size;
	uint8_t payload[1100];
};