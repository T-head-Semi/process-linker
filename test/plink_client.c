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
#include "process_linker_types.h"
#include "video_mem.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

#define NUM_OF_BUFFERS  5
#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

int main(int argc, char **argv) {
    PlinkStatus sts = PLINK_STATUS_OK;
    PlinkPacket sendpkt, recvpkt;
    PlinkMsg msg;
    PlinkHandle plink = NULL;
    VmemParams params;
    void *vmem = NULL;
    FILE *fp = NULL;
    int exitcode = 0;

    int frames = argc > 1 ? atoi(argv[1]) : 1000;
    char *plinkname = argc > 2 ? argv[2] : "/tmp/plink.test";
    char *dumpname = argc > 3 ? argv[3] : NULL;

    if (dumpname != NULL)
    {
        fp = fopen(dumpname, "wb");
        if (fp == NULL)
            errExit("fopen");
    }

    if (VMEM_create(&vmem) != VMEM_STATUS_OK)
        errExit("Failed to create VMEM.");

    if (PLINK_create(&plink, plinkname, PLINK_MODE_CLIENT) != PLINK_STATUS_OK)
        errExit("Failed to create PLINK.");

    if (PLINK_connect(plink, NULL) != PLINK_STATUS_OK)
        errExit("Failed to connect to server.");

    int frmcnt = 0;
    do {
        sts = PLINK_recv(plink, 0, &recvpkt);
        memset(&params, 0, sizeof(params));
        if (recvpkt.fd != PLINK_INVALID_FD)
        {
            params.fd = recvpkt.fd;
            if (VMEM_import(vmem, &params) != VMEM_STATUS_OK)
                errExit("Failed to import fd.");
            if (VMEM_mmap(vmem, &params) != VMEM_STATUS_OK)
                errExit("Failed to mmap buffer.");
        }

        for (int i = 0; i < recvpkt.num; i++)
        {
            PlinkDescHdr *hdr = (PlinkDescHdr *)(recvpkt.list[i]);
            if (hdr->type == PLINK_TYPE_2D_YUV)
            {
                PlinkYuvInfo *pic = (PlinkYuvInfo *)(recvpkt.list[i]);
                printf("[CLIENT] Received YUV frame %d 0x%010llx: fd %d, %dx%d, stride = luma %d, chroma %d\n", 
                        pic->header.id, pic->bus_address_y, recvpkt.fd,
                        pic->pic_width, pic->pic_height,
                        pic->stride_y, pic->stride_u);

                // Save YUV data to file
                if (fp != NULL && params.vir_address != NULL)
                {
                    void *buffer = params.vir_address;
                    for (int i = 0; i < pic->pic_height * 3 / 2; i++)
                    {
                        fwrite(buffer, pic->pic_width, 1, fp);
                        buffer += pic->stride_y;
                    }
                }

                // return the buffer to source
                msg.header.type = PLINK_TYPE_MESSAGE;
                msg.header.size = DATA_SIZE(PlinkMsg);
                msg.msg = hdr->id;
                sendpkt.list[0] = &msg;
                sendpkt.num = 1;
                sendpkt.fd = PLINK_INVALID_FD;
                if (PLINK_send(plink, 0, &sendpkt) == PLINK_STATUS_ERROR)
                    errExit("Failed to send data.");
            }
            if (hdr->type == PLINK_TYPE_2D_RGB)
            {
                PlinkRGBInfo *pic = (PlinkRGBInfo *)(recvpkt.list[i]);
                printf("[CLIENT] Received RGB picture %d 0x%010llx: fd %d, %dx%d, stride %d/%d/%d/%d\n", 
                        pic->header.id, pic->bus_address_r, recvpkt.fd,
                        pic->img_width, pic->img_height,
                        pic->stride_r, pic->stride_g, pic->stride_b, pic->stride_a);

                // Save RGB data to file
                if (fp != NULL && params.vir_address != NULL && pic->format == PLINK_COLOR_Format24BitBGR888Planar)
                {
                    void *buffer = params.vir_address;
                    for (int i = 0; i < pic->img_height * 3; i++)
                    {
                        fwrite(buffer, pic->img_width, 1, fp);
                        buffer += pic->stride_r;
                    }
                }

                // return the buffer to source
                msg.header.type = PLINK_TYPE_MESSAGE;
                msg.header.size = DATA_SIZE(PlinkMsg);
                msg.msg = hdr->id;
                sendpkt.list[0] = &msg;
                sendpkt.num = 1;
                sendpkt.fd = PLINK_INVALID_FD;
                if (PLINK_send(plink, 0, &sendpkt) == PLINK_STATUS_ERROR)
                    errExit("Failed to send data.");
            }
            if (hdr->type == PLINK_TYPE_2D_RAW)
            {
                PlinkRawInfo *pic = (PlinkRawInfo *)(recvpkt.list[i]);
                printf("[CLIENT] Received RAW picture %d 0x%010llx: fd %d, %dx%d, stride %d\n", 
                        pic->header.id, pic->bus_address, recvpkt.fd,
                        pic->img_width, pic->img_height, pic->stride);

                // Save RAW data to file
                if (fp != NULL && params.vir_address != NULL)
                    fwrite(params.vir_address, pic->stride * pic->img_height, 1, fp);

                // return the buffer to source
                msg.header.type = PLINK_TYPE_MESSAGE;
                msg.header.size = DATA_SIZE(PlinkMsg);
                msg.msg = hdr->id;
                sendpkt.list[0] = &msg;
                sendpkt.num = 1;
                sendpkt.fd = PLINK_INVALID_FD;
                if (PLINK_send(plink, 0, &sendpkt) == PLINK_STATUS_ERROR)
                    errExit("Failed to send data.");
            }
            else if (hdr->type == PLINK_TYPE_MESSAGE)
            {
                PlinkMsg *msg = (PlinkMsg *)(recvpkt.list[i]);
                if (msg->msg == PLINK_EXIT_CODE)
                {
                    exitcode = 1;
                    printf("Exit\n");
                    break;
                }
            }
        }

        if (VMEM_release(vmem, &params) != VMEM_STATUS_OK)
            errExit("Failed to release buffer.");

        if (recvpkt.fd != PLINK_INVALID_FD)
            close(recvpkt.fd);

        frmcnt++;

        if (frmcnt >= frames)
        {
            msg.header.type = PLINK_TYPE_MESSAGE;
            msg.header.size = DATA_SIZE(PlinkMsg);
            msg.msg = PLINK_EXIT_CODE;
            sendpkt.list[0] = &msg;
            sendpkt.num = 1;
            sendpkt.fd = PLINK_INVALID_FD;
            if (PLINK_send(plink, 0, &sendpkt) == PLINK_STATUS_ERROR)
                errExit("Failed to send data.");
            break;
        }
    } while (exitcode == 0);

cleanup:
    sleep(1); // Sleep one second to make sure server is ready for exit
    PLINK_close(plink, 0);
    VMEM_destroy(vmem);
    if (fp != NULL)
        fclose(fp);
    exit(EXIT_SUCCESS);
}
