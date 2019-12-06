/* 

 * Copyright (c) 2012-2017 The Khronos Group Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*!
 * \file
 * \brief The Bitwise Kernels: And, Or, Xor, Not.
 * \author Hans-Peter Nilsson <hp@axis.com>
 */

#include <VX/vx.h>
#include <VX/vx_helper.h>
#include <vx_support.h>

#include "vx_interface.h"

/*
 * The three bitwise kernels with binary parameters have the same parameter domain so
 * let's just have one set of validators.
 */

static vx_status VX_CALLBACK vxBinaryBitwiseInputValidator(vx_node node, vx_uint32 index)
{
    vx_status status = VX_ERROR_INVALID_PARAMETERS;
    if (index == 0)
    {
        vx_image input = 0;
        vx_parameter param = vxGetParameterByIndex(node, index);

        vxQueryParameter(param, VX_PARAMETER_REF, &input, sizeof(input));
        if (input)
        {
            vx_df_image format = 0;
            vxQueryImage(input, VX_IMAGE_FORMAT, &format, sizeof(format));
            if (format == VX_DF_IMAGE_U1 || format == VX_DF_IMAGE_U8)
                status = VX_SUCCESS;
            vxReleaseImage(&input);
        }
        vxReleaseParameter(&param);
    }
    else if (index == 1)
    {
        vx_image images[2];
        vx_parameter param[2] = {
            vxGetParameterByIndex(node, 0),
            vxGetParameterByIndex(node, 1),
        };
        vxQueryParameter(param[0], VX_PARAMETER_REF, &images[0], sizeof(images[0]));
        vxQueryParameter(param[1], VX_PARAMETER_REF, &images[1], sizeof(images[1]));
        if (images[0] && images[1])
        {
            vx_uint32 width[2], height[2];
            vx_df_image format[2];

            vxQueryImage(images[0], VX_IMAGE_WIDTH, &width[0], sizeof(width[0]));
            vxQueryImage(images[1], VX_IMAGE_WIDTH, &width[1], sizeof(width[1]));
            vxQueryImage(images[0], VX_IMAGE_HEIGHT, &height[0], sizeof(height[0]));
            vxQueryImage(images[1], VX_IMAGE_HEIGHT, &height[1], sizeof(height[1]));
            vxQueryImage(images[0], VX_IMAGE_FORMAT, &format[0], sizeof(format[0]));
            vxQueryImage(images[1], VX_IMAGE_FORMAT, &format[1], sizeof(format[1]));
            if (width[0] == width[1] && height[0] == height[1] && format[0] == format[1])
                status = VX_SUCCESS;
            vxReleaseImage(&images[1]);
            vxReleaseImage(&images[0]);
        }
        vxReleaseParameter(&param[0]);
        vxReleaseParameter(&param[1]);
    }
    return status;
}

static vx_status VX_CALLBACK vxBinaryBitwiseOutputValidator(vx_node node, vx_uint32 index, vx_meta_format_t *ptr)
{
    vx_status status = VX_ERROR_INVALID_PARAMETERS;
    if (index == 2)
    {
        vx_parameter param0 = vxGetParameterByIndex(node, 0);
        if (param0)
        {
            vx_image image0 = 0;
            vxQueryParameter(param0, VX_PARAMETER_REF, &image0, sizeof(image0));
            /*
             * When passing on the geometry to the output image, we only look at image 0, as
             * both input images are verified to match, at input validation.
             */
            if (image0)
            {
                vx_df_image format = 0;
                vx_uint32 width = 0, height = 0;
                vxQueryImage(image0, VX_IMAGE_FORMAT, &format, sizeof(format));
                vxQueryImage(image0, VX_IMAGE_WIDTH, &width, sizeof(width));
                vxQueryImage(image0, VX_IMAGE_HEIGHT, &height, sizeof(height));
                ptr->type = VX_TYPE_IMAGE;
                ptr->dim.image.format = format;
                ptr->dim.image.width = width;
                ptr->dim.image.height = height;
                status = VX_SUCCESS;
                vxReleaseImage(&image0);
            }
            vxReleaseParameter(&param0);
        }
    }
    return status;
}
static vx_param_description_t binary_bitwise_kernel_params[] = {
    {VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED},
    {VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED},
    {VX_OUTPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED},
};


