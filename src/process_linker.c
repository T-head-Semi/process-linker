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

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "process_linker.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

#define MAX_CONNECTIONS 3
#define MAX_BUFFER_SIZE (4 * 1024 * 1024)

#define PLINK_PRINT(level, ...) \
    { \
        if (log_level >= PLINK_LOG_##level) \
        { \
            struct timeval ts; \
            gettimeofday(&ts, 0); \
            printf("PLINK[%d][%ld.%06ld] %s: ", pid, ts.tv_sec, ts.tv_usec, #level); \
            printf(__VA_ARGS__); \
        } \
    }

#define PLINK_PRINT_RETURN(retcode, level, ...) \
    { \
        PLINK_PRINT(level, __VA_ARGS__) \
        return retcode; \
    }

typedef enum _PlinkLogLevel
{
    PLINK_LOG_QUIET = 0,
    PLINK_LOG_ERROR,
    PLINK_LOG_WARNING,
    PLINK_LOG_INFO,
    PLINK_LOG_DEBUG,
    PLINK_LOG_TRACE,
    PLINK_LOG_MAX
} PlinkLogLevel;

typedef struct _PlinkContext
{
    PlinkMode mode;
    struct sockaddr_un addr;
    struct iovec io[PLINK_MAX_DATA_DESCS];
    int sockfd;
    int cfd[MAX_CONNECTIONS];
    int connect[MAX_CONNECTIONS];
    int count; // connected client number
    char *buffer;
    int offset;
    int pid;
} PlinkContext;

int log_level = PLINK_LOG_ERROR;
int pid = 0;

static PlinkStatus parseData(PlinkContext *ctx, PlinkPacket *pkt, int total);
static PlinkStatus wait(int sockfd, int timeout_ms);
static int getLogLevel();

PlinkStatus
PLINK_getVersion(PlinkVersion *version)
{
    if (version != NULL)
    {
        version->v.major = PLINK_VERSION_MAJOR;
        version->v.minor = PLINK_VERSION_MINOR;
        version->v.revision = PLINK_VERSION_REVISION;
        version->v.step = 0;
        return PLINK_STATUS_OK;
    }
    else
        return PLINK_STATUS_ERROR;
}

PlinkStatus 
PLINK_create(PlinkHandle *plink, const char *name, PlinkMode mode)
{
    PlinkContext *ctx = NULL;
    int sockfd;

    log_level = getLogLevel();
    pid = getpid();

    if (plink == NULL || name == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p, name = %s\n", plink, name);

    ctx = (PlinkContext *)malloc(sizeof(*ctx));
    if (NULL == ctx)
        PLINK_PRINT_RETURN(PLINK_STATUS_NO_MEMORY, ERROR,
            "Failed to allocate memory for plink\n");
    memset(ctx, 0, sizeof(*ctx));
    *plink = (PlinkHandle)ctx;

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (-1 == sockfd)
        PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
            "Failed to create socket as AF_UNIX, SOCK_STREAM\n");

    ctx->sockfd = sockfd;
    ctx->mode = mode;
    ctx->addr.sun_family = AF_UNIX;
    strncpy(ctx->addr.sun_path, name, sizeof(ctx->addr.sun_path) - 1);

    if (mode == PLINK_MODE_SERVER)
    {
        if (unlink (name) == -1 && errno != ENOENT)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
                "Failed to unlink %s\n", name);

        if (bind(sockfd, (struct sockaddr *)&ctx->addr, sizeof(struct sockaddr_un)) == -1)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
                "Failed to bind to AF_UNIX address: %s\n", ctx->addr.sun_path);

        if (listen(sockfd, MAX_CONNECTIONS) == -1)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
                "Failed to listen for connection request\n");
    }

    ctx->buffer = malloc(MAX_BUFFER_SIZE);
    if (ctx->buffer == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_NO_MEMORY, ERROR,
            "Failed to allocate memory for internal buffer\n");

    ctx->pid = getpid();

    return PLINK_STATUS_OK;
}

PlinkStatus 
PLINK_connect(PlinkHandle plink, PlinkChannelID *channel)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p\n", plink);

    if (ctx->mode == PLINK_MODE_SERVER)
    {
        if (channel == NULL)
            PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
                "Wrong parameters: channel = %p\n", channel);

        if (ctx->count >= MAX_CONNECTIONS)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
               "Too many connections %d while it is limited to %d\n", ctx->count, MAX_CONNECTIONS);

        // find available slot
        int i;
        for (i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (ctx->connect[i] == 0)
            {
                ctx->connect[i] = 1;
                *channel = i;
                break;
            }
        }

        // wait for connection from client
        PLINK_PRINT(INFO, "Waiting for connection...\n");
        int fd = accept(ctx->sockfd, NULL, NULL);
        if (fd == -1)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
               "Failed to accept connection\n");

        ctx->cfd[i] = fd;
        ctx->count++;
        PLINK_PRINT(INFO, "Accepted connection request from client %d (%d/%d): %d\n", 
                i, ctx->count, MAX_CONNECTIONS, fd);
    }
    else
    {
        if (connect(ctx->sockfd, (struct sockaddr *)&ctx->addr, sizeof(struct sockaddr_un)) == -1)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
               "Failed to connect to server %s: %s\n", ctx->addr.sun_path, strerror(errno));

        PLINK_PRINT(INFO, "Connected to server: %d\n", ctx->sockfd);
    }

    return PLINK_STATUS_OK;
}

