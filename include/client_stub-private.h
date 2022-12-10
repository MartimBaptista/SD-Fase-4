#include "network_client.h"
#include "tree.h"
#include "data.h"
#include "sdmessage.pb-c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

struct rtree_t{
    struct sockaddr_in *server;
    int client_sockfd;
};

struct rtree_ht_t{
    struct rtree_t *head;
    struct rtree_t *tail;
};