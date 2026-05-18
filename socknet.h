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
#define SOCKNET_OK 1
#endif

#ifndef SOCKNET_NO
/**
 * @brief Codice di fallimento di una funzione
 */
#define SOCKNET_NO 0
#endif

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
typedef struct {
    int fd; /**< File descriptor del server */
    size_t nclients; /**< Grandezza della coda di clients */

    pid_t *pidvec; /**< Vettore di pid dei processi per gestire i client */
    size_t pidcnt; /**< Numero di porocessi aperti */
    size_t pidsiz; /**< Grandezza allocata del vettore di pid */
} socknet_server[1];

/**
 * @brief Inizializza la struttura server
 * @param server Server da inizializzare
 * @param nclient Grandezza della coda di clients
 * @param ip IP del server
 * @param port Porta del server
 * @return Codice di ritorno
 */
int socknet_create(socknet_server server, size_t nclients, const char *ip, int port);

/**
 * @brief Chiude la struttura server
 * @param server Server da chiudere
 */
void socknet_close(socknet_server server);

/**
 * @brief Fa accettare ad un server un client creando un nuovo processo per gestirlo
 * @param server Server che dovra' accettare il client
 * @param callback Funzione per comunicare con il client, riceve la struttura condivisa
 * @return Codice di ritorno
 */
int socknet_accept(socknet_server server, socknet_callback callback);

/**
 * @brief Permette di connettersi ad un server
 * @param ip IP del server
 * @param port Porta del server
 * @param callback Funzione per comunicare con il server
 * @param user Puntatore con dati inseriti dall'utente da passare alla funzione di callback
 * @return Codice di ritorno
 */
int socknet_connect(const char *ip, int port, socknet_callback callback, void *user);

#endif
