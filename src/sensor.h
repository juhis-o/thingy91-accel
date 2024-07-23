#include <zcbor_common.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#define FIFO_SIZE 510
#define ARRAY_BYTE 1
#define TABLE_BYTES 2

void empty_fifo_fn(struct k_work *work);
uint16_t heatshrink_compression(uint8_t* input_data, uint16_t input_len, uint8_t* output_data);
int cbor_bit_width(long int number);
int cbor_bit_width_timestamp(uint64_t number);
void trigger_handler(const struct device *dev, const struct sensor_trigger *trig);
int initialize_sensors(void);