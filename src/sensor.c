#include "sensor.h"
#include "heatshrink_encoder.h"
#include "msg_encode.h"
#include "shared.h"

LOG_MODULE_REGISTER(SENSORS,4);

uint16_t collected_accel = 0;
struct msg encode_msg_buf;

const struct device *adxl362;
const struct device *bme680;
struct sensor_trigger trig = { .chan = SENSOR_ADXL_FIFO_RAW, .type = SENSOR_TRIG_FIFO};

extern bool send_flag;
extern uint8_t msg_index;
extern struct message_payload message[2][PAYLOAD_COUNT];

extern struct z_thread_stack_element networking_stack;
extern struct k_work_q networking_work_q;
extern struct k_work empty_fifo;
extern struct k_work coap_send;

int initialize_sensors(void) {
    int err = 0;
    bme680 = DEVICE_DT_GET_ONE(bosch_bme680);
	adxl362 = DEVICE_DT_GET_ONE(adi_adxl362);

    if((!device_is_ready(adxl362)) && (!device_is_ready(bme680))) {
		LOG_INF("Device(s) %s or %s are not ready\n", adxl362->name,bme680->name);
		return err;
	}

	if(IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		LOG_INF("trig chan: %d, type: %d",trig.chan,trig.type);
		if (sensor_trigger_set(adxl362, &trig, trigger_handler)) {
			LOG_INF("Trigger set error\n");
			return err;
		}
	}
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


int cbor_bit_width(long int number) {

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


int cbor_bit_width_timestamp(uint64_t number) {

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


void empty_fifo_fn(struct k_work *work) {
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
	if(send_flag)
		k_work_submit_to_queue(&networking_work_q,&coap_send);
}

void trigger_handler(const struct device *dev,
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


