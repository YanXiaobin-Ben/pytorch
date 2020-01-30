/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pytorch_qnnpack.h>
#include <qnnpack/log.h>
#include <qnnpack/math.h>
#include <qnnpack/operator.h>
#include <qnnpack/pack.h>
#include <qnnpack/params.h>
#include <qnnpack/requantization.h>

enum pytorch_qnnp_status pytorch_qnnp_create_fully_connected_nc_q8(
    size_t input_channels,
    size_t output_channels,
    uint8_t input_zero_point,
    const uint8_t* kernel_zero_points,
    const uint8_t* kernel,
    const int32_t* bias,
    uint8_t output_zero_point,
    uint8_t output_min,
    uint8_t output_max,
    uint32_t flags,
    const float* requantization_scale,
    const int32_t* multipliers,
    const int32_t* shifts,
    pytorch_qnnp_operator_t* fully_connected_out) {
  pytorch_qnnp_operator_t fully_connected = NULL;
  enum pytorch_qnnp_status status = pytorch_qnnp_status_uninitialized;

  if (!pytorch_qnnp_params.initialized) {
    pytorch_qnnp_log_error(
        "pytorch_qnnp_create_fully_connected_nc_q8 failed because QNNPACK is not properly initialized");
    goto error;
  }

  status = pytorch_qnnp_status_invalid_parameter;

  status = pytorch_qnnp_status_unsupported_parameter;

  // Need to adjust this to check for per channel. Although internally we dont use this path at all.
  // Better remove it at some point.
  for (size_t i = 0; i < output_channels; ++i) {
    if (requantization_scale[i] >= 1.0f) {
      pytorch_qnnp_log_error(
          "failed to create fully connected operator with "
          "requantization scale %.7g is greater or equal to 1.0",
          requantization_scale[i]);
      goto error;
    }
  }

  status = pytorch_qnnp_status_out_of_memory;

  fully_connected = calloc(1, sizeof(struct pytorch_qnnp_operator));
  if (fully_connected == NULL) {
    pytorch_qnnp_log_error(
        "failed to allocate %zu bytes for pytorch_qnnp_operator structure",
        sizeof(struct pytorch_qnnp_operator));
    goto error;
  }

  const uint32_t nr = pytorch_qnnp_params.q8conv.nr;
  const uint32_t kr = pytorch_qnnp_params.q8conv.kr;

  const uint32_t n_stride = (output_channels + (nr - 1)) & -nr;
  const uint32_t k_stride = (input_channels + (kr - 1)) & -kr;

  fully_connected->packed_weights =
      malloc(n_stride * (k_stride * sizeof(uint8_t) + sizeof(int32_t)));
  if (fully_connected->packed_weights == NULL) {
    pytorch_qnnp_log_error(
        "failed to allocate %zu bytes for packed weights",
        n_stride * (k_stride * sizeof(uint8_t) + sizeof(int32_t)));
    goto error;
  }
  memset(
      fully_connected->packed_weights,
      kernel_zero_points[0],
      n_stride * (k_stride * sizeof(uint8_t) + sizeof(int32_t)));

  pytorch_pack_q8gemm_w(
      output_channels,
      input_channels,
      nr,
      nr,
      kr,
#if !PYTORCH_QNNPACK_RUNTIME_QUANTIZATION
      input_zero_point,
      kernel_zero_points[0],
#endif
      kernel,
      bias,
      fully_connected->packed_weights);

  fully_connected->groups = 1;
  fully_connected->group_input_channels = input_channels;
  fully_connected->group_output_channels = output_channels;

  fully_connected->kernel_zero_point = kernel_zero_points[0];

  fully_connected->conv_quantization_params =
      pytorch_qnnp_compute_conv_quantization_params(
          input_zero_point,
          kernel_zero_points,
          multipliers,
          shifts,
          output_zero_point,
          output_min,
          output_max);

  fully_connected->ukernel_type = pytorch_qnnp_ukernel_type_gemm;
  fully_connected->format = pytorch_qnnp_format_quint8;

  *fully_connected_out = fully_connected;
  return pytorch_qnnp_status_success;

error:
  pytorch_qnnp_delete_operator(fully_connected);
  return status;
}

enum pytorch_qnnp_status pytorch_qnnp_setup_fully_connected_nc_q8(
    pytorch_qnnp_operator_t fully_connected,
    size_t batch_size,
    const uint8_t* input,
    size_t input_stride,
    uint8_t* output,
    size_t output_stride) {
  if (!pytorch_qnnp_params.initialized) {
    pytorch_qnnp_log_error(
        "pytorch_qnnp_setup_fully_connected_nc_q8 failed because QNNPACK is not properly initialized");
    return pytorch_qnnp_status_uninitialized;
  }

  if (batch_size == 0) {
    fully_connected->batch_size = 0;
    return pytorch_qnnp_status_success;
  }

  fully_connected->batch_size = 1;
  fully_connected->input_height = batch_size;
  fully_connected->input_width = 1;
  fully_connected->input = input;
  fully_connected->input_pixel_stride = input_stride;

  fully_connected->output_height = batch_size;
  fully_connected->output_width = 1;
  fully_connected->output = output;
  fully_connected->output_pixel_stride = output_stride;

  return pytorch_qnnp_status_success;
}