static vx_status VX_CALLBACK vxclCallOpenCLKernel(vx_node node, const vx_reference parameters[], vx_uint32 num)
{
    vx_status status = VX_FAILURE;
    vx_context context = node->base.context;

    vx_cl_kernel_description_t *vxclk = vxclFindKernel(node->kernel->enumeration);
    vx_uint32 pidx, pln, didx, plidx, argidx;
    cl_int err = 0;
    size_t off_dim[3] = {0,0,0};
    size_t work_dim[3];

    cl_event writeEvents[VX_INT_MAX_PARAMS];
    cl_event readEvents[VX_INT_MAX_PARAMS];
    cl_int we = 0, re = 0;

    // determine which platform to use
    plidx = 0;

    // determine which device to use
    didx = 0;

    cl_kernel kernel = vxclk->kernels[plidx];

    pln = 0;

    for (argidx = 0, pidx = 0; pidx < num; pidx++)
    {
        vx_reference ref = node->parameters[pidx];
        vx_enum dir = node->kernel->signature.directions[pidx];
        vx_memory_t *memory = &((vx_image)ref)->memory;

        /* set the work dimensions */
        work_dim[0] = memory->dims[pln][VX_DIM_X];
        work_dim[1] = memory->dims[pln][VX_DIM_Y];

        //stride_x, stride_y
        err = clSetKernelArg(kernel, argidx++, sizeof(vx_int32), &memory->strides[pln][VX_DIM_X]);
        err = clSetKernelArg(kernel, argidx++, sizeof(vx_int32), &memory->strides[pln][VX_DIM_Y]);
        VX_PRINT(VX_ZONE_INFO, "Setting vx_image as Buffer with 2 parameters\n");

        err = clSetKernelArg(kernel, argidx++, sizeof(cl_mem), &memory->hdls[pln]);
        CL_ERROR_MSG(err, "clSetKernelArg");
        if (dir == VX_INPUT || dir == VX_BIDIRECTIONAL)
        {
            err = clEnqueueWriteBuffer(context->queues[plidx][didx],
                                       memory->hdls[pln],
                                       CL_TRUE,
                                       0,
                                       ownComputeMemorySize(memory, pln),
                                       memory->ptrs[pln],
                                       0,
                                       NULL,
                                       &ref->event);
        }
    }

    we = 0;
    for (pidx = 0; pidx < num; pidx++)
    {
        vx_reference ref = node->parameters[pidx];
        vx_enum dir = node->kernel->signature.directions[pidx];
        if (dir == VX_INPUT || dir == VX_BIDIRECTIONAL) 
        {
            memcpy(&writeEvents[we++],&ref->event, sizeof(cl_event));
        }
    }


    err = clEnqueueNDRangeKernel(context->queues[plidx][didx],
                                 kernel,
                                 2,
                                 off_dim,
                                 work_dim,
                                 NULL,
                                 we, writeEvents, &node->base.event);

    clFinish(context->queues[plidx][didx]);

    CL_ERROR_MSG(err, "clEnqueueNDRangeKernel");

    pln = 0;

    vx_reference ref;
    /* enqueue a read on all output data */
    if (num == 3)
        ref = node->parameters[2];
    else  // Not kernel
        ref = node->parameters[1];

    vx_memory_t *memory = NULL;

    memory = &((vx_image)ref)->memory;

    err = clEnqueueReadBuffer(context->queues[plidx][didx],
        memory->hdls[pln],
        CL_TRUE, 0, ownComputeMemorySize(memory, pln),
        memory->ptrs[pln],
        0, NULL, NULL);

    CL_ERROR_MSG(err, "clEnqueueReadBuffer");

    clFinish(context->queues[plidx][didx]);

    re = 0;
    for (pidx = 0; pidx < num; pidx++) 
    {
        vx_reference ref = node->parameters[pidx];
        vx_enum dir = node->kernel->signature.directions[pidx];
        if (dir == VX_OUTPUT || dir == VX_BIDIRECTIONAL) 
        {
            memcpy(&readEvents[re++],&ref->event, sizeof(cl_event));
        }
    }
    err = clFlush(context->queues[plidx][didx]);
    CL_ERROR_MSG(err, "Flush");
    VX_PRINT(VX_ZONE_TARGET, "Waiting for read events!\n");
    clWaitForEvents(re, readEvents);
    if (err == CL_SUCCESS)
        status = VX_SUCCESS;

    VX_PRINT(VX_ZONE_API, "%s exiting %d\n", __FUNCTION__, status);
    return status;
}

