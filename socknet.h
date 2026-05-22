/**
 * @file socknet.h
 * @brief Library for client-server socket management
 * @author Vitolo Mirko
 * @date 2026-05-11
 */

#ifndef SOCKNET_H
#define SOCKNET_H

#include <stdlib.h>
#include <stdio.h>

#ifndef SOCKNET_MALLOC
/**
 * @brief Customizable macro to allocate memory
 * @param siz Size in bytes to allocate
 * @return Pointer to allocated memory
 */
#define SOCKNET_MALLOC(siz) malloc(siz)
#endif

#ifndef SOCKNET_FREE
/**
 * @brief Macro to free allocated memory
 * @param ptr Memory pointer to deallocate
 */
#define SOCKNET_FREE(ptr) free(ptr)
#endif

#ifndef SOCKNET_OK
/**
 * @brief Success code of a function
 */
#define SOCKNET_OK 0
#endif

#ifndef SOCKNET_NO
/**
 * @brief Function failure code
 */
#define SOCKNET_NO 1
#endif

/**
 * @brief Transform an 8, 16, 32, 64 bit number to big endian
 * @param n Number to transform
 * @return Number transformed into big endian
 */
#define socknet_host2net(n) \
    !socknet_islittleen() ? (n) : \
    (sizeof(n) == 1) ? (n) : \
    (sizeof(n) == 2) ? (((n) >> 8) | ((n) << 8)) : \
    (sizeof(n) == 4) ? ( \
        (((n) >> 24) & 0x000000FFU) | \
        (((n) >>  8) & 0x0000FF00U) | \
        (((n) <<  8) & 0x00FF0000U) | \
        (((n) << 24) & 0xFF000000U)   \
    ) : \
    (sizeof(n) == 8) ? ( \
        (((n) >> 56) & 0x00000000000000FFULL) | \
        (((n) >> 40) & 0x000000000000FF00ULL) | \
        (((n) >> 24) & 0x0000000000FF0000ULL) | \
        (((n) >>  8) & 0x00000000FF000000ULL) | \
        (((n) <<  8) & 0x000000FF00000000ULL) | \
        (((n) << 24) & 0x0000FF0000000000ULL) | \
        (((n) << 40) & 0x00FF000000000000ULL) | \
        (((n) << 56) & 0xFF00000000000000ULL)   \
    ) : (n)

/**
 * @brief Function to transform an 8, 16, 32, 64 bit number into machine endianess
 * @param n Number to transform
 * @return Number transformed into machine endianess
 */
#define socknet_net2host(n) socknet_host2net(n)

/**
 * @brief Type of pointer to a callback function
 * @param socket Client or server socket
 * @param ip IP of the client or server
 * @param port Client or server ports
 * @param args Pointer to additional arguments
 * @return Return code
 */
typedef int (*socknet_callback)(FILE *socket, const char *ip, int port, void *args);

/**
 * @brief Server structure
 */
typedef struct socknet_server socknet_server;

/**
 * @brief Initialize the server structure
 * @param server Server to initialize
 * @param nclient Size of the client queue
 * @param ip Server IP
 * @param port Server port
 * @return Return code
 */
socknet_server *socknet_create(size_t nclients, const char *ip, int port);

/**
 * @brief Closes the server tree
 * @param server Server to close
 */
void socknet_close(socknet_server *server);

int socknet_shared(socknet_server *server, size_t size);

void *socknet_struct(socknet_server *server);

void socknet_lock(void *shared);

void socknet_unlock(void *shared);

/**
 * @brief Makes a server accept a client by creating a new process to handle it
 * @param server Server that will have to accept the client
 * @param callback Function to communicate with the client, receives the shared structure
 * @return Return code
 */
int socknet_accept(socknet_server *server, socknet_callback callback);

/**
 * @brief Allows you to connect to a server
 * @param ip Server IP
 * @param port Server port
 * @param callback Function to communicate with the server
 * @param user Pointer with user-entered data to pass to the callback function
 * @return Return code
 */
int socknet_connect(const char *ip, int port, socknet_callback callback, void *user);

/**
 * @brief Function to recognize machine endianess
 * @return 1 if it's little endian 0 if it's big endian
 */
int socknet_islittleen(void);

#endif
