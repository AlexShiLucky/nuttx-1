/*
 * Copyright (c) 2015 Motorola Mobility, LLC.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <debug.h>
#include <stdlib.h>

#include <arch/byteorder.h>
#include <apps/ice/cdsi.h>
#include <nuttx/device_cam_ext.h>
#include <nuttx/greybus/greybus.h>
#include <nuttx/gpio.h>

//#define DEBUG
#include "camera_ext_dbg.h"

//support only one camera_ext device for now
#define CAMERA_EXT_DEVICE_ID 0

/* Version of the Greybus camera protocol we support */
#define GB_CAMERA_EXT_VERSION_MAJOR 0x00
#define GB_CAMERA_EXT_VERSION_MINOR 0x01

/* Greybus camera request types */
#define GB_CAMERA_TYPE_INVALID          0x00
#define GB_CAMERA_TYPE_PROTOCOL_VERSION 0x01

#define GB_CAMERA_TYPE_POWER_ON         0x02
#define GB_CAMERA_TYPE_POWER_OFF        0x03

#define GB_CAMERA_EXT_TYPE_INPUT_ENUM   0x04
#define GB_CAMERA_EXT_TYPE_INPUT_GET    0x05
#define GB_CAMERA_EXT_TYPE_INPUT_SET    0x06

#define GB_CAMERA_EXT_TYPE_FMT_ENUM     0x07
#define GB_CAMERA_EXT_TYPE_FMT_GET      0x08
#define GB_CAMERA_EXT_TYPE_FMT_SET      0x09

#define GB_CAMERA_EXT_TYPE_FMSIZE_ENUM  0x0A
#define GB_CAMERA_EXT_TYPE_FRMIVAL_ENUM 0x0B

#define GB_CAMERA_EXT_TYPE_STREAM_ON    0x0C
#define GB_CAMERA_EXT_TYPE_STREAM_OFF   0x0D

#define GB_CAMERA_EXT_TYPE_STREAM_PARM_SET 0x0E
#define GB_CAMERA_EXT_TYPE_STREAM_PARM_GET 0x0F

struct gb_proto_version_response {
    uint8_t major;
    uint8_t minor;
};

struct gb_camera_ext_info {
    char          *dev_type;
    struct device *dev;
};

static struct gb_camera_ext_info dev_info = {
    .dev_type  = DEVICE_TYPE_CAMERA_EXT_HW,
};

static uint8_t gb_operation_errno_map(int code)
{
    switch (code) {
    case 0:
        return GB_OP_SUCCESS;
    case -EINTR:
        return GB_OP_INTERRUPTED;
    case -ETIMEDOUT:
        return GB_OP_TIMEOUT;
    case -ENOMEM:
        return GB_OP_NO_MEMORY;
    case -EPROTONOSUPPORT:
        return GB_OP_PROTOCOL_BAD;
    case -EMSGSIZE:
        return GB_OP_OVERFLOW;  /* Could be underflow too */
    case -EINVAL:
        return GB_OP_INVALID;
    case -EAGAIN:
        return GB_OP_RETRY;
    case -ENODEV:
        return GB_OP_NONEXISTENT;
    case -EIO:
    default:
        return GB_OP_UNKNOWN_ERROR;
    }
}

static int gb_camera_ext_init(void)
{
    CAM_DBG("init camera device\n");
    if (dev_info.dev != NULL) {
        CAM_DBG("device not closed in last session\n");
        device_close(dev_info.dev);
    }
    dev_info.dev = device_open(dev_info.dev_type, CAMERA_EXT_DEVICE_ID);
    if (!dev_info.dev) {
        CAM_ERR("failed to open camera device\n");
        return -EIO;
    }
    return 0;
}

static void gb_camera_ext_exit(void)
{
    if (dev_info.dev != NULL) {
        device_close(dev_info.dev);
    }
    dev_info.dev = NULL;
}

static uint8_t gb_camera_protocol_version(struct gb_operation *operation)
{
    struct gb_proto_version_response *response;

    response = gb_operation_alloc_response(operation, sizeof(*response));
    if (!response)
        return GB_OP_NO_MEMORY;

    response->major = GB_CAMERA_EXT_VERSION_MAJOR;
    response->minor = GB_CAMERA_EXT_VERSION_MINOR;

    return GB_OP_SUCCESS;
}

