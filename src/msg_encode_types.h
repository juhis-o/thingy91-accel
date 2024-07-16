/*
 * Generated using zcbor version 0.8.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 256
 */

#ifndef MSG_ENCODE_TYPES_H__
#define MSG_ENCODE_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

/** Which value for --default-max-qty this file was created with.
 *
 *  The define is used in the other generated file to do a build-time
 *  compatibility check.
 *
 *  See `zcbor --help` for more information about --default-max-qty
 */
#define DEFAULT_MAX_QTY 256

struct msg_xyz {
	int32_t msg_xyz_x;
	int32_t msg_xyz_y;
	int32_t msg_xyz_z;
};

struct msg {
	uint64_t msg_tphg_timestamp;
	int32_t msg_tphg_temp;
	int32_t msg_tphg_press;
	int32_t msg_tphg_hum;
	struct msg_xyz msg_xyz[256];
	size_t msg_xyz_count;
	uint16_t size;
};

#ifdef __cplusplus
}
#endif

#endif /* MSG_ENCODE_TYPES_H__ */
