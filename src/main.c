#include "main.h"
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/net/tls_credentials.h>
#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>

LOG_MODULE_REGISTER(MAIN,4);
K_SEM_DEFINE(lte_connected, 0, 1);

static K_THREAD_STACK_DEFINE(networking_stack, WORKQ_STACK);
static struct k_work_q networking_work_q = {0};
static struct k_work empty_fifo;
static struct k_work coap_send;

const struct device *adxl362;
const struct device *bme680;

static struct pollfd fds;
static struct sockaddr_storage server;

struct msg encode_msg_buf;
struct message_payload message[2][PAYLOAD_COUNT];

bool send_flag = false;

static int sock;
static uint16_t con_token;
uint16_t collected_accel = 0;
uint16_t messagesToSend;
static uint8_t coap_buf[COAP_MSG_MAX_LEN];
uint8_t msg_index = 0;

struct sensor_trigger trig = { .chan = SENSOR_ADXL_FIFO_RAW, .type = SENSOR_TRIG_FIFO};

enum {
	STATE_READ,
	STATE_WRITE
} state = STATE_WRITE;

static bool check_list(uint16_t x, uint8_t msg_index) {
	bool send = (x & (1u << msg_index) ? true : false);
	return send; 
}

static uint8_t getmsgCount(uint16_t x ) {
    int i = 0;
    for (i = 9; i >= 0; i--){
    	if((x & (1u << i) ? 1 : 0) == 0)
			break;
    }
    return i;
}

