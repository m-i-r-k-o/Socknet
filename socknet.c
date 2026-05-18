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
 * @brief Tempo di attesa della poll
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
    size_t nclients; /**< Grandezza della coda di clients */
    socknet_shared_header *header;
    pid_t *pidvec; /**< Vettore di pid dei processi per gestire i client */
    size_t pidcnt; /**< Numero di porocessi aperti */
    size_t pidsiz; /**< Grandezza allocata del vettore di pid */
};

/**
 * @brief Permette di fare bind ad un server inserendo un tipo di IP diretto
 * @param fd File descriptor del server
 * @param port Porta del server
 * @param type Tipo di IP del server
 * @return Codice di ritorno
 */
static int socknet_direct_bind(int fd, int port, in_addr_t type) {
    /** Creo la struttura dell'indirizzo */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** IPv4 */
    addr.sin_port = htons(port); /** Trasformo la porta nell'endianess del network */
    addr.sin_addr.s_addr = type; /** Inserisco il tipo di IP */

    /** Eseguo il bind con l'indirizzo precedentemente creato */
    if(bind(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/**
 * @brief Permette di fare bind ad un server inserendo ip e porta
 * @param fd File descriptor del server
 * @param ip IP del server
 * @param port Porta del server
 * @return Codice di ritorno
 */
static int socknet_ip_bind(int fd, const char *ip, int port) {
    /** Creo la struttura dell'indirizzo */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** IPv4 */
    addr.sin_port = htons(port); /** Trasformo la porta nell'endianess del network */

    /** Inserisco l'IP del server */
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return SOCKNET_NO;
    }

    /** Eseguo il bind con l'indirizzo precedentemente creato */
    if(bind(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/** 
 * @brief Permette di connettere un client ad un server
 * @param fd File descriptor del client
 * @param ip IP del server
 * @param port Porta del server
 * @return Codice di ritorno
 */
static int socknet_ip_connect(int fd, const char *ip, int port) {
    /** Creo la struttura dell'indirizzo */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET; /** IPv4 */
    addr.sin_port = htons(port); /** Trasformo la porta nell'endianess del network */

    /** Inserisco l'IP del server */
    if(inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        return SOCKNET_NO;
    }

    /** Connetto il client all'indirizzo del server */
    if(connect(fd, (void*)(&addr), sizeof(addr)) < 0) {
        return SOCKNET_NO;
    }

    return SOCKNET_OK;
}

/** 
 * @brief Trasforma una grandezza in una grandezza piu' granda a base 2
 * @param size Grandezza iniziale
 * @return Grandezza piu' grande a base 2
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
 * @brief Inserisce nella lista dei processi del server un pid
 * @param server Server possessore della lista
 * @param pid Codice del processo da inserire nella lista
 * @return Codice di ritorno
 */
static int socknet_put_pid(socknet_server *server, pid_t pid) {
    /** Nel caso la lista sia piena riallochiamola */
    if(server->pidcnt >= server->pidsiz) {
        /** Ingrandisco la grandezza massima */
        size_t newsiz = socknet_round_size(server->pidsiz);

        /** Creo una nuova lista */
        pid_t *newvec = SOCKNET_MALLOC(newsiz * sizeof(pid_t));
        if(!newvec) return SOCKNET_NO;

        /** Inizializzo a 0 la lista */
        memset(newvec, 0, newsiz * sizeof(pid_t));

        /** Se c'erano dati precendenti li copio nella nuova lista */
        if(server->pidvec) {
            memcpy(newvec, server->pidvec, server->pidcnt);
            SOCKNET_FREE(server->pidvec);
        }

        /** Aggiorno la lista e la grandezza */
        server->pidvec = newvec;
        server->pidsiz = newsiz;
    }

    /** Cerco un pid vuoto o un processo finito e gli inserisco il pid nuovo */
    for(size_t n = 0; n < server->pidcnt; n++) {
        int curr = server->pidvec[n];
        if(curr <= 0 || waitpid(curr, NULL, WNOHANG)) {
            server->pidvec[n] = pid;
            return SOCKNET_OK;
        }
    }

    /** Se non ho trovato un pid vuoto ingrandisco la lista */
    server->pidvec[server->pidcnt++] = pid;
    return SOCKNET_OK;
}

socknet_server *socknet_create(size_t nclients, const char *ip, int port) {
    socknet_server *server = SOCKNET_MALLOC(sizeof(socknet_server));
    if(!server) return NULL;

    /** Creo un nuovo socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        SOCKNET_FREE(server);
        return NULL;
    }

    /** Se non e' specificato un IP accetto ogni indirizzo */
    int res = SOCKNET_OK;
    if(!ip) res = socknet_direct_bind(fd, port, INADDR_ANY);
    else res = socknet_ip_bind(fd, ip, port);

    /** In caso di errore chiudo il socket e mando un codice negativo */
    if(res == SOCKNET_NO) {
        SOCKNET_FREE(server);
        close(fd);
        return NULL;
    }

    /** Metto il server in ascolto */
    if(listen(fd, (int)(nclients)) < 0) {
        SOCKNET_FREE(server);
        close(fd);
        return NULL;
    }

    /** Inizializzo i dati del server */
    server->fd = fd;
    server->nclients = nclients;
    server->header = NULL;
    server->pidvec = NULL;
    server->pidcnt = 0;
    server->pidsiz = 0;

    return server;
}

void socknet_close(socknet_server *server) {
    /** Chiudo il socket */
    close(server->fd);

    if(server->header) {
        size_t total = (
            sizeof(socknet_shared_header) +
            server->header->size
        );
        munmap(server->header, total);
    }

    /** Se ci sono processi aperti li chiudo */
    if(server->pidvec) {
        for(size_t n = 0; n < server->pidcnt; n++) {
            /** Se il processo non e' valido saltalo */
            if(server->pidvec[n] <= 0) continue;
            
            /** Chiedo al processo di chiudersi e lo aspetto */
            kill(server->pidvec[n], SIGTERM);
            waitpid(server->pidvec[n], NULL, 0);
        }
        
        /** Libero la memoria del vettore di processi */
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
    /** Creo una poll per gestire il tempo di block di accept */
    struct pollfd pfd;
    pfd.fd = server->fd;
    pfd.events = POLLIN;

    /** Controllo se un client vuole connettersi */
    int res = poll(&pfd, 1, 100);
    if(res == 0) return SOCKNET_OK;

    /** Errore della poll */
    if(res < 0) {
        if(errno == EINTR) return SOCKNET_OK;
        return SOCKNET_NO;
    }

    /** Creo la struttura di indirizzo per il client */
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    /** Accetto il client */
    int fd = accept(server->fd, (void*)(&addr), &len);
    if(fd < 0) return SOCKNET_NO;

    /** Eseguo un nuovo processo per gestire il client */
    pid_t pid = fork();

    /** Errore del fork */
    if(pid < 0) {
        close(fd);
        return SOCKNET_NO;
    }

    /** Se e' il processo figlio */
    else if(pid == 0) {
        /** Trasformo l'ip in una stringa leggibile */
        char ip[INET_ADDRSTRLEN];
        if(!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        /** Trasformo il socket da file descriptor a file per comodita' */
        FILE *client = fdopen(fd, "a+");
        if(!client) {
            close(fd);
            _exit(EXIT_FAILURE);
        }

        /** Rimuovo il buffering del file per evitare problemi con l'invio dei dati */
        if(setvbuf(client, NULL, _IONBF, 0) != 0) {
            fclose(client);
            _exit(EXIT_FAILURE);
        }

        /** Eseguo le operazioni di comunicazione con il client */
        int res = callback(client, ip, (int)(ntohs(addr.sin_port)), server->header->shared);
        fclose(client); /** Chiudo il socket */

        /** Chiudo il processo con uno stato positivo o negativo in base all'operazione */
        int status = EXIT_SUCCESS;
        if(res == SOCKNET_NO) status = EXIT_FAILURE;
        _exit(status);
    }

    /** Se e' il processo padre */
    else {
        /** Aggiungi il pid del processo figlio nella lista */
        int res = socknet_put_pid(server, pid);
        close(fd); /** Chiudi il socket */
        return res; /** Ritorna il codice di errore */
    }

    return SOCKNET_OK;
}

int socknet_connect(const char *ip, int port, socknet_callback callback, void *user) {
    /** Creo il socket del server */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) return SOCKNET_NO;

    /** Connetto il socket all'ip e alla porta */
    int res = socknet_ip_connect(fd, ip, port);
    if(res == SOCKNET_NO) {
        close(fd);
        return SOCKNET_NO;
    }

    /** Trasformo di file descriptor in un file per comodita' */
    FILE *server = fdopen(fd, "a+");
    if(!server) {
        close(fd);
        return SOCKNET_NO;
    }

    /** Rimuovo il buffering del file per evitare problemi con l'invio dei dati */
    if(setvbuf(server, NULL, _IONBF, 0) != 0) {
        fclose(server);
        return SOCKNET_NO;
    }

    /** Comunico con il server */
    int err = callback(server, ip, port, user);

    /** Chiudo il socket e ritorno il codice di errore */
    fclose(server);
    return err;
}
