/*
 * Generated using zcbor version 0.8.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 256
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_encode.h"
#include "msg_encode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 256
#error "The type file was generated with a different default_max_qty than this file"
#endif

static bool encode_repeated_msg_xyz(zcbor_state_t *state, const struct msg_xyz *input);
static bool encode_msg(zcbor_state_t *state, const struct msg *input);


static bool encode_repeated_msg_xyz(
		zcbor_state_t *state, const struct msg_xyz *input)
{
	zcbor_log("%s\r\n", __func__);

	bool tmp_result = (((zcbor_list_start_encode(state, 3) && (((((((*input).msg_xyz_x >= INT16_MIN)
	&& ((*input).msg_xyz_x <= INT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_int32_encode(state, (&(*input).msg_xyz_x))))
	&& (((((*input).msg_xyz_y >= INT16_MIN)
	&& ((*input).msg_xyz_y <= INT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_int32_encode(state, (&(*input).msg_xyz_y))))
	&& (((((*input).msg_xyz_z >= INT16_MIN)
	&& ((*input).msg_xyz_z <= INT16_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_int32_encode(state, (&(*input).msg_xyz_z))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 3))));

	if (!tmp_result) {
		zcbor_trace_file(state);
		zcbor_log("%s error: %s\r\n", __func__, zcbor_error_str(zcbor_peek_error(state)));
	} else {
		zcbor_log("%s success\r\n", __func__);
	}

	return tmp_result;
}

static bool encode_msg(
		zcbor_state_t *state, const struct msg *input)
{
	zcbor_log("%s\r\n", __func__);

	bool tmp_result = (((zcbor_list_start_encode(state, 257) && ((((zcbor_list_start_encode(state, 4) && (((((((*input).msg_tphg_timestamp <= UINT64_MAX)) || (zcbor_error(state, ZCBOR_ERR_WRONG_RANGE), false))
	&& (zcbor_uint64_encode(state, (&(*input).msg_tphg_timestamp))))
	&& ((zcbor_int32_encode(state, (&(*input).msg_tphg_temp))))
	&& ((zcbor_int32_encode(state, (&(*input).msg_tphg_press))))
	&& ((zcbor_int32_encode(state, (&(*input).msg_tphg_hum))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 4)))
	&& zcbor_multi_encode_minmax(1, 256, &(*input).msg_xyz_count, (zcbor_encoder_t *)encode_repeated_msg_xyz, state, (&(*input).msg_xyz), sizeof(struct msg_xyz))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 257))));

	if (!tmp_result) {
		zcbor_trace_file(state);
		zcbor_log("%s error: %s\r\n", __func__, zcbor_error_str(zcbor_peek_error(state)));
	} else {
		zcbor_log("%s success\r\n", __func__);
	}

	return tmp_result;
}



int cbor_encode_msg(
		uint8_t *payload, size_t payload_len,
		const struct msg *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[4];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_msg, sizeof(states) / sizeof(zcbor_state_t), 1);
}
