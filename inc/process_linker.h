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

#ifndef _PROCESS_LINKER_H_
#define _PROCESS_LINKER_H_

#define PLINK_VERSION_MAJOR     0
#define PLINK_VERSION_MINOR     1
#define PLINK_VERSION_REVISION  1

/* Maximum data descriptors in one packet */
#define PLINK_MAX_DATA_DESCS 4

/* Close all the connections from client. */
/* Can be used as the second parameter of PLINK_close when the instance is created as SERVER */
#define PLINK_CLOSE_ALL -1

/* invalid file descriptor */
#define PLINK_INVALID_FD -1

#define DATA_HEADER_SIZE (sizeof(PlinkDescHdr))
#define DATA_SIZE(type) (sizeof(type) - DATA_HEADER_SIZE)

typedef void *PlinkHandle;
typedef void PlinkDescriptor;
typedef int PlinkChannelID;

typedef enum _PlinkStatus
{
    PLINK_STATUS_OK = 0,
    PLINK_STATUS_MORE_DATA = 1,     /* have more data to parse in the receive buffer */
    PLINK_STATUS_TIMEOUT = 2,       /* wait timeout, which means no data received within the time */
    PLINK_STATUS_NO_DATA = 3,       /* no data recieved */
    PLINK_STATUS_ERROR = -1,        /* general error */
    PLINK_STATUS_WRONG_PARAMS = -2, /* wrong parameters */
    PLINK_STATUS_NO_MEMORY = -3,    /* not enough memory */
} PlinkStatus;

/* plink mode */
typedef enum _PlinkMode
{
    PLINK_MODE_SERVER = 0,      /* run plink as server; server should be launched before client */
    PLINK_MODE_CLIENT,          /* run plink as client which can connect to server */
    PLINK_MODE_MAX
} PlinkMode;

typedef union _PlinkVersion
{
    struct process_linker
    {
        unsigned char major;
        unsigned char minor;
        unsigned char revision;
        unsigned char step;
    } v;
    unsigned int version;
} PlinkVersion;

typedef struct _PlinkDescHdr
{
    unsigned int size;      /* data size, excluding this header */
    int type;               /* type of this data descriptor */
    int id;                 /* buffer id if it's buffer descriptor. Only values greater than 0 are valid */
} PlinkDescHdr;

/* data packet can be sent/received in one send/recv call */
typedef struct _PlinkPacket
{
    int fd;                                         /* file descriptor. If PLINK_INVALID_FD, it's invalid */
    unsigned int timestamp;                         /* timestamp of this packet, the time for rendering */
    int num;                                        /* number of valid data descriptor entries in list[] */
    PlinkDescriptor *list[PLINK_MAX_DATA_DESCS];    /* list of pointers which point to data descriptor. */
} PlinkPacket;

/**
 * \brief Create a plink instance.
 *
 * Create a plink object with the specified name as server or client.
 * When mode is PLINK_MODE_SERVER, a file of the specified name will be created.
 *
 * \param plink Point to the pointer of plink instance.
 * \param name Socket file name.
 * \param mode plink mode, server or client.
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_getVersion(PlinkVersion *version);

/**
 * \brief Create a plink instance.
 *
 * Create a plink object with the specified name as server or client.
 * When mode is PLINK_MODE_SERVER, a file of the specified name will be created.
 *
 * \param plink Point to the pointer of plink instance.
 * \param name Socket file name.
 * \param mode plink mode, server or client.
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_create(PlinkHandle *plink, const char *name, PlinkMode mode);

/**
 * \brief Create a connection between server and client
 *
 * Server calls this function to wait for connection and accept.
 * Client calls this function to connect to server.
 *
 * \param plink Pointer of plink instance.
 * \param channel id of the new connection. Valid for server only. Should be 0 for client
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_connect(PlinkHandle plink, PlinkChannelID *channel);

/**
 * \brief Create a connection between server and client with timeout
 *
 * Server calls this function to wait for connection and accept with timeout.
 * Client calls this function to connect to server with timeout.
 *
 * \param plink Pointer of plink instance.
 * \param channel id of the new connection. Valid for server only. Should be 0 for client
 * \param timeout_ms timeout in unit of milliseconds.
 * \return PLINK_STATUS_OK successful, 
 * \return PLINK_STATUS_TIMEOUT if no data received within timeout_ms, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_connect_ex(PlinkHandle plink, PlinkChannelID *channel, int timeout_ms);

/**
 * \brief Send a packet
 *
 * Send a packet through the channel.
 *
 * \param plink Pointer of plink instance.
 * \param channel The channel to send this packet. Valid for server only. Should be 0 for client
 * \param pkt Point to the packet to be sent.
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_send(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt);

/**
 * \brief Wait for data from channel
 *
 * This function returns once there is data received from the channel.
 *
 * \param plink Pointer of plink instance.
 * \param channel The channel to receive data. Valid for server only. Should be 0 for client
 * \param timeout_ms timeout in unit of milliseconds.
 * \return PLINK_STATUS_OK successful, 
 * \return PLINK_STATUS_TIMEOUT if no data received within timeout_ms, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_wait(PlinkHandle plink, PlinkChannelID channel, int timeout_ms);

/**
 * \brief Receive data
 *
 * Receive data from the channel.
 * Data descriptors of the packet are stored in the internal buffer, 
 * and may be overwritten in the next PLINK_recv call. 
 *
 * \param plink Pointer of plink instance.
 * \param channel The channel to receive data. Valid for server only. Should be 0 for client
 * \param pkt Point to the received packet.
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_recv(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt);

/**
 * \brief Receive data with timeout
 *
 * Receive data from the channel with timeout.
 * Data descriptors of the packet are stored in the internal buffer, 
 * and may be overwritten in the next PLINK_recv call. 
 *
 * \param plink Pointer of plink instance.
 * \param channel The channel to receive data. Valid for server only. Should be 0 for client
 * \param pkt Point to the received packet.
 * \param timeout_ms timeout in unit of milliseconds.
 * \return PLINK_STATUS_OK successful, 
 * \return PLINK_STATUS_TIMEOUT if no data received within timeout_ms, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_recv_ex(PlinkHandle plink, PlinkChannelID channel, PlinkPacket *pkt, int timeout_ms);

/**
 * \brief Close connections
 *
 * Close connections. Server can set channel to PLINK_CLOSE_ALL to close all connections.
 *
 * \param plink Pointer of plink instance.
 * \param channel The connection to be closed. Valid for server only. Should be 0 for client
 * \return PLINK_STATUS_OK successful, 
 * \return other unsuccessful.
 */
PlinkStatus PLINK_close(PlinkHandle plink, PlinkChannelID channel);

#endif /* !_PROCESS_LINKER_H_ */
