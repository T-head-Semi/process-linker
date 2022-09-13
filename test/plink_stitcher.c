/*
 * Copyright (c) 2022 Alibaba Group. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <memory.h>
#include "process_linker_types.h"
#include "video_mem.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

#define MAX_NUM_OF_INPUTS   4
#define NUM_OF_BUFFERS      5
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)
#define STITCHER_MIN(a, b) ((a) < (b) ? (a) : (b))


typedef enum _StitchLayout
{
    STITCH_LAYOUT_Vertical = 0,
    STITCH_LAYOUT_Horizontal,
    STITCH_LAYOUT_Matrix,
    STITCH_LAYOUT_Max
} StitchLayout;

typedef struct _StitherRegion
{
    int width;
    int height;
    int offset_uv;
    int offset_y;
} StitcherRegion;

typedef struct _StitcherParams
{
    char *in_name[MAX_NUM_OF_INPUTS];
    char *out_name;
    StitchLayout layout;
    PlinkColorFormat format;
    int width;
    int height;
    int stride;
} StitcherParams;

typedef struct _StitcherPort
{
    char *name;
    int index;
    PlinkChannelID id;
    PlinkHandle plink;
    int sendid;
    int backid;
    int *exit;
    int available_bufs;
    void *vmem;
    pthread_mutex_t pic_mutex;
    pthread_mutex_t *count_mutex;
    sem_t *sem_ready;
    sem_t *sem_done;
    StitchLayout layout;
    PlinkColorFormat format;
    void *buffer;
    int width;
    int height;
    int stride;
    int offset;
    int offset_uv;
    int *in_count;
} StitcherPort;

typedef struct _StitcherContext
{
    StitcherPort in[MAX_NUM_OF_INPUTS];
    StitcherPort out;
    pthread_mutex_t count_mutex;
    sem_t sem_ready;
    sem_t sem_done;
    int in_count;
    int exitcode;
} StitcherContext;

typedef struct _PictureBuffer
{
  unsigned int bus_address;
  void *virtual_address;
  unsigned int size;
  int fd;
} PictureBuffer;

static void printUsage(char *name)
{
    printf("usage: %s [options]\n"
           "\n"
           "  Stitch multiple pictures to one. Maximum # of pictures to be stitched is %d\n"
           "  Available options:\n"
           "    -i<n>   plink file name of input port #n (default: /tmp/plink.stitch.in<n>). n is 0 based.\n"
           "    -o      plink file name of output port (default: /tmp/plink.stitch.out)\n"
           "    -l      layout (default: 0)\n"
           "                0 - vertical\n"
           "                1 - horizontal\n"
           "                2 - matrix\n"
           "    -f      output color format (default: 3)\n"
           "                3 - NV12\n"
           "    -w      output video width (default: 800)\n"
           "    -h      output video height (default: 1280)\n"
           "    -s      output video buffer stride (default: video width)\n"
           "    --help  print this message\n"
           "\n", name, MAX_NUM_OF_INPUTS);
}

static void parseParams(int argc, char **argv, StitcherParams *params)
{
    int i = 1;
    memset(params, 0, sizeof(*params));
    params->in_name[0] = "/tmp/plink.stitch.in0";
    params->in_name[1] = "/tmp/plink.stitch.in1";
    params->in_name[2] = "/tmp/plink.stitch.in2";
    params->in_name[3] = "/tmp/plink.stitch.in3";
    params->out_name = "/tmp/plink.stitch.out";
    params->layout = STITCH_LAYOUT_Vertical;
    params->format = PLINK_COLOR_FormatYUV420SemiPlanar;
    params->width = 800;
    params->height = 1280;
    while (i < argc)
    {
        if (argv[i][0] != '-' || strlen(argv[i]) < 2)
        {
            i++;
            continue;
        }
        
        if (argv[i][1] == 'i')
        {
            if (++i < argc)
            {
                if (strlen(argv[i-1]) > 2) // input name
                {
                    int id = atoi(argv[i-1]+2);
                    if (id < MAX_NUM_OF_INPUTS)
                        params->in_name[id] = argv[i++];
                }
                else
                    params->in_name[0] = argv[i++];
            }
        }
        else if (argv[i][1] == 'o')
        {
            if (++i < argc)
                params->out_name = argv[i++];
        }
        else if (argv[i][1] == 'l')
        {
            if (++i < argc)
                params->layout = atoi(argv[i++]);
        }
        else if (argv[i][1] == 'f')
        {
            if (++i < argc)
                params->format = atoi(argv[i++]);
        }
        else if (argv[i][1] == 'w')
        {
            if (++i < argc)
                params->width = atoi(argv[i++]);
        }
        else if (argv[i][1] == 'h')
        {
            if (++i < argc)
                params->height = atoi(argv[i++]);
        }
        else if (argv[i][1] == 's')
        {
            if (++i < argc)
                params->stride = atoi(argv[i++]);
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            params->layout = STITCH_LAYOUT_Max;
            i++;
        }
    }

    if (params->stride == 0)
        params->stride = params->width;

    printf("[STITCHER] Input 0 Name         : %s\n", params->in_name[0]);
    printf("[STITCHER] Input 1 Name         : %s\n", params->in_name[1]);
    printf("[STITCHER] Input 2 Name         : %s\n", params->in_name[2]);
    printf("[STITCHER] Input 3 Name         : %s\n", params->in_name[3]);
    printf("[STITCHER] Output Name          : %s\n", params->out_name);
    printf("[STITCHER] Output Layout        : %d\n", params->layout);
    printf("[STITCHER] Output Format        : %d\n", params->format);
    printf("[STITCHER] Output Resolution    : %dx%d\n", params->width, params->height);
    printf("[STITCHER] Output Stride        : %d\n", params->stride);
}

static int checkParams(StitcherParams *params)
{
    if (params->format != PLINK_COLOR_FormatYUV420SemiPlanar ||
        params->layout >= STITCH_LAYOUT_Max)
        return -1;
    return 0;
}

static int getBufferSize(StitcherParams *params)
{
    int size = 0;
    switch (params->format)
    {
        case PLINK_COLOR_FormatYUV420Planar:
        case PLINK_COLOR_FormatYUV420SemiPlanar:
            size = params->stride * params->height * 3 / 2;
            break;
        default:
            size = 0;
    }
    return size;
}

static void AllocateBuffers(PictureBuffer picbuffers[NUM_OF_BUFFERS], unsigned int size, void *vmem)
{
    unsigned int buffer_size = (size + 0xFFF) & ~0xFFF;
    VmemParams params;
    params.size = buffer_size;
    params.flags = VMEM_FLAG_CONTIGUOUS | VMEM_FLAG_4GB_ADDR;
    for (int i = 0; i < NUM_OF_BUFFERS; i++)
    {
        params.fd = 0;
        VMEM_allocate(vmem, &params);
        VMEM_mmap(vmem, &params);
        VMEM_export(vmem, &params);
        printf("[STITCHER] mmap %p from %x with size %d, dma-buf fd %d\n", 
                params.vir_address, params.phy_address, params.size, params.fd);
        picbuffers[i].virtual_address = params.vir_address;
        picbuffers[i].bus_address = params.phy_address;
        picbuffers[i].size = buffer_size;
        picbuffers[i].fd = params.fd;
    }
}

static void FreeBuffers(PictureBuffer picbuffers[NUM_OF_BUFFERS], void *vmem)
{
    VmemParams params;
    memset(&params, 0, sizeof(params));
    for (int i = 0; i < NUM_OF_BUFFERS; i++)
    {
        close(picbuffers[i].fd);
        params.size = picbuffers[i].size;
        params.vir_address = picbuffers[i].virtual_address;
        params.phy_address = picbuffers[i].bus_address;
        VMEM_free(vmem, &params);
    }
}

static void getRegion(StitcherPort *out, int index, int in_count, StitcherRegion *region)
{
    region->offset_uv = 0; //out->stride * out->height;
    if (out->layout == STITCH_LAYOUT_Horizontal)
    {
        int width = (out->width / in_count + 1) & ~1;
        region->offset_y = width * index;
        region->offset_uv += width * index;
        region->width = width;
        region->height = out->height;
    }
    else if (out->layout == STITCH_LAYOUT_Matrix && in_count >= 3)
    {
        int y = index >> 1;
        int x = index & 1;
        int width = (out->width / 2 + 1) & ~1;
        int height = (out->height / 2 + 1) & ~1;
        region->offset_y = out->stride * (height * y);
        region->offset_y += width * x;
        region->offset_uv += out->stride * (height * y / 2);
        region->offset_uv += width * x;
        region->width = width;
        region->height = height;
    }
    else // (out->layout == STITCH_LAYOUT_Vertical)
    {
        int height = (out->height / in_count + 1) & ~1;
        region->offset_y = out->stride * (height * index);
        region->offset_uv += out->stride * (height * index / 2);
        region->width = out->width;
        region->height = height;
    }
}

static int stitchOneFrame(StitcherContext *ctx)
{
    StitcherPort *in = NULL;
    StitcherPort *out = &ctx->out;
    int in_count = 0;
    do
    {
        pthread_mutex_lock(out->count_mutex);
        in_count = ctx->in_count;
        pthread_mutex_unlock(out->count_mutex);
        if (in_count > 0)
            break;
        else
            sleep(1);
    } while (1);

    for (int i = 0; i < in_count; i++)
    {
        in = &ctx->in[i];
        if (in->index == 0)
        {
            sem_wait(&ctx->sem_ready); // wait for picture from input port0.
            if (ctx->exitcode == 1)
                return 1;
        }
        StitcherRegion region;
        getRegion(out, in->index, in_count, &region);
        pthread_mutex_lock(&in->pic_mutex);
        int width = STITCHER_MIN(region.width, in->width);
        int height = STITCHER_MIN(region.height, in->height);
        void *dst = out->buffer + out->offset + region.offset_y;
        void *src = in->buffer + in->offset;
        //fprintf(stderr, "%d: %p, %d, %d, %p, %d\n", in->index, out->buffer, out->offset, region.offset_y, in->buffer, in->offset);
        //fprintf(stderr, "%d: %dx%d, %d, %d\n", in->index, width, height, in->stride, out->stride);
        if (in->available_bufs > 0)
        {
            if (in->format == PLINK_COLOR_FormatYUV420SemiPlanarP010 ||
                in->format == PLINK_COLOR_FormatRawBayer10bit ||
                in->format == PLINK_COLOR_FormatRawBayer12bit)
            {
                int shift = in->format == PLINK_COLOR_FormatYUV420SemiPlanarP010 ? 2 : 8;
                unsigned int temp[4];
                for (int h = 0; h < height; h++)
                {
                    unsigned int *dst32 = (unsigned int *)dst;
                    unsigned short *src16 = (unsigned short *)src;
                    for (int w = 0; w < width; w+=4)
                    {
                        temp[0] = ((unsigned int)(src16[w+0] >> shift) & 0xFF);
                        temp[1] = ((unsigned int)(src16[w+1] >> shift) & 0xFF) << 8;
                        temp[2] = ((unsigned int)(src16[w+2] >> shift) & 0xFF) << 16;
                        temp[3] = ((unsigned int)(src16[w+3] >> shift) & 0xFF) << 24;
                        dst32[w>>2] = temp[0] | temp[1] | temp[2] | temp[3];
                    }
                    dst += out->stride;
                    src += in->stride;
                }
            }
            else
            {
                for (int h = 0; h < height; h++)
                {
                    memcpy(dst, src, width);
                    dst += out->stride;
                    src += in->stride;
                }
            }
        }
        else
        {
            for (int h = 0; h < height; h++)
            {
                memset(dst, 0x00, width);
                dst += out->stride;
            }
        }
        dst = out->buffer + out->offset_uv + region.offset_uv;
        src = in->buffer + in->offset_uv;
        //fprintf(stderr, "%d: %p, %d, %d, %p, %d\n", in->index, out->buffer, out->offset_uv, region.offset_uv, in->buffer, in->offset_uv);
        if (in->format == PLINK_COLOR_FormatYUV420SemiPlanar &&
            in->available_bufs > 0)
        {
            for (int h = 0; h < height/2; h++)
            {
                memcpy(dst, src, width);
                dst += out->stride;
                src += in->stride;
            }
        }
        else // treat all the other formats as monochrome
        {
            for (int h = 0; h < height/2; h++)
            {
                memset(dst, 0x80, width);
                dst += out->stride;
            }
        }
        pthread_mutex_unlock(&in->pic_mutex);
        if (in->index == 0)
            sem_post(&ctx->sem_done); // Resume input port0.
        // !Assume output thread is fast enough, so input port0 won't be blocked for long time.
    }

    return 0;
}

static void constructYuvInfo(PlinkYuvInfo *info, StitcherParams *params, unsigned int bus_address, int id)
{
    int size_y = params->width * params->stride;
    int size_uv = size_y / 2;
    int size_yuv = size_y + size_uv;
    
    info->header.type = PLINK_TYPE_2D_YUV;
    info->header.size = DATA_SIZE(*info);
    info->header.id = id + 1;

    info->format = params->format;
    info->bus_address_y = bus_address;
    info->bus_address_u = info->bus_address_y + size_y;
    info->bus_address_v = info->bus_address_u + (size_uv / 2);
    info->pic_width = params->width;
    info->pic_height = params->height;
    info->stride_y = params->stride;
    info->stride_u = 
        params->format == PLINK_COLOR_FormatYUV420Planar ? 
        params->stride/2 : params->stride;
    info->stride_v = info->stride_u;
}

int getBufferCount(PlinkPacket *pkt)
{
    int ret = 0;
    for (int i = 0; i < pkt->num; i++)
    {
        PlinkDescHdr *hdr = (PlinkDescHdr *)(pkt->list[i]);
        if (hdr->type == PLINK_TYPE_MESSAGE)
        {
            int *data = (int *)(pkt->list[i] + DATA_HEADER_SIZE);
            if (*data == PLINK_EXIT_CODE)
            {
                ret |= 0x80000000; // set bit 31 to 1 to indicate 'exit'
            }
            else if (*data >= 0)
                ret++;
        }
    }

    return ret;
}

static void retreiveSentBuffers(PlinkHandle plink, StitcherPort *port)
{
    PlinkStatus sts = PLINK_STATUS_OK;
    PlinkPacket pkt = {0};
    while (port->available_bufs < NUM_OF_BUFFERS && sts == PLINK_STATUS_OK)
    {
        do
        {
            sts = PLINK_recv(plink, port->id, &pkt);
            int count = getBufferCount(&pkt);
            if (count > 0)
            {
                port->available_bufs += count;
            }
        } while (sts == PLINK_STATUS_MORE_DATA);
    }
}

static void *input_thread(void *args)
{
    StitcherPort *port = (StitcherPort *)args;
    PlinkHandle plink = NULL;
    PlinkStatus sts = PLINK_STATUS_OK;
    void *vmem = port->vmem;

    pthread_mutex_init(&port->pic_mutex, NULL);

    if (PLINK_create(&plink, port->name, PLINK_MODE_CLIENT) != PLINK_STATUS_OK)
        return NULL;

    do
    {
        sts = PLINK_connect_ex(plink, NULL, 1000);
    } while (sts == PLINK_STATUS_TIMEOUT && *port->exit == 0);
    if (sts != PLINK_STATUS_OK)
    {
        PLINK_close(plink, 0);
        return NULL;
    }

    pthread_mutex_lock(port->count_mutex);
    port->index = *(port->in_count);
    *(port->in_count) = *(port->in_count) + 1;
    pthread_mutex_unlock(port->count_mutex);

    PlinkPacket sendpkt = {0};
    PlinkPacket recvpkt = {0};
    PlinkMsg msg = {0};
    VmemParams params;
    int exitcode = 0;
    do {
        sts = PLINK_recv(plink, 0, &recvpkt);
        if (sts == PLINK_STATUS_ERROR)
            break;

        for (int i = 0; i < recvpkt.num; i++)
        {
            PlinkDescHdr *hdr = (PlinkDescHdr *)(recvpkt.list[i]);
            if (hdr->type == PLINK_TYPE_2D_YUV || 
                hdr->type == PLINK_TYPE_2D_RAW)
            {
                // return previous buffer to source
                if (sendpkt.num > 0)
                {
                    if (port->index == 0)
                        sem_wait(port->sem_done);
                    pthread_mutex_lock(&port->pic_mutex);
                    if (VMEM_release(vmem, &params) != VMEM_STATUS_OK)
                        fprintf(stderr, "[STITCHER] ERROR: Failed to release buffer.\n");
                    sts = PLINK_send(plink, 0, &sendpkt);
                    if (sts == PLINK_STATUS_ERROR)
                        break;
                    if (sendpkt.fd != PLINK_INVALID_FD)
                        close(sendpkt.fd);
                    port->available_bufs--;
                    sendpkt.num = 0;
                }
                else
                    pthread_mutex_lock(&port->pic_mutex);

                memset(&params, 0, sizeof(params));
                if (recvpkt.fd != PLINK_INVALID_FD)
                {
                    params.fd = recvpkt.fd;
                    if (VMEM_import(vmem, &params) != VMEM_STATUS_OK)
                        break;
                    if (VMEM_mmap(vmem, &params) != VMEM_STATUS_OK)
                        break;
                }

                if (port->index == 0)
                    sem_post(port->sem_ready); // signal output thread that one picture is ready
            }

            if (hdr->type == PLINK_TYPE_2D_YUV)
            {
                PlinkYuvInfo *pic = (PlinkYuvInfo *)(recvpkt.list[i]);
                printf("[STITCHER] Input%d: Received YUV frame %d 0x%010llx from %s: fd %d, %dx%d, stride = luma %d, chroma %d\n", 
                        port->index, pic->header.id, pic->bus_address_y, port->name, recvpkt.fd,
                        pic->pic_width, pic->pic_height,
                        pic->stride_y, pic->stride_u);

                port->buffer = params.vir_address;
                port->format = pic->format;
                port->width = pic->pic_width;
                port->height = pic->pic_height;
                port->stride = pic->stride_y;
                port->offset = pic->offset_y;
                port->offset_uv = pic->offset_u > 0 ? pic->offset_u : (pic->offset_y + pic->pic_height*pic->stride_y);
            }
            else if (hdr->type == PLINK_TYPE_2D_RAW)
            {
                PlinkRawInfo *img = (PlinkRawInfo *)(recvpkt.list[i]);
                printf("[STITCHER] Input%d: Received RAW frame %d 0x%010llx from %s: fd %d, %dx%d, stride %d\n", 
                        port->index, img->header.id, img->bus_address, port->name, recvpkt.fd,
                        img->img_width, img->img_height, img->stride);

                port->buffer = params.vir_address;
                port->format = img->format;
                port->width = img->img_width;
                port->height = img->img_height;
                port->stride = img->stride;
                port->offset = img->offset;
            }
            else if (hdr->type == PLINK_TYPE_MESSAGE)
            {
                PlinkMsg *msg = (PlinkMsg *)(recvpkt.list[i]);
                if (msg->msg == PLINK_EXIT_CODE)
                {
                    exitcode = 1;
                    printf("[STITCHER] Input %d: Exit\n", port->index);
                    break;
                }
            }

            if (hdr->type == PLINK_TYPE_2D_YUV ||
                hdr->type == PLINK_TYPE_2D_RAW)
            {
                port->available_bufs++;
                pthread_mutex_unlock(&port->pic_mutex);

                msg.header.type = PLINK_TYPE_MESSAGE;
                msg.header.size = DATA_SIZE(PlinkMsg);
                msg.msg = hdr->id;
                sendpkt.list[0] = &msg;
                sendpkt.num = 1;
                sendpkt.fd = recvpkt.fd;
            }
        }
    } while (exitcode == 0 && *port->exit == 0 && sts != PLINK_STATUS_ERROR);

    if (sendpkt.num > 0)
    {
        pthread_mutex_lock(&port->pic_mutex);
        if (VMEM_release(vmem, &params) != VMEM_STATUS_OK)
            fprintf(stderr, "[STITCHER] ERROR: Failed to release buffer.\n");
        sts = PLINK_send(plink, 0, &sendpkt);
        port->available_bufs--;
        pthread_mutex_unlock(&port->pic_mutex);
    }

    if (port->index == 0)
    {
        *port->exit = 1;
        sem_post(port->sem_ready);
    }
    msg.header.type = PLINK_TYPE_MESSAGE;
    msg.header.size = DATA_SIZE(PlinkMsg);
    msg.msg = PLINK_EXIT_CODE;
    sendpkt.list[0] = &msg;
    sendpkt.num = 1;
    sendpkt.fd = PLINK_INVALID_FD;
    sts = PLINK_send(plink, 0, &sendpkt);
    PLINK_close(plink, 0);
    pthread_mutex_destroy(&port->pic_mutex);
    return NULL;
}

int main(int argc, char **argv) {
    PlinkStatus sts = PLINK_STATUS_OK;
    StitcherParams params;
    StitcherContext ctx;
    PlinkHandle plink = NULL;
    PlinkPacket pkt = {0};
    PlinkYuvInfo pic;
    PlinkMsg msg;

    parseParams(argc, argv, &params);
    if (checkParams(&params) != 0)
    {
        printUsage(argv[0]);
        return 0;
    }

    memset(&ctx, 0, sizeof(ctx));

    void *vmem = NULL;
    if (VMEM_create(&vmem) != VMEM_STATUS_OK)
        errExit("Failed to create VMEM.");

    int width = params.width;
    int height = params.height;
    int stride = params.stride;
    StitchLayout layout = params.layout;
    int size = getBufferSize(&params);
    if (size == 0)
        errExit("Wrong format or wrong resolution.");
    PictureBuffer picbuffers[NUM_OF_BUFFERS];
    AllocateBuffers(picbuffers, size, vmem);

    sem_init(&ctx.sem_ready, 0, 0);
    sem_init(&ctx.sem_done, 0, 0);
    pthread_mutex_init(&ctx.count_mutex, NULL);

    pthread_t thread_in[MAX_NUM_OF_INPUTS];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    for (int i = 0; i < MAX_NUM_OF_INPUTS; i++)
    {
        ctx.in[i].name = params.in_name[i];
        ctx.in[i].count_mutex = &ctx.count_mutex;
        ctx.in[i].sem_ready = &ctx.sem_ready;
        ctx.in[i].sem_done = &ctx.sem_done;
        ctx.in[i].in_count = &ctx.in_count;
        ctx.in[i].exit = &ctx.exitcode;
        ctx.in[i].vmem = vmem;
        if (pthread_create(&thread_in[i], &attr, input_thread, &ctx.in[i]) != 0)
            fprintf(stderr, "[STITCHER] ERROR: Failed to create thread for input %d\n", i);
    }

    sts = PLINK_create(&plink, params.out_name, PLINK_MODE_SERVER);

    StitcherPort *out = &ctx.out;
    out->available_bufs = NUM_OF_BUFFERS;
    out->count_mutex = &ctx.count_mutex;
    out->sem_ready = &ctx.sem_ready;
    out->sem_done = &ctx.sem_done;
    out->format = params.format;
    out->layout = params.layout;
    out->width = params.width;
    out->height = params.height;
    out->stride = params.stride;
    out->offset = 0;
    out->offset_uv = params.height * params.stride;
    out->exit = &ctx.exitcode;
    sts = PLINK_connect(plink, &out->id);

    int exitcode = 0;
    do {
        int sendid = out->sendid;
        out->buffer = picbuffers[sendid].virtual_address;
        if (stitchOneFrame(&ctx) != 0)
            break;
        constructYuvInfo(&pic, &params, picbuffers[sendid].bus_address, sendid);
        printf("[STITCHER] Processed frame %d 0x%010llx: %dx%d, stride = luma %d, chroma %d\n", 
                sendid, pic.bus_address_y, 
                pic.pic_width, pic.pic_height,
                pic.stride_y, pic.stride_u);

        pkt.list[0] = &pic;
        pkt.num = 1;
        pkt.fd = picbuffers[sendid].fd;
        sts = PLINK_send(plink, out->id, &pkt);
        out->sendid = (out->sendid + 1) % NUM_OF_BUFFERS;
        out->available_bufs -= 1;

        int timeout = out->available_bufs == 0 ? 100 : 0;
        if (PLINK_wait(plink, out->id, timeout) == PLINK_STATUS_OK)
        {
            do
            {
                sts = PLINK_recv(plink, out->id, &pkt);
                int count = getBufferCount(&pkt);
                if (count < 0)
                    exitcode = 1;
                out->available_bufs += count;
            } while (sts == PLINK_STATUS_MORE_DATA);
        }
    } while (exitcode == 0);

cleanup:
    ctx.exitcode = 1;
    sem_post(&ctx.sem_done);
    msg.header.type = PLINK_TYPE_MESSAGE;
    msg.header.size = DATA_SIZE(PlinkMsg);
    msg.msg = PLINK_EXIT_CODE;
    pkt.list[0] = &msg;
    pkt.num = 1;
    pkt.fd = PLINK_INVALID_FD;
    sts = PLINK_send(plink, out->id, &pkt);
    retreiveSentBuffers(plink, out);
    PLINK_recv_ex(plink, out->id, &pkt, 1000);
    for (int i = 0; i < MAX_NUM_OF_INPUTS; i++)
        pthread_join(thread_in[i], NULL);
    //sleep(1); // Sleep one second to make sure client is ready for exit
    if (vmem)
        FreeBuffers(picbuffers, vmem);
    PLINK_close(plink, PLINK_CLOSE_ALL);
    VMEM_destroy(vmem);
    pthread_mutex_destroy(&ctx.count_mutex);
    sem_destroy(&ctx.sem_ready);
    sem_destroy(&ctx.sem_done);
    exit(EXIT_SUCCESS);
}

