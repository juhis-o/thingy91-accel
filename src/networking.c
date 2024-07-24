#include <zephyr/logging/log.h>
#include "networking.h"
#include "shared.h"

LOG_MODULE_REGISTER(NETWORKING,4);

extern bool send_flag;
extern uint8_t msg_index;
extern struct message_payload message[2][PAYLOAD_COUNT];

uint8_t coap_buf[COAP_MSG_MAX_LEN];
static int sock;
static uint16_t con_token;

bool connected = false;
int cid_supported = true;
struct pollfd fds;
struct sockaddr_storage server;

enum {
	STATE_READ,
	STATE_WRITE
} state = STATE_WRITE;


int connect_to_server(void){
    int err = 0;
    err = server_resolve();
	if (err < 0) {
		LOG_ERR("Failed to resolve server name\n");
		return err;
	}
	
	err = client_init();
	if (err < 0) {
		LOG_ERR("Failed to initialize CoAP client\n");
		return err;
	}

    return err;
}

bool check_list(uint16_t x, uint8_t msg_index) {
	bool send = (x & (1u << msg_index) ? true : false);
	return send;
}

uint8_t getmsgCount(uint16_t x) {
    int i = 0;
    for (i = 9; i >= 0; i--){
    	if((x & (1u << i) ? 1 : 0) == 0)
			break;
    }
    return i;
}

void coap_send_fn(struct k_work * work) {
    int err = 0;
    int received = 0;
	uint8_t tries = 0;
	uint8_t t = 0;
	uint8_t LAST_MSG = 9;
	send_flag = false;
	t = (msg_index == 1) ? 0 : 1;
    int64_t next_msg_time = COAP_TIMEOUT;
	union received_msg recv_payload = {0};
	bool reconnected = false;

	for(int i = 0; i < (PAYLOAD_COUNT / 10);) {
		for(uint8_t x = 0; x < (LAST_MSG + 1);) {
			bool msg_type = (x >= LAST_MSG) ? COAP_TYPE_CON : COAP_TYPE_NON_CON;	
			uint8_t index = i*10+x;

			/*  Skip messages, that has arrived to server */
			if (check_list(recv_payload.integer,x)) {
				x++;
				continue;
			}
			LOG_INF("recv: %d i: %d", recv_payload.integer,x);

			err = client_get_send(message[t][index].payload, message[t][index].size, msg_type, index);
			if (err < 0) {
            	LOG_ERR("client_post_send, error code: %d", err);
				if(tries < 3) {
					tries++;
					continue;
				} 
				else {
					LOG_ERR("Failed %d times, restarting device...",tries);
					NVIC_SystemReset();
				}
            }
			err = wait(next_msg_time, msg_type);
			if (err < 0) {
				LOG_ERR("Error in wait function, error code: %d", err);
				if(tries < 3) {
					tries++;
					continue;
				}
				else if(tries == 4 && !reconnected) {
					LOG_ERR("Reconnecting...");
					err = connect_to_server();
					if(err) {
						LOG_ERR("Cannot connect to server. Restarting device");
						NVIC_SystemReset();
					}
					else {
						reconnected = true;
						continue;
					}
				}
				else {
					LOG_ERR("Failed %d times, quitting...",tries);
					NVIC_SystemReset();
				}
			}
			x++;
		}
		received = recv(sock, coap_buf, sizeof(coap_buf),0);
		if (received < 0) {
			LOG_ERR("Socket error %d, quitting...",errno);
			break;			
			}
		if (received == 0)
			LOG_INF("Empty datagram");
		err = client_handle_get_response(coap_buf, received, recv_payload.bytes);
		if (err < 0) {
			if(tries < 3) {
				continue;
			}
			else {
				break;
			}
		}
		LOG_INF("Payload %d", recv_payload.integer);
		/*Everything arrived to server, set to 0*/
		if(recv_payload.integer == 0x3ff) {
			LAST_MSG = 9;
			i++;
			recv_payload.integer = 0;
		}
		//Something went wrong, resend messages in current index
		else {
			if(tries < 3) {
				LAST_MSG = getmsgCount(recv_payload.integer);
				continue;
			}
			else {
				break; 
			}
		}
	}
}

/**@brief Resolves the configured hostname. */
int server_resolve(void) {
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};
	char ipv4_addr[NET_IPV4_ADDR_LEN];

	err = getaddrinfo(CONFIG_COAP_SERVER_HOSTNAME, NULL, &hints, &result);
	if (err != 0) {
		LOG_ERR("ERROR: getaddrinfo failed %d\n", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_ERR("ERROR: Address not found\n");
		return -ENOENT;
	}

	/* IPv4 Address. */
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&server);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_COAP_SERVER_PORT);

	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
	LOG_INF("IPv4 Address found %s\n", ipv4_addr);

	/* Free the address. */
	freeaddrinfo(result);

	return 0;
}