typedef vx_uint8 (bitwiseOp)(vx_uint8, vx_uint8);

static vx_uint8 vx_and_op(vx_uint8 a, vx_uint8 b)
{
    return a & b;
}

static vx_uint8 vx_or_op(vx_uint8 a, vx_uint8 b)
{
    return a | b;
}

static vx_uint8 vx_xor_op(vx_uint8 a, vx_uint8 b)
{
    return a ^ b;
}

static vx_status vxBinaryU1Op(vx_image in1, vx_image in2, vx_image output, bitwiseOp op)
{
    vx_uint32 y, x, width, height;
    void *dst_base   = NULL;
    void *src_base[2] = {NULL, NULL};
    vx_imagepatch_addressing_t dst_addr, src_addr[2];
    vx_rectangle_t rect;
    vx_status status = VX_SUCCESS;

    status  = vxGetValidRegionImage(in1, &rect);
    status |= vxAccessImagePatch(in1, &rect, 0, &src_addr[0], (void **)&src_base[0], VX_READ_ONLY);
    status |= vxAccessImagePatch(in2, &rect, 0, &src_addr[1], (void **)&src_base[1], VX_READ_ONLY);
    status |= vxAccessImagePatch(output, &rect, 0, &dst_addr, (void **)&dst_base, VX_WRITE_ONLY);
    width  = src_addr[0].dim_x - rect.start_x % 8;  // vxAccessImagePatch rounds start_x down to the nearest byte boundary
    height = src_addr[0].dim_y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            vx_uint32 xShftd = x + rect.start_x % 8;     // U1 valid region start bit offset
            vx_uint8 *src[2] = {
                vxFormatImagePatchAddress2d(src_base[0], xShftd, y, &src_addr[0]),
                vxFormatImagePatchAddress2d(src_base[1], xShftd, y, &src_addr[1]),
            };
            vx_uint8 *dst = vxFormatImagePatchAddress2d(dst_base, xShftd, y, &dst_addr);

            vx_uint8 mask  = 1 << (xShftd % 8);
            vx_uint8 pixel = op(*src[0], *src[1]) & mask;
            *dst = (*dst & ~mask) | pixel;
        }
    }
    status |= vxCommitImagePatch(in1, NULL, 0, &src_addr[0], src_base[0]);
    status |= vxCommitImagePatch(in2, NULL, 0, &src_addr[1], src_base[1]);
    status |= vxCommitImagePatch(output, &rect, 0, &dst_addr, dst_base);

    return status;
}

static vx_status VX_CALLBACK vxAndKernel(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    vx_image in1 = (vx_image)parameters[0];
    vx_image in2 = (vx_image)parameters[1];
    vx_image output = (vx_image)parameters[2];

    vx_df_image format;
    vx_status status = vxQueryImage(in1, VX_IMAGE_FORMAT, &format, sizeof(format));
    if (status != VX_SUCCESS)
        return status;
    else if (format == VX_DF_IMAGE_U1)
        return vxBinaryU1Op(in1, in2, output, vx_and_op);
    else
        return vxclCallOpenCLKernel(node, parameters, num);
}

vx_cl_kernel_description_t and_kernel = {
    {
        VX_KERNEL_AND,
        "org.khronos.openvx.and",
        vxAndKernel,
        binary_bitwise_kernel_params, dimof(binary_bitwise_kernel_params),
        NULL,
        vxBinaryBitwiseInputValidator,
        vxBinaryBitwiseOutputValidator,
        NULL,
        NULL,
    },
    VX_CL_SOURCE_DIR""FILE_JOINER"vx_and.cl",
    "vx_and",
    INIT_PROGRAMS,
    INIT_KERNELS,
    INIT_NUMKERNELS,
    INIT_RETURNS,
    NULL,
};

