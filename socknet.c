#include "socknet.h"

#include <stddef.h>
#include <errno.h>
#include <string.h>

#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>

/**
 * @brief Poll waiting time
 */
#define SOCKNET_POLL_WAIT 100

#define socknet_containerof(ptr,type,member) ((type*)((uint8_t*)(ptr) - offsetof(type, member)))

typedef struct {
    pthread_mutex_t lock;
    size_t size;
    uint8_t shared[];
} socknet_shared_header;

struct socknet_server {
    int fd; /**< File descriptor del server */
    size_t nclients; /**< Size of the client queue */
    socknet_shared_header *header;
    pid_t *pidvec; /**< Process pid vector to manage clients */
    size_t pidcnt; /**< Number of open processes */
    size_t pidsiz; /**< Allocated quantity of the pid vector */
};

/**
 * @brief Unique socket for each child process (client)
 */
static FILE *child_client_socket = NULL;

/**
 * @brief Function to terminate the client when the server closes
 * @param sig Signal number which should be SIGTERM
 */
static void socknet_sigterm_client(int sig) {
    (void)(sig);

    /** If the client socket is valid send the last data and close it */
    if(child_client_socket) {
        shutdown(fileno(child_client_socket), SHUT_WR);
        fclose(child_client_socket);
    }

    /** I'm leaving the trial */
    _exit(EXIT_SUCCESS);
}

/**
 * @brief Allows you to bind to a server by entering a direct IP type
 * @param fd Server descriptor file
 * @param port Server port
 * @param type Server IP type
 * @return Return code
 */
