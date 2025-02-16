/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#include "kms_mst_helper.h"

/*
 * @drm_fd: DRM file descriptor
 * @display: pointer to #igt_display_t structure
 * @output: target output
 * @mst_outputs: filled with mst output of same toplogy as @output
 * @num_mst_outputs: filled with count of mst outputs found in topology
 *
 * Iterates over all connected outputs and adds each DP MST
 * output that shares the same MST connector ID as @output
 * into @mst_outputs.
 *
 * Returns: 0 on success, -error on failure
 */
int igt_find_all_mst_output_in_topology(int drm_fd, igt_display_t *display,
					igt_output_t *output,
					igt_output_t *mst_outputs[],
					int *num_mst_outputs)
{
	int output_root_id, root_id;
	igt_output_t *connector_output;

	if (!igt_check_output_is_dp_mst(output))
		return -EINVAL;

	output_root_id = igt_get_dp_mst_connector_id(output);
	if (output_root_id == -EINVAL)
		return -EINVAL;
	/*
	 * If output is MST check all other connected output which shares
	 * same path and fill mst_outputs and num_mst_outputs
	 */
	for_each_connected_output(display, connector_output) {
		if (!igt_check_output_is_dp_mst(connector_output))
			continue;

		root_id = igt_get_dp_mst_connector_id(connector_output);
		if (((*num_mst_outputs) < IGT_MAX_PIPES) && root_id == output_root_id)
			mst_outputs[(*num_mst_outputs)++] = connector_output;
	}
	return 0;
}
