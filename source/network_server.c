/*
Trabalho realisado por: Martim Baptista Nº56323
                        Martim Paraíba Nº56273
                        Daniel Luis Nº56362
*/

#include "inet.h"
#include <sys/poll.h>
#include <errno.h>
#include <stdlib.h>
#include "sdmessage.pb-c.h"
#include "tree_skel.h"
#include "entry.h"
#include "data.h"

/* Função para preparar uma socket de receção de pedidos de ligação
 * num determinado porto.
 * Retornar descritor do socket (OK) ou -1 (erro).
 */
int network_server_init(short port){
    int listening_socket;
    struct sockaddr_in server;

    // Cria socket TCP
    if ((listening_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        perror("Erro ao criar socket");
        return -1;
    }

    // Fazer com que possa ser reutilisado
    const int enable = 1;
    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("Erro no Setsockopt(SO_REUSEADDR):");
        return -1;
    }
    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0){
        perror("Erro no Setsockopt(SO_REUSEPORT):");
        return -1;
    }

    // Preenche estrutura server com endereço(s) para associar (bind) à socket 
    server.sin_family = AF_INET;
    server.sin_port = htons(port); // Porta TCP
    server.sin_addr.s_addr = htonl(INADDR_ANY); //todo os endereços na máquina

    // Faz bind
    if (bind(listening_socket, (struct sockaddr *) &server, sizeof(server)) < 0){
        perror("Erro ao fazer bind");
        close(listening_socket);
        return -1;
    }

    // Esta chamada diz ao SO que esta é uma socket para receber pedidos
    if (listen(listening_socket, 0) < 0){
        perror("Erro ao executar listen");
        close(listening_socket);
        return -1;
    }

    return listening_socket;
}

/* Função para ler toda uma messagem num determinado porto.
 * Retornar o tamanho total lido (OK) ou <0 (erro).
 */
int read_all(int sock, void *buf, int len){
    int bufsize = len;
    while(len>0) {
        int res = read(sock, buf, len);
        if(res<0) {
            if(errno==EINTR) continue;
            perror("read failed:");
            return res;
        }
    buf += res;
    len -= res;
    }
    return bufsize;
}

/* Função para escrever toda uma messagem num determinado porto.
 * Retornar o tamanho total escrito (OK) ou <0 (erro).
 */
int write_all(int sock, void *buf, int len){
    int bufsize = len;
    while(len>0) {
        int res = write(sock, buf, len);
        if(res<0) {
            if(errno==EINTR) continue;
            perror("write failed:");
            return res;
        }
    buf += res;
    len -= res;
    }
    return bufsize;
}


/* Esta função deve:
 * - Ler os bytes da rede, a partir do client_socket indicado;
 * - De-serializar estes bytes e construir a mensagem com o pedido,
 *   reservando a memória necessária para a estrutura message_t.
 */
MessageT *network_receive(int client_socket){
    int size, size_n;

    if(read_all(client_socket, &size_n, sizeof(size_n)) < 0){
		perror("Erro ao receber tamnaho dos dados do cliente");
		close(client_socket);
    }

    size = ntohl(size_n);
    uint8_t buf [size];

    if(read_all(client_socket, buf, size) < 0){
		perror("Erro ao receber dados do cliente");
		close(client_socket);
    }

    MessageT *res; 
    res = message_t__unpack(NULL, size, buf);

    return res;
}


/* Esta função deve:
 * - Serializar a mensagem de resposta contida em msg;
 * - Libertar a memória ocupada por esta mensagem;
 * - Enviar a mensagem serializada, através do client_socket.
 */
int network_send(int client_socket, MessageT *msg){

    int size = message_t__get_packed_size(msg);
    int size_n = htonl(size);

    uint8_t *buf = malloc(size);
    message_t__pack(msg, buf);

    if(write_all(client_socket, &size_n, sizeof(int)) < 0){
        perror("Erro ao enviar tamanho da resposta ao cliente");
        close(client_socket);
    }

    if(write_all(client_socket, buf, size) < 0){
        perror("Erro ao enviar resposta ao cliente");
    	close(client_socket);
    }

    free(buf);
    message_t__free_unpacked(msg, NULL);

    return 0;
}

/* Esta função deve:
 * - Aceitar uma conexão de um cliente;
 * - Receber uma mensagem usando a função network_receive;
 * - Entregar a mensagem de-serializada ao skeleton para ser processada;
 * - Esperar a resposta do skeleton;
 * - Enviar a resposta ao cliente usando a função network_send.
 */
int network_main_loop(int listening_socket){

    nfds_t nfdesc = 6;  // numero maximo de cliente concorrentes
    int timeout = 60;

    struct sockaddr_in client;
    socklen_t size_client = sizeof(client);
    

    struct pollfd desc_set[nfdesc];  // numero maximo de cliente concorrentes
    int *busyClients = calloc( nfdesc, sizeof(int));  // posições dos clientes que estão ocupadas

    for(int i = 0; i < nfdesc; i++)
        desc_set[i].fd = -1;

    desc_set[0].fd = listening_socket; //adiciona listening_socket a desc_set
    desc_set[0].events = POLLIN; 
    desc_set[0].revents = 0;

    nfds_t nfds = 1; //numero de file descriptors

    printf("Server started, waiting for client...\n");
    
    while(poll(desc_set, nfdesc, timeout) >= 0){
        if ((desc_set[0].revents & POLLIN) && (nfds < nfdesc)) {

            int position = -1;

            for (size_t i = 0; position < 0; i++)
            {
                if (busyClients[i] == 0)
                {
                    position = i + 1;
                    busyClients[i] = 1;
                }
            }
                        

            desc_set[position].fd = accept(desc_set[0].fd,(struct sockaddr *) &client, &size_client);
            if(desc_set[position].fd > 0){
                printf("Client number: %d connected\n", position);
                desc_set[position].events = POLLIN; //wait for data
                nfds++;
            }
        }
        for(int i = 1; i < nfdesc; i++){
            if(busyClients[i - 1] == 1) {

                // printf("revents[%d]: %d\n", i, desc_set[i].revents);
                // printf("    A ler do cliente %d", i);
                // printf("|| desc_set[i].revents & POLLIN == %d", desc_set[i].revents & POLLIN);
                // puts("");

                if(desc_set[i].revents & POLLIN) {
                    // puts("  A receber mensagem");
                    MessageT *msg  = network_receive(desc_set[i].fd);

                    if(msg->opcode == MESSAGE_T__OPCODE__OP_DISCONNECT) {
                        printf("Client number: %d disconnected\n",i);
                        busyClients[i - 1] = 0;
                        message_t__free_unpacked(msg, NULL);
                        close(desc_set[i].fd);
                        desc_set[i].fd = -1;
                    } else {
                        if(invoke(msg) < 0) {
                            printf("Error on invoke\n");
                            msg->opcode = MESSAGE_T__OPCODE__OP_ERROR;
                            msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;
                        }
                    if(network_send(desc_set[i].fd, msg) == -1) {
                            close(desc_set[i].fd);
                            return -1;
                        }
                    }
                }
            }
        }

        for(int i = 1 ; i < nfdesc && nfds > 0;i++){  //allows new connections when clients disconnect
            if (busyClients[i - 1] == 0 && desc_set[i].fd == -1)
            {
                desc_set[i].fd = desc_set[nfds].fd;
                nfds--;
            }
            
        }
    }
    close(listening_socket);
    return 0;
} 

/* A função network_server_close() liberta os recursos alocados por
 * network_server_init().
 */
int network_server_close(){
    printf("\nClosing server.\n");
    return 0;
}
