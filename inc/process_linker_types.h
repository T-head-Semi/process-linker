/*
 * Copyright (c) 2021-2022 Alibaba Group. All rights reserved.
 * License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _PROCESS_LINKER_TYPES_H_
#define _PROCESS_LINKER_TYPES_H_

#include "process_linker.h"

/* When set PlinkMsg.msg to this exit code, it means to close the connection */
#define PLINK_EXIT_CODE -1

/* image/video color format */
typedef enum _PlinkColorFormat
{
    PLINK_COLOR_FormatUnused,
    PLINK_COLOR_FormatMonochrome,
    PLINK_COLOR_FormatYUV420Planar,
    PLINK_COLOR_FormatYUV420SemiPlanar,
    PLINK_COLOR_FormatYUV420SemiPlanarP010,
    PLINK_COLOR_FormatYUV422Planar,
    PLINK_COLOR_FormatYUV422SemiPlanar,
    PLINK_COLOR_Format32bitBGRA8888,
    PLINK_COLOR_Format32bitARGB8888,
    PLINK_COLOR_Format24BitRGB888,
    PLINK_COLOR_Format24BitRGB888Planar,
    PLINK_COLOR_Format24BitBGR888,
    PLINK_COLOR_Format24BitBGR888Planar,
    PLINK_COLOR_FormatRawBayer8bit,
    PLINK_COLOR_FormatRawBayer10bit,
    PLINK_COLOR_FormatRawBayer12bit,
    PLINK_COLOR_FormatMax
} PlinkColorFormat;

typedef enum _PlinkBayerPattern
{
    PLINK_BAYER_PATTERN_RGGB,
    PLINK_BAYER_PATTERN_BGGR,
    PLINK_BAYER_PATTERN_GRBG,
    PLINK_BAYER_PATTERN_GBRG,
    PLINK_BAYER_PATTERN_MAX
} PlinkBayerPattern;

/* Data descriptor type */
typedef enum _PlinkDescType
{
    PLINK_TYPE_1D_BUFFER = 0,   /* PlinkBufferInfo */
    PLINK_TYPE_2D_YUV,          /* PlinkYuvInfo */
    PLINK_TYPE_2D_RGB,          /* PlinkRGBInfo */
    PLINK_TYPE_OBJECT,          /* PlinkObjectInfo */
    PLINK_TYPE_MESSAGE,         /* PlinkMsg */
    PLINK_TYPE_TIME,            /* PlinkTimeInfo */
    PLINK_TYPE_2D_RAW,          /* PlinkRawInfo */
    PLINK_TYPE_MAX
} PlinkDescType;

/* time type */
typedef enum _PlinkTimeType
{
    PLINK_TIME_START = 0,       /* start time */
    PLINK_TIME_CALIBRATION,     /* time delta for calibration */
    PLINK_TIME_MAX
} PlinkTimeType;

/* 1D buffer */
typedef struct _PlinkBufferInfo
{
    PlinkDescHdr header;
    unsigned long long bus_address;
    unsigned int offset;
    unsigned int size;
} PlinkBufferInfo;

/* 2D YUV surface */
typedef struct _PlinkYuvInfo
{
    PlinkDescHdr header;
    PlinkColorFormat format;
    unsigned long long bus_address_y;
    unsigned long long bus_address_u;
    unsigned long long bus_address_v;
    unsigned int offset_y;
    unsigned int offset_u;
    unsigned int offset_v;
    unsigned int pic_width;
    unsigned int pic_height;
    unsigned int stride_y;
    unsigned int stride_u;
    unsigned int stride_v;
} PlinkYuvInfo;

/* 2D RGB surface */
typedef struct _PlinkRGBInfo
{
    PlinkDescHdr header;
    PlinkColorFormat format;
    unsigned long long bus_address_r;
    unsigned long long bus_address_g;
    unsigned long long bus_address_b;
    unsigned long long bus_address_a;
    unsigned int offset_r;
    unsigned int offset_g;
    unsigned int offset_b;
    unsigned int offset_a;
    unsigned int img_width;
    unsigned int img_height;
    unsigned int stride_r;
    unsigned int stride_g;
    unsigned int stride_b;
    unsigned int stride_a;
} PlinkRGBInfo;

/* 2D Bayer Raw surface */
typedef struct _PlinkRawInfo
{
    PlinkDescHdr header;
    PlinkColorFormat format;
    PlinkBayerPattern pattern;
    unsigned long long bus_address;
    unsigned int offset;
    unsigned int img_width;
    unsigned int img_height;
    unsigned int stride;
} PlinkRawInfo;

/* Feature map buffer after NPU inference */
typedef struct _PlinkBox
{
    float x1;
    float y1;
    float x2;
    float y2;
} PlinkBox;

typedef struct _PlinkLandmark
{
    float x[5];
    float y[5];
} PlinkLandmark;

typedef struct _PlinkObjectDetect
{
    float score;
    PlinkBox box;
    PlinkLandmark landmark;
} PlinkObjectDetect;

typedef struct _PlinkObjectInfo
{
    PlinkDescHdr header;
    unsigned long long bus_address;
    unsigned int object_cnt;
} PlinkObjectInfo;

/* Used to send message */
typedef struct _PlinkMsg
{
    PlinkDescHdr header;
    int msg;                /* When greater than 0, it means the id of buffer which can be released */
                            /* When set to 0, it means a buffer can be released, but id is unknown */
                            /* When set to PLINK_EXIT_CODE, it means to close connection */
                            /* Other values are reserved */
} PlinkMsg;

/* time information */
typedef struct _PlinkTimeInfo
{
    PlinkDescHdr header;
    PlinkTimeType type;
    long long seconds;
    long long useconds;
} PlinkTimeInfo;

#endif /* !_PROCESS_LINKER_TYPES_H_ */
