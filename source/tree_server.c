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
        printf("Uso: ./tree_server <porto_servidor> <N>\n");
        printf("Exemplo de uso: ./tree_server 12345 3\n");
        return -1;
    }

    int listening_socket_port = atoi(argv[1]);

    int listening_socket_fd = network_server_init(listening_socket_port);

    if(tree_skel_init(atoi(argv[2])) < 0){
        printf("Error creating the tree or initiating the secondary threads\n");
        closing_handler(-1);
        return -1;
    }

    signal(SIGINT, closing_handler);

    network_main_loop(listening_socket_fd);

    return 0;
}