/**@brief Initialize the CoAP client */
int client_init(void) {
	int err;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);
	if (sock < 0) {
		LOG_ERR("Failed to create CoAP socket: %d.", errno);
		return -errno;
	}

	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	int verify = REQUIRED;

	err = setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if(err) {
		LOG_ERR("Failed to setup peer verification, errno %d", errno);
		return -errno;
	}

	int cid_option = TLS_DTLS_CID_SUPPORTED;

	LOG_DBG("Enable connection id");
	err = setsockopt(sock, SOL_TLS, TLS_DTLS_CID, &cid_option, sizeof(cid_option));
	if (!err) {
		cid_supported = true;
	} else if ((err != EOPNOTSUPP) && (err != EINVAL)) {
		LOG_ERR("Error enabling connection ID: %d", -errno);
		cid_supported = false;
	} else {
		LOG_INF("Connection ID not supported by the provided socket");
		cid_supported = false;
	}

	err = setsockopt(sock, SOL_TLS, TLS_HOSTNAME, CONFIG_COAP_SERVER_HOSTNAME, strlen(CONFIG_COAP_SERVER_HOSTNAME));
	if(err) {
		LOG_ERR("Failed to setup TLS hostname (%s), errno %d",
			CONFIG_COAP_SERVER_HOSTNAME, errno);
		return -errno;
	}

	sec_tag_t sec_tag_list[] = { SEC_TAG };
	err = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_list, sizeof(sec_tag_t) * ARRAY_SIZE(sec_tag_list));
	if(err) {
		LOG_ERR("Failed to setup socket security tag, errno %d", errno);
		return -errno;
	}

	err = connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d", errno);
		return -errno;
	}

	fds.fd = sock;
	fds.events = POLLOUT;

	return 0;
}

int client_handle_get_response(uint8_t *buf, int received, uint8_t *pload) {
	int err;
	struct coap_packet reply;
	const uint8_t *payload;
	uint8_t token[8];
	uint16_t payload_len;
	uint16_t token_len;

	err = coap_packet_parse(&reply, buf, received, NULL, 0);
	if(err < 0) {
		LOG_ERR("Malformed response received: %d", err);
		return err;
	}

	token_len = coap_header_get_token(&reply, token);

	if((token_len != sizeof(con_token)) ||
	    (memcmp(&con_token, token, sizeof(con_token)) != 0)) {
		LOG_ERR("Invalid token received: 0x%02x%02x",
		       token[1], token[0]);
		return -1;
	}

	uint8_t response_code = coap_header_get_code(&reply);
	
	if(response_code == COAP_RESPONSE_CODE_CREATED) {
		LOG_INF("Message ok 0x%x", response_code);
		pload[0] = 0xff;
		pload[1] = 0x3;
	}

	else if(response_code == COAP_RESPONSE_CODE_BAD_REQUEST) {
		payload = coap_packet_get_payload(&reply, &payload_len);
		if (payload_len > 0) {
			pload[0] = payload[0];
			pload[1] = payload[1];
		} 	
		else {
			//bad value, resend all
			pload[0] = 0x0;
			pload[1] = 0x4;
		}
		LOG_INF("CoAP response: code: 0x%x, token 0x%02x%02x, Payload: %d,%d",
	    coap_header_get_code(&reply), token[1], token[0], payload[0], payload[1]);
	}

	else {
		LOG_INF("Response code not supported");
		return -1;
	}

	return 0;
}

int client_get_send(uint8_t * payload, uint16_t payloadSize, bool msg_type, uint16_t token) {
	int err;
	struct coap_packet request;

	LOG_INF("State: %d", state);

	if(!msg_type) {
		con_token = token;
	}

	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, msg_type,
			       sizeof(token), (uint8_t *)&token,
			       COAP_METHOD_POST, coap_next_id());
	if(err < 0) {
		LOG_ERR("Failed to create CoAP request, %d", err);
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(uint8_t *)CONFIG_COAP_RESOURCE,
					strlen(CONFIG_COAP_RESOURCE));
	if(err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d", err);
		return err;
	}

	err = coap_packet_append_payload_marker(&request);
		if(err < 0) {
		LOG_ERR("Failed to append CoAP payload marker, %d", err);
		return err;
	}

	err = coap_packet_append_payload(&request, payload,payloadSize);
	if(err < 0) {
		LOG_ERR("Failed to append CoAP payload, %d", err);
		return err;
	}

	err = send(sock, request.data, request.offset, 0);
	if(err < 0) {
		LOG_ERR("Failed to send CoAP request, %d", errno);
		return -errno;
	}

	LOG_INF("CoAP request sent: token 0x%04x", token);

	return 0;
}


int wait(int timeout, bool status) {
	
	fds.events = (!status) ? POLLIN : POLLOUT;
	
	int ret = poll(&fds, 1, timeout);
	if (ret < 0) {
		LOG_ERR("poll error: %d", errno);
		return -errno;
	}

	if (ret == 0) {
		/* Timeout. */
		return -EAGAIN;
	}

	if ((fds.revents & POLLERR) == POLLERR) {
		LOG_ERR("wait: POLLERR");
		return -EIO;
	}

	if ((fds.revents & POLLNVAL) == POLLNVAL) {
		LOG_ERR("wait: POLLNVAL");
		return -EBADF;
	}

	return 0;
}