PlinkStatus 
PLINK_send(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL || pkt == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p, pkt = %p\n", plink, pkt);

    if (pkt->num > PLINK_MAX_DATA_DESCS)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Too many data nodes to send: %d\n", pkt->num);

    char buf[CMSG_SPACE(sizeof(int))];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < pkt->num; i++)
    {
        PlinkDescHdr *hdr = (PlinkDescHdr *)(pkt->list[i]);
        ctx->io[i].iov_base = pkt->list[i];
        ctx->io[i].iov_len = hdr->size + DATA_HEADER_SIZE;
        PLINK_PRINT(INFO, "Sending %ld bytes\n", ctx->io[i].iov_len);
    }

    struct msghdr msg = {0};
    msg.msg_iov = ctx->io;
    msg.msg_iovlen = pkt->num;

    if (pkt->fd > PLINK_INVALID_FD)
    {
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        struct cmsghdr *cmsg;
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        *((int *)CMSG_DATA(cmsg)) = pkt->fd;
        PLINK_PRINT(INFO, "Sent fd %d\n", pkt->fd);
    }

    int sockfd = ctx->mode == PLINK_MODE_SERVER ? ctx->cfd[channel] : ctx->sockfd;
    if (sendmsg(sockfd, &msg, 0) == -1)
        PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
            "sendmsg() failed: %s\n", strerror(errno));
    PLINK_PRINT(INFO, "Sent data to %d\n", sockfd);

    return PLINK_STATUS_OK;
}

PlinkStatus 
PLINK_recv(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL || pkt == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p, pkt = %p\n", plink, pkt);

    pkt->num = 0;

    char buf[CMSG_SPACE(sizeof(int))];
    memset(buf, 0, sizeof(buf));
    ctx->io[0].iov_base = ctx->buffer + ctx->offset;
    ctx->io[0].iov_len = MAX_BUFFER_SIZE - ctx->offset;

    struct msghdr msg = {0};
    msg.msg_iov = ctx->io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    int sockfd = ctx->mode == PLINK_MODE_SERVER ? ctx->cfd[channel] : ctx->sockfd;
    PLINK_PRINT(INFO, "Receiving data from %d\n", sockfd);
    int total = recvmsg (sockfd, &msg, 0);
    if (total > 0)
        PLINK_PRINT(INFO, "Received %d bytes\n", total)
    else if (total == 0)
        PLINK_PRINT_RETURN(PLINK_STATUS_NO_DATA, WARNING,
            "recvmsg() returns %d\n", total)
    else
        PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
            "Failed to recieve data from %d: %s\n", sockfd, strerror(errno))

    if (msg.msg_controllen >= CMSG_SPACE(sizeof(int)))
    {
        struct cmsghdr *cmsg;
        cmsg = CMSG_FIRSTHDR(&msg);
        pkt->fd = *((int *)CMSG_DATA(cmsg));
        PLINK_PRINT(INFO, "Received fd %d\n", pkt->fd);
    }
    else
        pkt->fd = PLINK_INVALID_FD;

    return parseData(ctx, pkt, ctx->offset + total);
}

PlinkStatus 
PLINK_wait(PlinkHandle plink, PlinkChannelID channel, int timeout_ms)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p\n", plink);

    int sockfd = ctx->mode == PLINK_MODE_SERVER ? ctx->cfd[channel] : ctx->sockfd;
    return wait(sockfd, timeout_ms);
}

PlinkStatus 
PLINK_close(PlinkHandle plink, PlinkChannelID channel)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p\n", plink);

    if (ctx->mode == PLINK_MODE_SERVER)
    {
        if (channel == PLINK_CLOSE_ALL)
        {
            // close all connections
            int i;
            for (i = 0; i < MAX_CONNECTIONS; i++)
            {
                if (ctx->connect[i] != 0)
                {
                    ctx->connect[i] = 0;
                    close(ctx->cfd[i]);
                    ctx->count--;
                    PLINK_PRINT(INFO, "Closed channel %d\n", i);
                }
            }

            unlink(ctx->addr.sun_path);
            close(ctx->sockfd);
            if (ctx->buffer != NULL)
                free(ctx->buffer);

            free(ctx);
        }
        else if (channel < MAX_CONNECTIONS && ctx->connect[channel] != 0)
        {
            close(ctx->cfd[channel]);
            ctx->count--;
            PLINK_PRINT(INFO, "Closed channel %d\n", channel);
        }
        else
        {
            PLINK_PRINT(ERROR, "Invalid channel: %d\n", channel);
        }

        unlink(ctx->addr.sun_path);
    }
    else
    {
        close(ctx->sockfd);
        if (ctx->buffer != NULL)
            free(ctx->buffer);

        free(ctx);
    }

    return PLINK_STATUS_OK;
}