static vx_status VX_CALLBACK vxOrKernel(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    vx_image in1 = (vx_image)parameters[0];
    vx_image in2 = (vx_image)parameters[1];
    vx_image output = (vx_image)parameters[2];

    vx_df_image format;
    vx_status status = vxQueryImage(in1, VX_IMAGE_FORMAT, &format, sizeof(format));
    if (status != VX_SUCCESS)
        return status;
    else if (format == VX_DF_IMAGE_U1)
        return vxBinaryU1Op(in1, in2, output, vx_or_op);
    else
        return vxclCallOpenCLKernel(node, parameters, num);
}

vx_cl_kernel_description_t orr_kernel = {
    {
        VX_KERNEL_OR,
        "org.khronos.openvx.or",
        vxOrKernel,
        binary_bitwise_kernel_params, dimof(binary_bitwise_kernel_params),
        NULL,
        vxBinaryBitwiseInputValidator,
        vxBinaryBitwiseOutputValidator,
        NULL,
        NULL,
    },
    VX_CL_SOURCE_DIR""FILE_JOINER"vx_orr.cl",
    "vx_orr",
    INIT_PROGRAMS,
    INIT_KERNELS,
    INIT_NUMKERNELS,
    INIT_RETURNS,
    NULL,
};

static vx_status VX_CALLBACK vxXorKernel(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    vx_image in1 = (vx_image)parameters[0];
    vx_image in2 = (vx_image)parameters[1];
    vx_image output = (vx_image)parameters[2];

    vx_df_image format;
    vx_status status = vxQueryImage(in1, VX_IMAGE_FORMAT, &format, sizeof(format));
    if (status != VX_SUCCESS)
        return status;
    else if (format == VX_DF_IMAGE_U1)
        return vxBinaryU1Op(in1, in2, output, vx_xor_op);
    else
        return vxclCallOpenCLKernel(node, parameters, num);
}

vx_cl_kernel_description_t xor_kernel = {
    {
        VX_KERNEL_XOR,
        "org.khronos.openvx.xor",
        vxXorKernel,
        binary_bitwise_kernel_params, dimof(binary_bitwise_kernel_params),
        NULL,
        vxBinaryBitwiseInputValidator,
        vxBinaryBitwiseOutputValidator,
        NULL,
        NULL,
    },
    VX_CL_SOURCE_DIR""FILE_JOINER"vx_xor.cl",
    "vx_xor",
    INIT_PROGRAMS,
    INIT_KERNELS,
    INIT_NUMKERNELS,
    INIT_RETURNS,
    NULL,
};

/* The Not kernel is an unary operator, requiring separate validators. */

static vx_status VX_CALLBACK vxUnaryBitwiseInputValidator(vx_node node, vx_uint32 index)
{
    vx_status status = VX_ERROR_INVALID_PARAMETERS;
    if (index == 0)
    {
        vx_image input = 0;
        vx_parameter param = vxGetParameterByIndex(node, index);

        vxQueryParameter(param, VX_PARAMETER_REF, &input, sizeof(input));
        if (input)
        {
            vx_df_image format = 0;
            vxQueryImage(input, VX_IMAGE_FORMAT, &format, sizeof(format));
            if (format == VX_DF_IMAGE_U1 || format == VX_DF_IMAGE_U8)
                status = VX_SUCCESS;
            vxReleaseImage(&input);
        }
        vxReleaseParameter(&param);
    }
    return status;
}