static int socknet_direct_bind(int fd, int port, in_addr_t type) {
    /** I create the address structure */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** I pv4 */
    addr.sin_port = htons(port); /** I transform the port into the endianess of the network */
    addr.sin_addr.s_addr = type; /** I enter the IP type */

    /** I bind with the previously created address */
    if(bind(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/**
 * @brief Allows you to bind to a server by entering IP and port
 * @param fd Server descriptor file
 * @param ip Server IP
 * @param port Server port
 * @return Return code
 */
static int socknet_ip_bind(int fd, const char *ip, int port) {
    /** I create the address structure */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** I pv4 */
    addr.sin_port = htons(port); /** I transform the port into the endianess of the network */

    /** I enter the server IP */
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return SOCKNET_NO;
    }

    /** I bind with the previously created address */
    if(bind(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/** 
 * @brief Allows you to connect a client to a server
 * @param fd Client file descriptor
 * @param ip Server IP
 * @param port Server port
 * @return Return code
 */
static int socknet_ip_connect(int fd, const char *ip, int port) {
    /** I create the address structure */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** I pv4 */
    addr.sin_port = htons(port); /** I transform the port into the endianess of the network */

    /** I enter the server IP */
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return SOCKNET_NO;
    }

    /** I connect the client to the server address */
    if(connect(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/** 
 * @brief Transform a quantity into a larger quantity with base 2
 * @param size Initial size
 * @return Largest quantity at base 2
 */
static size_t socknet_round_size(size_t size) {
    if(size == 0) return 1;
    size--;

    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;

    return size + 1;
}

/**
 * @brief Inserts a pid into the server process list
 * @param server Server owner of the list
 * @param pid Process code to insert into the list
 * @return Return code
 */
static int socknet_put_pid(socknet_server *server, pid_t pid) {
    /** If the list is full, let's reallocate it */
    if(server->pidcnt >= server->pidsiz) {
        /** I enlarge the maximum size */
        size_t newsiz = socknet_round_size(server->pidsiz);

        /** I create a new list */
        pid_t *newvec = SOCKNET_MALLOC(newsiz * sizeof(pid_t));
        if(!newvec) return SOCKNET_NO;

        /** I initialize the list to 0 */
        memset(newvec, 0, newsiz * sizeof(pid_t));

        /** If there were previous data I copy them into the new list */
        if(server->pidvec) {
            memcpy(newvec, server->pidvec, server->pidcnt);
            SOCKNET_FREE(server->pidvec);
        }

        /** I update the list and size */
        server->pidvec = newvec;
        server->pidsiz = newsiz;
    }

    /** I look for an empty pid or a finished process and insert the new pid into it */
    for(size_t n = 0; n < server->pidcnt; n++) {
        int curr = server->pidvec[n];
        if(curr <= 0 || waitpid(curr, NULL, WNOHANG)) {
            server->pidvec[n] = pid;
            return SOCKNET_OK;
        }
    }

    /** If I haven't found an empty pid I enlarge the list */
    server->pidvec[server->pidcnt++] = pid;
    return SOCKNET_OK;
}

socknet_server *socknet_create(size_t nclients, const char *ip, int port) {
    socknet_server *server = SOCKNET_MALLOC(sizeof(socknet_server));
    if(!server) return NULL;

    /** I create a new socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        SOCKNET_FREE(server);
        return NULL;
    }

    /** If an IP is not specified I accept any address */
    int res = SOCKNET_OK;
    if(!ip) res = socknet_direct_bind(fd, port, INADDR_ANY);
    else res = socknet_ip_bind(fd, ip, port);

    /** In case of error I close the socket and send a negative code */
    if(res == SOCKNET_NO) {
        SOCKNET_FREE(server);
        close(fd);
        return NULL;
    }

    /** I put the server to listen */
    if(listen(fd, (int)(nclients)) < 0) {
        SOCKNET_FREE(server);
        close(fd);
        return NULL;
    }

    /** I initialize the server data */
    server->fd = fd;
    server->nclients = nclients;
    server->header = NULL;
    server->pidvec = NULL;
    server->pidcnt = 0;
    server->pidsiz = 0;

    return server;
}

void socknet_close(socknet_server *server) {
    /** I close the socket */
    close(server->fd);

    if(server->header) {
        size_t total = (
            sizeof(socknet_shared_header) +
            server->header->size
        );
        munmap(server->header, total);
    }

    /** If there are open processes I close them */
    if(server->pidvec) {
        for(size_t n = 0; n < server->pidcnt; n++) {
            /** If the process is invalid, skip it */
            if(server->pidvec[n] <= 0) continue;

            /** I ask the process to close and wait for it */
            kill(server->pidvec[n], SIGTERM);
            waitpid(server->pidvec[n], NULL, 0);
        }
        
        /** Free the memory of the process vector */
        SOCKNET_FREE(server->pidvec);
    }

    SOCKNET_FREE(server);
}

int socknet_shared(socknet_server *server, size_t size) {
    size_t total = sizeof(socknet_shared_header) + size;

    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_ANONYMOUS | MAP_SHARED;

    void *map = mmap(NULL, total, prot, flags, -1, 0);
    if(map == MAP_FAILED) return SOCKNET_NO;

    socknet_shared_header *header = map;
    header->size = size;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    
    if (pthread_mutex_init(&header->lock, &attr) != 0) {
        munmap(map, total);
        return SOCKNET_NO;
    }
    pthread_mutexattr_destroy(&attr);

    if(server->header) {
        total = (
            sizeof(socknet_shared_header) +
            server->header->size
        );
        munmap(server->header, total);
    }

    server->header = header;
    return SOCKNET_OK;
}

void *socknet_struct(socknet_server *server) {
    return server->header->shared;
}

void socknet_lock(void *shared) {
    socknet_shared_header *header = NULL;
    header = socknet_containerof(shared, socknet_shared_header, shared);
    pthread_mutex_lock(&header->lock);
}

void socknet_unlock(void *shared) {
    socknet_shared_header *header = NULL;
    header = socknet_containerof(shared, socknet_shared_header, shared);
    pthread_mutex_unlock(&header->lock);
}

int socknet_accept(socknet_server *server, socknet_callback callback) {
    /** I create a poll to manage the accept block time */
    struct pollfd pfd;
    pfd.fd = server->fd;
    pfd.events = POLLIN;

    /** Check if a client wants to connect */
    int res = poll(&pfd, 1, 100);
    if(res == 0) return SOCKNET_OK;

    /** Poll error */
    if(res < 0) {
        if(errno == EINTR) return SOCKNET_OK;
        return SOCKNET_NO;
    }

    /** I create the address structure for the client */
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    /** I accept the client */
    int fd = accept(server->fd, (void*)(&addr), &len);
    if(fd < 0) return SOCKNET_NO;

    /** I run a new process to handle the client */
    pid_t pid = fork();

    /** Fork error */
    if(pid < 0) {
        close(fd);
        return SOCKNET_NO;
    }

    /** If it is the child process */
    else if(pid == 0) {
        /** I transform the ip into a readable string */
        char ip[INET_ADDRSTRLEN];
        if(!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        /** I transform the socket from file descriptor to file for convenience */
        FILE *client = fdopen(fd, "a+");
        if(!client) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        /** I remove file buffering to avoid problems with sending data */
        if(setvbuf(client, NULL, _IONBF, 0) != 0) {
            fclose(client);
            _exit(EXIT_FAILURE);
        }

        /** I save the socket in the global process variable */
        child_client_socket = client;

        /** I insert that the client's socknet ends when the server is closed */
        struct sigaction sa;
        sa.sa_handler = socknet_sigterm_client;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, NULL);

        /** I perform communication operations with the client */
        int res = callback(client, ip, (int)(ntohs(addr.sin_port)), server->header->shared);

        /** Sending the latest socket data */
        shutdown(fileno(client), SHUT_WR);
        fclose(client); /** I close the socket */

        /** I close the process with a positive or negative status depending on the operation */
        int status = EXIT_SUCCESS;
        if(res == SOCKNET_NO) status = EXIT_FAILURE;
        _exit(status);
    }

    /** If it is the parent process */
    else {
        /** Add the pid of the child process into the list */
        int res = socknet_put_pid(server, pid);
        close(fd); /** Close the socket */
        return res; /** The error code returns */
    }

    return SOCKNET_OK;
}

int socknet_connect(const char *ip, int port, socknet_callback callback, void *user) {
    /** I create the server socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return SOCKNET_NO;

    /** I connect the socket to the IP and the port */
    int res = socknet_ip_connect(fd, ip, port);
    if(res == SOCKNET_NO) {
        close(fd);
        return SOCKNET_NO;
    }

    /** Transforming file descriptor into a file for convenience */
    FILE *server = fdopen(fd, "a+");
    if(!server) {
        close(fd);
        return SOCKNET_NO;
    }

    /** I remove file buffering to avoid problems with sending data */
    if(setvbuf(server, NULL, _IONBF, 0) != 0) {
        fclose(server);
        return SOCKNET_NO;
    }

    /** I communicate with the server */
    int err = callback(server, ip, port, user);

    /** I close the socket and return the error code */
    fclose(server);
    return err;
}

int socknet_islittleen(void) {
    uint16_t x = 1;
    return *(uint8_t*)&x == 1;
}