static void coap_send_fn(struct k_work * work) {
    int err = 0;
    int received = 0;
	uint8_t tries = 0;
	uint8_t t = 0;
	uint8_t LAST_MSG = 9;
	send_flag = false;
	t = (msg_index == 1) ? 0 : 1;
    int64_t next_msg_time = COAP_TIMEOUT;
	union received_msg recv_payload = {0};
	
	err = server_resolve();
	if (err < 0) {
		LOG_ERR("Failed to resolve server name\n");
		return;
	}
	err = client_init();
	if (err < 0) {
		LOG_ERR("Failed to initialize CoAP client\n");
		return;
	}

	for (int i = 0; i < (PAYLOAD_COUNT / 10);) {
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
					LOG_ERR("Failed %d times, quitting...",tries);
					return;
				}
            }
			err = wait(next_msg_time, msg_type);
			if (err < 0) {
				LOG_ERR("Error in wait function, error code: %d", err);
				if(tries < 3) {
					tries++;
					continue;
				}
				else {
					LOG_ERR("Failed %d times, quitting...",tries);
					return;
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
	(void)close(sock);
}

static int configure_low_power(void) {
	int err;

#if defined(CONFIG_UDP_PSM_ENABLE)
	/** Power Saving Mode */
	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d\n", err);
	}
#else
	err = lte_lc_psm_req(false);
	if (err) {
		LOG_ERR("lte_lc_psm_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_EDRX_ENABLE)
	/** enhanced Discontinuous Reception */
	err = lte_lc_edrx_req(true);
	if (err) {
		LOG_ERR("lte_lc_edrx_req, error: %d\n", err);
	}
#else
	err = lte_lc_edrx_req(false);
	if (err) {
		LOG_ERR("lte_lc_edrx_req, error: %d\n", err);
	}
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
	/** Release Assistance Indication  */
	err = lte_lc_rai_req(true);
	if (err) {
		LOG_ERR("lte_lc_rai_req, error: %d\n", err);
	}
#endif

	return err;
}

uint16_t heatshrink_compression(uint8_t* input_data, uint16_t input_len, uint8_t* output_data) {
    heatshrink_encoder *hse = heatshrink_encoder_alloc(9,3);
    if(hse == NULL) {
        LOG_ERR("Encoder malloc failed");
        return 0;
    }

    uint16_t sunk = 0;
    uint16_t polled = 0;
    size_t count = 0;
    while(sunk < input_len) {
        heatshrink_encoder_sink(hse, &input_data[sunk], input_len - sunk, &count);
        sunk += count;
        if(sunk == input_len) {
            heatshrink_encoder_finish(hse);
        }

        HSE_poll_res pres;
        do {
            pres = heatshrink_encoder_poll(hse, &output_data[polled], input_len - polled, &count);
            polled += count;
        } while(pres == HSER_POLL_MORE);

        if (polled >= input_len) { 
            break;
        }
		
        if(sunk == input_len) {
            heatshrink_encoder_finish(hse);
        }
    }
    heatshrink_encoder_free(hse);
    return polled;
}


static int cbor_bit_width(long int number) {

	if ((number >= -24) && (number <= 23)) {
		return 1;
	}
	else if ((number >= -256) && (number <= 255)) {
		return 2;
	}
	else if ((number >= -65536) && (number <= 65535)) {
		return 3;
	}
	else {
		return 5;
	}
}


static int cbor_bit_width_timestamp(uint64_t number) {

	if (number <= 23) {
		return 1;
	}
	else if (number <= 255) {
		return 2;
	}
	else if (number <= 65535) {
		return 3;
	}
	else if (number <= 4294967295) {
		return 5;
	}
	else {
		return 9;
	}
}


static void empty_fifo_fn(struct k_work *work) {
	int err;
	size_t cbor_len = 0;
	struct sensor_value temp1, press, humidity;
	uint64_t timestamp;
	int16_t accel[FIFO_SIZE];
	uint16_t compression_size;
	uint8_t temporary[1200];
	timestamp = k_uptime_get();
	err = sensor_sample_fetch(adxl362);
	if (err < 0) {
		LOG_ERR("Sample fetch error code: %d\n", err);
		return;
	}
	if (sensor_fifo_get(adxl362, accel) < 0) {
		LOG_ERR("Channel get error\n");
		return;
	}

	sensor_sample_fetch(bme680);
	sensor_channel_get(bme680, SENSOR_CHAN_AMBIENT_TEMP, &temp1);
	sensor_channel_get(bme680, SENSOR_CHAN_PRESS, &press);
	sensor_channel_get(bme680, SENSOR_CHAN_HUMIDITY, &humidity);

	encode_msg_buf.msg_tphg_temp = temp1.val1;
	encode_msg_buf.msg_tphg_press = ((press.val1*1000)+(press.val2/1000));
	encode_msg_buf.msg_tphg_hum = ((humidity.val1*1000)+(humidity.val2/1000));
	encode_msg_buf.size += cbor_bit_width(encode_msg_buf.msg_tphg_temp)+cbor_bit_width(encode_msg_buf.msg_tphg_press)+cbor_bit_width(encode_msg_buf.msg_tphg_hum)+cbor_bit_width_timestamp(encode_msg_buf.msg_tphg_timestamp) + TABLE_BYTES + ARRAY_BYTE;

	//Jatketaan siitä, mihin jäätiin
	int encode_index = encode_msg_buf.msg_xyz_count;
	int accel_index = 0;
	//Käsitellään kaikki FIFO arvot
	while(accel_index < FIFO_SIZE) {
		//accel_indeksin osoittamien X, Y ja Z lukujen koko lasketaan yhteensä CBOR arvona
		uint8_t elementSize = 0;
		elementSize = cbor_bit_width(accel[accel_index]) + cbor_bit_width(accel[accel_index+1]) + cbor_bit_width(accel[accel_index+2]);
		if(encode_msg_buf.size+elementSize+ARRAY_BYTE > PAYLOAD_SIZE ) {
			//Enkoodattavien arvojen määrä asetetaan
			encode_msg_buf.msg_xyz_count = encode_index;
			err = cbor_encode_msg(temporary, sizeof(temporary), &encode_msg_buf, &cbor_len);
			if (err != ZCBOR_SUCCESS) {
				//Tyhjennä ja yritä koodata jäljellä olevat arvot
				LOG_ERR("Encoding failed : %d\r\n", err);
				memset(&encode_msg_buf,0,sizeof(encode_msg_buf));
				encode_msg_buf.size += cbor_bit_width(encode_msg_buf.msg_tphg_temp)+cbor_bit_width(encode_msg_buf.msg_tphg_press)+cbor_bit_width(encode_msg_buf.msg_tphg_hum)+cbor_bit_width_timestamp(encode_msg_buf.msg_tphg_timestamp)+ARRAY_BYTE+TABLE_BYTES;
				encode_index = 0;
			}
			else {
				//Yritetään pakata heatshrink pakkauksella
				compression_size = heatshrink_compression(temporary, cbor_len, message[msg_index][collected_accel].payload);
				if(compression_size > 0) {
					message[msg_index][collected_accel].size = compression_size;
				}
				else {
					LOG_ERR("Compression failed");
					//Asetetaan CBOR-koodatut arvot lähetettäväksi, jos ei onnistu
					memcpy(message[msg_index][collected_accel].payload,temporary,cbor_len);
					message[msg_index][collected_accel].size = cbor_len;
				}

				collected_accel++;
				memset(&encode_msg_buf,0,sizeof(encode_msg_buf));
				encode_index = 0;
				//Lasketaan tavut
				encode_msg_buf.size += cbor_bit_width(encode_msg_buf.msg_tphg_temp)+cbor_bit_width(encode_msg_buf.msg_tphg_press)+cbor_bit_width(encode_msg_buf.msg_tphg_hum)+cbor_bit_width_timestamp(encode_msg_buf.msg_tphg_timestamp)+ARRAY_BYTE+TABLE_BYTES;
				if (collected_accel == PAYLOAD_COUNT) {
					msg_index = (msg_index == 1) ? 0 : 1;
					send_flag = true;
					collected_accel = 0;
				}			
			}
		}
		encode_msg_buf.msg_xyz[encode_index].msg_xyz_x = accel[accel_index];
		encode_msg_buf.msg_xyz[encode_index].msg_xyz_y = accel[accel_index+1];
		encode_msg_buf.msg_xyz[encode_index].msg_xyz_z = accel[accel_index+2];
		encode_msg_buf.size+=elementSize+ARRAY_BYTE;
		encode_index++;
		accel_index+=3;
	}
	//Tallennetaan indeksi
	encode_msg_buf.msg_xyz_count = encode_index;
	//Vähennetään 1 tavun kokoiset elementit taulukosta
	encode_msg_buf.size -= cbor_bit_width(encode_msg_buf.msg_tphg_temp)+cbor_bit_width(encode_msg_buf.msg_tphg_press)+cbor_bit_width(encode_msg_buf.msg_tphg_hum)+cbor_bit_width_timestamp(encode_msg_buf.msg_tphg_timestamp)+ARRAY_BYTE+TABLE_BYTES;
	encode_msg_buf.msg_tphg_timestamp = timestamp;
	if(send_flag) k_work_submit_to_queue(&networking_work_q,&coap_send);
}

/**@brief Resolves the configured hostname. */
static int server_resolve(void) {
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
static int client_init(void) {
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
	
	err = connect(sock, (struct sockaddr *)&server,
		      sizeof(struct sockaddr_in));
	if (err < 0) {
		LOG_ERR("Connect failed : %d", errno);
		return -errno;
	}
	LOG_INF("Successfully connected to server");

	/* Initialize FDS, for poll. */
	fds.fd = sock;
	fds.events = POLLOUT;

	return 0;
}

static int client_handle_get_response(uint8_t *buf, int received, uint8_t *pload) {
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

static int client_get_send(uint8_t * payload, uint16_t payloadSize, bool msg_type, uint16_t token) {
	int err;
	struct coap_packet request;

	LOG_INF("State: %d", state);

	if(!msg_type) {
		con_token = token;
	}

	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, msg_type,//COAP_TYPE_NON_CON,
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

static void lte_handler(const struct lte_lc_evt *const evt) {
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		     (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}
		LOG_INF("Network registration status: %s\n",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming\n");
		k_sem_give(&lte_connected);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_INF("PSM parameter update: TAU: %d, Active time: %d\n",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;
		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f\n",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			LOG_INF("%s\n", log_buf);
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_INF("RRC mode: %s\n",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle\n");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_INF("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
		       evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static int modem_configure(void) {
	int err;

	LOG_INF("Initializing modem library");
	err = nrf_modem_lib_init();
	if (err) {
		LOG_ERR("Failed to initialize the modem library, error: %d", err);
		return err;
	}

	LOG_INF("Connecting to LTE network");
	err = lte_lc_connect_async(lte_handler);
	if (err) {
		LOG_ERR("Error in lte_lc_connect_async, error: %d", err);
		return err;
	}

	k_sem_take(&lte_connected, K_FOREVER);
	LOG_INF("Connected to LTE network");

	return 0;
}

static int wait(int timeout, bool status) {
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


static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig) {
	switch (trig->type) {
	case SENSOR_TRIG_FIFO:
		LOG_INF("trig handler");
		k_work_submit(&empty_fifo);
		break;
	default:
		LOG_INF("Unknown trigger Type: %d, Channel: %d", (int)trig->type, (int)trig->chan);
	}
}

int main(void){
	int err = 0;
	bme680 = DEVICE_DT_GET_ONE(bosch_bme680);
	adxl362 = DEVICE_DT_GET_ONE(adi_adxl362);

	if((!device_is_ready(adxl362)) && (!device_is_ready(bme680))) {
		LOG_INF("Device(s) %s or %s are not ready\n", adxl362->name,bme680->name);
		return -1;
	}

	if(IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		LOG_INF("trig chan: %d, type: %d",trig.chan,trig.type);
		if (sensor_trigger_set(adxl362, &trig, trigger_handler)) {
			LOG_INF("Trigger set error\n");
			return -1;
		}
	}

	err = modem_configure();
	if(err) {
		LOG_ERR("Unable to configure modem, error: %d\n",
		       err);
	}
	err = configure_low_power();
	if(err) {
		LOG_ERR("Unable to set low power configuration, error: %d\n",
		       err);
	}

	k_work_init(&coap_send, coap_send_fn);
	
	k_work_queue_start(&networking_work_q, networking_stack,
                   K_THREAD_STACK_SIZEOF(networking_stack), 4,
                   NULL);

	k_work_init(&empty_fifo, empty_fifo_fn);
	
	return 0;
}
