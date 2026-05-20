/**
 * @file socknet.h
 * @brief Libreria per gestione socket client-server
 * @author Vitolo Mirko
 * @date 2026-05-11
 */

#ifndef SOCKNET_H
#define SOCKNET_H

#include <stdlib.h>
#include <stdio.h>

#ifndef SOCKNET_MALLOC
/**
 * @brief Macro personalizzabile per allocare memoria
 * @param siz Grandezza in byte da allocare
 * @return Puntatore a memoria allocata
 */
#define SOCKNET_MALLOC(siz) malloc(siz)
#endif

#ifndef SOCKNET_FREE
/**
 * @brief Macro per liberare memoria allocata
 * @param ptr Puntatore della memoria da deallocare
 */
#define SOCKNET_FREE(ptr) free(ptr)
#endif

#ifndef SOCKNET_OK
/**
 * @brief Codice di successo di una funzione
 */
#define SOCKNET_OK 0
#endif

#ifndef SOCKNET_NO
/**
 * @brief Codice di fallimento di una funzione
 */
#define SOCKNET_NO 1
#endif

/**
 * @brief Trasforma un numero di 8, 16, 32, 64 bit in big endian
 * @param n Numero da trasfromare
 * @return Numero trasformato in big endian
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
 * @brief Funzione per trasformare un numero di 8, 16, 32, 64 bit nell'endianess della macchina
 * @param n Numero da trasformare
 * @return Numero trasformato nell'endianess della macchina
 */
#define socknet_net2host(n) socknet_host2net(n)

/**
 * @brief Tipo di puntatore a una funzione di callback
 * @param socket Socket del client o server
 * @param ip IP del client o server
 * @param port Porte del client o server
 * @param args Puntatore agli argomenti aggiuntivi
 * @return Codice di ritorno
 */
typedef int (*socknet_callback)(FILE *socket, const char *ip, int port, void *args);

/**
 * @brief Struttura del server
 */
typedef struct socknet_server socknet_server;

/**
 * @brief Inizializza la struttura server
 * @param server Server da inizializzare
 * @param nclient Grandezza della coda di clients
 * @param ip IP del server
 * @param port Porta del server
 * @return Codice di ritorno
 */
socknet_server *socknet_create(size_t nclients, const char *ip, int port);

/**
 * @brief Chiude la struttura server
 * @param server Server da chiudere
 */
void socknet_close(socknet_server *server);

int socknet_shared(socknet_server *server, size_t size);

void *socknet_struct(socknet_server *server);

void socknet_lock(void *shared);

void socknet_unlock(void *shared);

/**
 * @brief Fa accettare ad un server un client creando un nuovo processo per gestirlo
 * @param server Server che dovra' accettare il client
 * @param callback Funzione per comunicare con il client, riceve la struttura condivisa
 * @return Codice di ritorno
 */
int socknet_accept(socknet_server *server, socknet_callback callback);

/**
 * @brief Permette di connettersi ad un server
 * @param ip IP del server
 * @param port Porta del server
 * @param callback Funzione per comunicare con il server
 * @param user Puntatore con dati inseriti dall'utente da passare alla funzione di callback
 * @return Codice di ritorno
 */
int socknet_connect(const char *ip, int port, socknet_callback callback, void *user);

/**
 * @brief Funzione per riconoscere l'endianess della macchina
 * @return 1 se e' little endian 0 se e' big endian
 */
int socknet_islittleen(void);

#endif