static uint8_t gb_camera_power_on(struct gb_operation *operation)
{
    int retval;

    retval = gb_camera_ext_init();
    if (retval == 0) {
        retval = CALL_CAM_DEV_OP(dev_info.dev, power_on);
        if (retval != 0) {
            gb_camera_ext_exit();
        }
    }

    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_power_off(struct gb_operation *operation)
{
    int retval = CALL_CAM_DEV_OP(dev_info.dev, power_off);
    CAM_DBG("result %d\n", retval);

    gb_camera_ext_exit();
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_stream_on(struct gb_operation *operation)
{
    int retval = CALL_CAM_DEV_OP(dev_info.dev, stream_on);
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_stream_off(struct gb_operation *operation)
{
    int retval = CALL_CAM_DEV_OP(dev_info.dev, stream_off);
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_input_enum(struct gb_operation *operation)
{
    int retval;
    __le32 *index;
    struct camera_ext_input* response;

    if (gb_operation_get_request_payload_size(operation) == sizeof(__le32)) {
        index = (__le32*)gb_operation_get_request_payload(operation);
        response = gb_operation_alloc_response(operation,
            sizeof(struct camera_ext_input));
        if (response != NULL) {
            response->index = *index;
            retval = CALL_CAM_DEV_OP(dev_info.dev, input_enum, response);
        } else {
            retval = -ENOMEM;
        }
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }

    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_input_get(struct gb_operation *operation)
{
    int retval;
    __le32 *response;
    int index = -1;

    retval = CALL_CAM_DEV_OP(dev_info.dev, input_get, &index);
    if (retval == 0) {
        response = gb_operation_alloc_response(operation, sizeof(__le32));
        if (response != NULL) {
            *response = cpu_to_le32(index);
        } else {
            retval = -ENOMEM;
        }
    }

    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_input_set(struct gb_operation *operation)
{
    int retval;
    __le32 *input;

    if (gb_operation_get_request_payload_size(operation) == sizeof(__le32)) {
        input = (__le32*)gb_operation_get_request_payload(operation);
        retval = CALL_CAM_DEV_OP(dev_info.dev,
                        input_set, le32_to_cpu(*input));
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_fmt_enum(struct gb_operation *operation)
{
    int retval;
    __le32* index;
    struct camera_ext_fmtdesc *response;

    if (gb_operation_get_request_payload_size(operation) == sizeof(__le32)) {
        index = (__le32*)gb_operation_get_request_payload(operation);
        response = gb_operation_alloc_response(operation,
            sizeof(struct camera_ext_fmtdesc));
        if (response != NULL) {
            response->index = *index;
            retval = CALL_CAM_DEV_OP(dev_info.dev, format_enum, response);
        } else {
            retval = -ENOMEM;
        }
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }

    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_fmt_get(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_format *response;

    response = gb_operation_alloc_response(operation,
        sizeof(struct camera_ext_format));
    if (response != NULL) {
        retval = CALL_CAM_DEV_OP(dev_info.dev, format_get, response);
    } else {
        retval = -ENOMEM;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_fmt_set(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_format *request;

    if (gb_operation_get_request_payload_size(operation) == sizeof(*request)) {
        request = (struct camera_ext_format*)
            gb_operation_get_request_payload(operation);
        retval = CALL_CAM_DEV_OP(dev_info.dev, format_set, request);
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_frmsize_enum(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_frmsize *response;
    struct camera_ext_frmsize *request;

    if (gb_operation_get_request_payload_size(operation) == sizeof(*request)) {
        request = (struct camera_ext_frmsize*)
            gb_operation_get_request_payload(operation);

        response = gb_operation_alloc_response(operation,
            sizeof(struct camera_ext_frmsize));
        if (response != NULL) {
            response->index = request->index;
            response->pixelformat = request->pixelformat;

            retval = CALL_CAM_DEV_OP(dev_info.dev, frmsize_enum, response);
        } else {
            retval = -ENOMEM;
        }
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_frmival_enum(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_frmival *response;
    struct camera_ext_frmival *request;

    if (gb_operation_get_request_payload_size(operation) == sizeof(*request)) {
        response = gb_operation_alloc_response(operation,
            sizeof(struct camera_ext_frmival));

        if (response != NULL) {
            request = (struct camera_ext_frmival *)
                gb_operation_get_request_payload(operation);

            response->index = request->index;
            response->pixelformat = request->pixelformat;
            response->width = request->width;
            response->height = request->height;

            retval = CALL_CAM_DEV_OP(dev_info.dev, frmival_enum, response);
        } else {
            retval = -ENOMEM;
        }
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_stream_set_parm(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_streamparm *parm;

    if (gb_operation_get_request_payload_size(operation) == sizeof(*parm)) {
        parm = (struct camera_ext_streamparm*)
            gb_operation_get_request_payload(operation);
        retval = CALL_CAM_DEV_OP(dev_info.dev, stream_set_parm, parm);
    } else {
        CAM_ERR("invalid request size\n");
        retval = -EINVAL;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static uint8_t gb_camera_ext_stream_get_parm(struct gb_operation *operation)
{
    int retval;
    struct camera_ext_streamparm *response;

    response = gb_operation_alloc_response(operation, sizeof(*response));
    if (response != NULL) {
        retval = CALL_CAM_DEV_OP(dev_info.dev, stream_get_parm, response);
    } else {
        retval = -ENOMEM;
    }
    CAM_DBG("result %d\n", retval);
    return gb_operation_errno_map(retval);
}

static struct gb_operation_handler gb_camera_handlers[] = {
    GB_HANDLER(GB_CAMERA_TYPE_PROTOCOL_VERSION, gb_camera_protocol_version),
    GB_HANDLER(GB_CAMERA_TYPE_POWER_ON, gb_camera_power_on),
    GB_HANDLER(GB_CAMERA_TYPE_POWER_OFF, gb_camera_power_off),

    GB_HANDLER(GB_CAMERA_EXT_TYPE_INPUT_ENUM, gb_camera_ext_input_enum),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_INPUT_GET, gb_camera_ext_input_get),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_INPUT_SET, gb_camera_ext_input_set),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_FMT_ENUM, gb_camera_ext_fmt_enum),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_FMT_GET, gb_camera_ext_fmt_get),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_FMT_SET, gb_camera_ext_fmt_set),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_FMSIZE_ENUM, gb_camera_ext_frmsize_enum),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_FRMIVAL_ENUM, gb_camera_ext_frmival_enum),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_STREAM_ON, gb_camera_ext_stream_on),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_STREAM_OFF, gb_camera_ext_stream_off),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_STREAM_PARM_SET,
        gb_camera_ext_stream_set_parm),
    GB_HANDLER(GB_CAMERA_EXT_TYPE_STREAM_PARM_GET,
        gb_camera_ext_stream_get_parm),
};

static struct gb_driver gb_camera_driver = {
    .op_handlers = gb_camera_handlers,
    .op_handlers_count = ARRAY_SIZE(gb_camera_handlers),
};

void gb_camera_ext_register(int cport)
{
    CAM_DBG("register camera greybus driver at %d\n", cport);
    gb_register_driver(cport, &gb_camera_driver);
}