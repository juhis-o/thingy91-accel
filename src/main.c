#include <stdio.h>
#include "networking.h"
#include "main.h"
#include "modem.h"
#include "sensor.h"
#include "shared.h"

#define WORKQ_STACK 4096

LOG_MODULE_REGISTER(MAIN,4);

K_THREAD_STACK_DEFINE(networking_stack, WORKQ_STACK);
struct k_work_q networking_work_q = {0};
struct k_work empty_fifo;
struct k_work coap_send;

extern bool send_flag;
extern uint8_t msg_index;

int main(void){
	int err = 0;
	send_flag = false;
	msg_index = 0;

	err = initialize_sensors();
	if (err < 0) {
		LOG_ERR("Failed to initialize sensors\n");
		return -1;
	}
	err = initialize_modem();
	if (err < 0) {
		LOG_ERR("Failed to initialize modem\n");
		return -1;
	}
	err = connect_to_server();
	if (err < 0) {
		LOG_ERR("Failed to connect\n");
		return -1;
	}

	k_work_init(&coap_send, coap_send_fn);
	
	k_work_queue_start(&networking_work_q, networking_stack,
                   K_THREAD_STACK_SIZEOF(networking_stack), 4,
                   NULL);

	k_work_init(&empty_fifo, empty_fifo_fn);
	
	return 0;
}