PlinkStatus
PLINK_connect_ex(PlinkHandle plink, PlinkChannelID *channel, int timeout_ms)
{
    PlinkContext *ctx = (PlinkContext *)plink;

    if (ctx == NULL)
        PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
            "Wrong parameters: plink = %p\n", plink);

    int i = 0;
    if (ctx->mode == PLINK_MODE_SERVER)
    {
        if (channel == NULL)
            PLINK_PRINT_RETURN(PLINK_STATUS_WRONG_PARAMS, ERROR,
                "Wrong parameters: channel = %p\n", channel);

        if (ctx->count >= MAX_CONNECTIONS)
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
               "Too many connections %d while it is limited to %d\n", ctx->count, MAX_CONNECTIONS);

        // find available slot
        for (i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (ctx->connect[i] == 0)
            {
                ctx->connect[i] = 1;
                *channel = i;
                break;
            }
        }

        // wait for connection from client
        PLINK_PRINT(INFO, "Waiting for connection...\n");
        if (wait(ctx->sockfd, timeout_ms) == PLINK_STATUS_OK)
        {
            int fd = accept(ctx->sockfd, NULL, NULL);
            if (fd == -1)
                PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
                    "Failed to accept connection\n");

            ctx->cfd[i] = fd;
            ctx->count++;
            PLINK_PRINT(INFO, "Accepted connection request from client %d (%d/%d): %d\n", 
                    i, ctx->count, MAX_CONNECTIONS, fd);
        }
        else
            PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
                "No connection request within %dms\n", timeout_ms);
    }
    else
    {
        int max_tries = (timeout_ms + 1000 - 1) / 1000;
        for (i = 0; i < max_tries; i++)
        {
            if (connect(ctx->sockfd, (struct sockaddr *)&ctx->addr, sizeof(struct sockaddr_un)) == -1)
                sleep(1);
            else
                break;
        }

        if (i >= max_tries)
            PLINK_PRINT_RETURN(PLINK_STATUS_TIMEOUT, WARNING,
               "Failed to connect to server %s\n", ctx->addr.sun_path);

        PLINK_PRINT(INFO, "Connected to server: %d\n", ctx->sockfd);
    }

    return PLINK_STATUS_OK;
}

PlinkStatus 
PLINK_recv_ex(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt, int timeout_ms)
{
    int ret = PLINK_wait(plink, channel, timeout_ms);
    if (ret == PLINK_STATUS_OK)
    {
        return PLINK_recv(plink, channel, pkt);
    }

    return ret;
}


static PlinkStatus 
parseData(PlinkContext *ctx, PlinkPacket *pkt, int remaining)
{
    PlinkStatus sts = PLINK_STATUS_OK;

    if (ctx == NULL)
        return PLINK_STATUS_ERROR;

    int index = 0;
    void *buffer = ctx->buffer;
    while (remaining >= DATA_HEADER_SIZE)
    {
        if (index >= PLINK_MAX_DATA_DESCS)
        {
            // not enough buffer to store received data, need another recv call
            sts = PLINK_STATUS_MORE_DATA;
            break;
        }

        PlinkDescHdr *hdr = (PlinkDescHdr *)buffer;
        if (remaining < (unsigned int)hdr->size)
        {
            PLINK_PRINT(WARNING, "Not enough data received. Expect %d while only %d available\n", hdr->size, remaining);
            // return to get more data in next recvmsg call
            break;
        }

        pkt->list[index] = buffer;

        buffer += DATA_HEADER_SIZE + hdr->size;
        remaining -= DATA_HEADER_SIZE + hdr->size;
        index++;
    }

    // move the remaining data to the beginning of the buffer
    if (remaining > 0)
    {
        memmove(ctx->buffer, buffer, remaining);
        ctx->offset = remaining;
    }

    pkt->num = sts == PLINK_STATUS_MORE_DATA ? index-1 : index;

    return sts;
}

static int getLogLevel()
{
    char *env = getenv("PLINK_LOG_LEVEL");
    if (env == NULL)
        return PLINK_LOG_ERROR;
    else
    {
        int level = atoi(env);
        if (level >= PLINK_LOG_MAX || level < PLINK_LOG_QUIET)
            return PLINK_LOG_ERROR;
        else
            return level;
    }
}

static PlinkStatus 
wait(int sockfd, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int ret;

    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    PLINK_PRINT(INFO, "Waiting for data from socket %d, timeout %dms\n", sockfd, timeout_ms);
    ret = select(sockfd+1, &rfds, NULL, NULL, &tv);
    if (ret == -1)
        PLINK_PRINT_RETURN(PLINK_STATUS_ERROR, ERROR,
            "Failed to wait for data\n")
    else if (ret)
        return PLINK_STATUS_OK;
    else if (timeout_ms > 0)
        PLINK_PRINT_RETURN(PLINK_STATUS_TIMEOUT, WARNING,
            "Wait timeout\n");

    return PLINK_STATUS_TIMEOUT;
}
