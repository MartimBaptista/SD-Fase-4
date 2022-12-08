/*
Trabalho realisado por: Martim Baptista Nº56323
                        Martim Paraíba Nº56273
                        Daniel Luis Nº56362
*/

#include "network_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

void closing_handler(int unused){
    network_server_close();
    tree_skel_destroy();
    exit(0);
}


int main(int argc, char *argv[]){

    if (argc != 3){
        printf("Uso: %s <listening_port> <host:port>\n", argv[0]);
        printf("Exemplo de uso: %s 12345 localhost:2181\n", argv[0]);
        return -1;
    }

    int listening_socket_fd = network_server_init(atoi(argv[1]));

    if(tree_skel_init(argv[2], argv[1]) < 0){
        printf("Error creating the tree or connecting to zookeeper\n");
        closing_handler(-1);
        return -1;
    }

    signal(SIGINT, closing_handler);

    network_main_loop(listening_socket_fd);

    return 0;
}