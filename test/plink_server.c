/*
 * Copyright (c) 2021 Alibaba Group. All rights reserved.
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
#include <memory.h>
#include <errno.h>
#include "process_linker_types.h"
#include "video_mem.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

#define NUM_OF_BUFFERS  5
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

typedef struct _ServerParams
{
    char *plinkname;
    char *inputfile;
    PlinkColorFormat format;
    int width;
    int height;
    int stride;
    int frames;
} ServerParams;

typedef struct _PlinkChannel
{
    PlinkChannelID id;
    PlinkHandle plink;
    PlinkPacket pkt;
    int sendid;
    int backid;
    int exit;
    int available_bufs;
} PlinkChannel;

typedef struct _PictureBuffer
{
  unsigned int bus_address;
  void *virtual_address;
  unsigned int size;
  int fd;
} PictureBuffer;

void printUsage(char *name)
{
    printf("usage: %s [options]\n"
           "\n"
           "  Available options:\n"
           "    -l      plink file name (default: /tmp/plink.test)\n"
           "    -i      input YUV file name (mandatory)\n"
           "    -f      input color format (default: 3)\n"
           "                2 - I420\n"
           "                3 - NV12\n"
           "                4 - P010\n"
           "                14 - Bayer Raw 10bit\n"
           "                15 - Bayer Raw 12bit\n"
           "    -w      video width (mandatory)\n"
           "    -h      video height (mandatory)\n"
           "    -s      video buffer stride in bytes (default: video width)\n"
           "    -n      number of frames to send (default: 10)\n"
           "\n", name);
}

void parseParams(int argc, char **argv, ServerParams *params)
{
    int i = 1;
    memset(params, 0, sizeof(*params));
    params->plinkname = "/tmp/plink.test";
    params->format = PLINK_COLOR_FormatYUV420SemiPlanar;
    params->frames = 10;
    while (i < argc)
    {
        if (argv[i][0] != '-' || strlen(argv[i]) < 2)
        {
            i++;
            continue;
        }

        if (argv[i][1] == 'l')
        {
            if (++i < argc)
            {
                params->plinkname = argv[i++];
            }
        }
        else if (argv[i][1] == 'i')
        {
            if (++i < argc)
            {
                params->inputfile = argv[i++];
            }
        }
        else if (argv[i][1] == 'f')
        {
            if (++i < argc)
            {
                params->format = atoi(argv[i++]);
            }
        }
        else if (argv[i][1] == 'w')
        {
            if (++i < argc)
            {
                params->width = atoi(argv[i++]);
            }
        }
        else if (argv[i][1] == 'h')
        {
            if (++i < argc)
            {
                params->height = atoi(argv[i++]);
            }
        }
        else if (argv[i][1] == 's')
        {
            if (++i < argc)
            {
                params->stride = atoi(argv[i++]);
            }
        }
        else if (argv[i][1] == 'n')
        {
            if (++i < argc)
            {
                params->frames = atoi(argv[i++]);
            }
        }
    }

    if ((params->format == PLINK_COLOR_FormatYUV420SemiPlanarP010 ||
            params->format == PLINK_COLOR_FormatRawBayer10bit ||
            params->format == PLINK_COLOR_FormatRawBayer12bit) &&
        params->stride < params->width*2)
    {
        params->stride = params->width*2;
    }
    else if (params->stride < params->width)
    {
        params->stride = params->width;
    }
}

int checkParams(ServerParams *params)
{
    if (params->plinkname == NULL ||
        params->inputfile == NULL ||
        params->format == PLINK_COLOR_FormatUnused ||
        params->width == 0 ||
        params->height == 0 ||
        params->stride == 0)
        return -1;
    return 0;
}

int getBufferSize(ServerParams *params)
{
    int size = 0;
    switch (params->format)
    {
        case PLINK_COLOR_FormatYUV420Planar:
        case PLINK_COLOR_FormatYUV420SemiPlanar:
            size = params->stride * params->height * 3 / 2;
            break;
        case PLINK_COLOR_FormatYUV420SemiPlanarP010:
            size = params->stride * params->height * 3;
            break;
        case PLINK_COLOR_FormatRawBayer10bit:
        case PLINK_COLOR_FormatRawBayer12bit:
            size = params->stride * params->height;
            break;
        default:
            size = 0;
    }
    return size;
}

void AllocateBuffers(PictureBuffer picbuffers[NUM_OF_BUFFERS], unsigned int size, void *vmem)
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
        printf("[SERVER] mmap %p from %x with size %d, dma-buf fd %d\n", 
                params.vir_address, params.phy_address, params.size, params.fd);
        picbuffers[i].virtual_address = params.vir_address;
        picbuffers[i].bus_address = params.phy_address;
        picbuffers[i].size = buffer_size;
        picbuffers[i].fd = params.fd;
    }
}

void FreeBuffers(PictureBuffer picbuffers[NUM_OF_BUFFERS], void *vmem)
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

void ProcessOneFrame(void *virtual_address, FILE *fp, int size)
{
    if(virtual_address != MAP_FAILED && fp != NULL)
    {
        if (fread(virtual_address, size, 1, fp) < 1)
            fprintf(stderr, "ERROR: Failed to read %d bytes from file (eof %d): %s\n",
                size, feof(fp), strerror(errno));
    }
}

void constructYuvInfo(PlinkYuvInfo *info, ServerParams *params, unsigned int bus_address, int id)
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

void constructRawInfo(PlinkRawInfo *info, ServerParams *params, unsigned int bus_address, int id)
{
    int size_y = params->width * params->stride;
    int size_uv = size_y / 2;
    int size_yuv = size_y + size_uv;
    
    info->header.type = PLINK_TYPE_2D_RAW;
    info->header.size = DATA_SIZE(*info);
    info->header.id = id + 1;

    info->format = params->format;
    info->bus_address = bus_address;
    info->img_width = params->width;
    info->img_height = params->height;
    info->stride = params->stride;
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

void retreiveSentBuffers(PlinkHandle plink, PlinkChannel *channel)
{
    PlinkStatus sts = PLINK_STATUS_OK;
    while (channel->available_bufs < NUM_OF_BUFFERS && sts == PLINK_STATUS_OK)
    {
        do
        {
            sts = PLINK_recv(plink, channel->id, &channel->pkt);
            int count = getBufferCount(&channel->pkt);
            if (count > 0)
            {
                channel->available_bufs += count;
            }
        } while (sts == PLINK_STATUS_MORE_DATA);
    }
}


int main(int argc, char **argv) {
    PlinkStatus sts = PLINK_STATUS_OK;
    ServerParams params;
    PlinkChannel channel[2];
    PlinkHandle plink = NULL;
    PlinkYuvInfo pic = {0};
    PlinkRawInfo img = {0};
    PlinkMsg msg;

    parseParams(argc, argv, &params);
    if (checkParams(&params) != 0)
    {
        printUsage(argv[0]);
        return 0;
    }

    FILE *fp = fopen(params.inputfile, "rb");
    if (fp == NULL)
        errExit("fopen");

    void *vmem = NULL;
    if (VMEM_create(&vmem) != VMEM_STATUS_OK)
        errExit("Failed to create VMEM.");

    int width = params.width;
    int height = params.height;
    int stride = params.stride;
    int frames = params.frames;
    int size = getBufferSize(&params);
    if (size == 0)
        errExit("Wrong format or wrong resolution.");
    PictureBuffer picbuffers[NUM_OF_BUFFERS];
    AllocateBuffers(picbuffers, size, vmem);

    sts = PLINK_create(&plink, params.plinkname, PLINK_MODE_SERVER);

    memset(&channel[0], 0, sizeof(channel[0]));
    channel[0].available_bufs = NUM_OF_BUFFERS;
    sts = PLINK_connect(plink, &channel[0].id);

    int frmcnt = 0;
    do {
        int sendid = channel[0].sendid;
        ProcessOneFrame(picbuffers[sendid].virtual_address, fp, size);
        if (params.format == PLINK_COLOR_FormatRawBayer8bit ||
            params.format == PLINK_COLOR_FormatRawBayer10bit ||
            params.format == PLINK_COLOR_FormatRawBayer12bit)
        {
            constructRawInfo(&img, &params, picbuffers[sendid].bus_address, sendid);
            printf("[SERVER] Processed frame %d 0x%010llx: %dx%d, stride %d\n", 
                    sendid, img.bus_address, img.img_width, img.img_height, img.stride);
            channel[0].pkt.list[0] = &img;
        }
        else // YUV
        {
            constructYuvInfo(&pic, &params, picbuffers[sendid].bus_address, sendid);
            printf("[SERVER] Processed frame %d 0x%010llx: %dx%d, stride = luma %d, chroma %d\n", 
                    sendid, pic.bus_address_y, 
                    pic.pic_width, pic.pic_height,
                    pic.stride_y, pic.stride_u);
            channel[0].pkt.list[0] = &pic;
        }

        channel[0].pkt.num = 1;
        channel[0].pkt.fd = picbuffers[sendid].fd;
        sts = PLINK_send(plink, channel[0].id, &channel[0].pkt);
        channel[0].sendid = (channel[0].sendid + 1) % NUM_OF_BUFFERS;
        channel[0].available_bufs -= 1;

        int timeout = 0;
        if (channel[0].available_bufs == 0)
            timeout = 60000; // wait up to 60 seconds if buffers are used up

        if (PLINK_wait(plink, channel[0].id, timeout) == PLINK_STATUS_OK)
        {
            do
            {
                sts = PLINK_recv(plink, channel[0].id, &channel[0].pkt);
                int count = getBufferCount(&channel[0].pkt);
                if (count < 0)
                    channel[0].exit = 1;
                channel[0].available_bufs += count;
            } while (sts == PLINK_STATUS_MORE_DATA);
        }

        frmcnt++;
    } while (channel[0].exit == 0 && frmcnt < frames);

cleanup:
    msg.header.type = PLINK_TYPE_MESSAGE;
    msg.header.size = DATA_SIZE(PlinkMsg);
    msg.msg = PLINK_EXIT_CODE;
    channel[0].pkt.list[0] = &msg;
    channel[0].pkt.num = 1;
    channel[0].pkt.fd = PLINK_INVALID_FD;
    sts = PLINK_send(plink, channel[0].id, &channel[0].pkt);
    retreiveSentBuffers(plink, &channel[0]);
    PLINK_recv_ex(plink, channel[0].id, &channel[0].pkt, 1000);
    //sleep(1); // Sleep one second to make sure client is ready for exit
    if (vmem)
        FreeBuffers(picbuffers, vmem);
    PLINK_close(plink, PLINK_CLOSE_ALL);
    VMEM_destroy(vmem);
    if (fp != NULL)
        fclose(fp);
    exit(EXIT_SUCCESS);
}