static vx_status VX_CALLBACK vxUnaryBitwiseOutputValidator(vx_node node, vx_uint32 index, vx_meta_format_t *ptr)
{
    vx_status status = VX_ERROR_INVALID_PARAMETERS;
    if (index == 1)
    {
        vx_parameter param = vxGetParameterByIndex(node, 0);
        if (param)
        {
            vx_image inimage = 0;
            vxQueryParameter(param, VX_PARAMETER_REF, &inimage, sizeof(inimage));
            if (inimage)
            {
                vx_df_image format = 0;
                vx_uint32 width = 0, height = 0;
                vxQueryImage(inimage, VX_IMAGE_FORMAT, &format, sizeof(format));
                vxQueryImage(inimage, VX_IMAGE_WIDTH, &width, sizeof(width));
                vxQueryImage(inimage, VX_IMAGE_HEIGHT, &height, sizeof(height));
                ptr->type = VX_TYPE_IMAGE;
                ptr->dim.image.format = format;
                ptr->dim.image.width = width;
                ptr->dim.image.height = height;
                status = VX_SUCCESS;
                vxReleaseImage(&inimage);
            }
            vxReleaseParameter(&param);
        }
    }
    return status;
}


static vx_param_description_t unary_bitwise_kernel_params[] = {
    {VX_INPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED},
    {VX_OUTPUT, VX_TYPE_IMAGE, VX_PARAMETER_STATE_REQUIRED},
};

vx_status vxNotU1(vx_image input, vx_image output)
{
    vx_uint32 y, x, width = 0, height = 0;
    vx_df_image format = 0;
    void *dst_base = NULL;
    void *src_base = NULL;
    vx_imagepatch_addressing_t dst_addr, src_addr;
    vx_rectangle_t rect;
    vx_status status = VX_SUCCESS;

    status  = vxGetValidRegionImage(input, &rect);
    status |= vxQueryImage(input, VX_IMAGE_FORMAT, &format, sizeof(format));
    status |= vxAccessImagePatch(input, &rect, 0, &src_addr, (void **)&src_base, VX_READ_ONLY);
    status |= vxAccessImagePatch(output, &rect, 0, &dst_addr, (void **)&dst_base, VX_WRITE_ONLY);
    width  = (format == VX_DF_IMAGE_U1) ? src_addr.dim_x - rect.start_x % 8 : src_addr.dim_x;
    height = src_addr.dim_y;

    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            vx_uint32 xShftd = (format == VX_DF_IMAGE_U1) ? x + rect.start_x % 8 : x;    // Shift for U1 valid region
            vx_uint8 *src = (vx_uint8*)vxFormatImagePatchAddress2d(src_base, xShftd, y, &src_addr);
            vx_uint8 *dst = (vx_uint8*)vxFormatImagePatchAddress2d(dst_base, xShftd, y, &dst_addr);

            vx_uint8 mask  = 1 << (xShftd % 8);
            vx_uint8 pixel = ~*src & mask;
            *dst = (*dst & ~mask) | pixel;
        }
    }
    status |= vxCommitImagePatch(input, NULL, 0, &src_addr, src_base);
    status |= vxCommitImagePatch(output, &rect, 0, &dst_addr, dst_base);

    return status;
}

static vx_status VX_CALLBACK vxNotKernel(vx_node node, const vx_reference *parameters, vx_uint32 num)
{
    vx_image input = (vx_image)parameters[0];
    vx_image output = (vx_image)parameters[1];
    vx_df_image format;
    vx_status status = vxQueryImage(input, VX_IMAGE_FORMAT, &format, sizeof(format));
    if (status != VX_SUCCESS)
        return status;
    else if (format == VX_DF_IMAGE_U1)
        return vxNotU1(input, output);
    else
        return vxclCallOpenCLKernel(node, parameters, num);
}

vx_cl_kernel_description_t not_kernel = {
    {
        VX_KERNEL_NOT,
        "org.khronos.openvx.not",
        vxNotKernel,
        unary_bitwise_kernel_params, dimof(unary_bitwise_kernel_params),
        NULL,
        vxUnaryBitwiseInputValidator,
        vxUnaryBitwiseOutputValidator,
        NULL,
        NULL,
    },
    VX_CL_SOURCE_DIR""FILE_JOINER"vx_not.cl",
    "vx_not",
    INIT_PROGRAMS,
    INIT_KERNELS,
    INIT_NUMKERNELS,
    INIT_RETURNS,
    NULL,
};
