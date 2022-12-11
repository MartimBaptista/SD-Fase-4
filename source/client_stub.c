/*
Trabalho realisado por: Martim Baptista Nº56323
*/

#include "client_stub.h"
#include "client_stub-private.h"
#include "inet.h"
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zookeeper/zookeeper.h>

/* ZooKeeper Znode Data Length (1MB, the max supported) */
#define ZDATALEN 1024 * 1024

typedef struct String_vector zoo_string;

static char *root_path = "/chain";
static zhandle_t *zh;
char current_head_path[50];
char current_tail_path[50];
struct rtree_ht_t *rtree_ht;


struct rtree_t *connect_to_server(const char *address_port) {
    struct rtree_t *rtree = malloc(sizeof(struct rtree_t));
    rtree->server = malloc(sizeof(struct sockaddr_in));
    char* ip = strtok((char*)address_port, ":");
    int port = atoi(strtok(NULL, ":"));

    rtree->server->sin_family = AF_INET; // família de endereços
    if (inet_pton(AF_INET, ip, &rtree->server->sin_addr) < 1) { // Endereço IP
        printf("Erro ao converter IP\n");
        return NULL;
    }
    rtree->server->sin_port = htons(port); // Porta TCP
    
    if(network_connect(rtree) != 0){
        free(rtree->server);
        free(rtree);
        return NULL;
    }
    return rtree;
}

int disconnect_from_server(struct rtree_t* rtree) {
    MessageT msg;

    //Create msg and signaling a disconnect
    message_t__init(&msg);
    msg.opcode = MESSAGE_T__OPCODE__OP_DISCONNECT;
    network_send_receive(rtree, &msg);

    int ret = network_close(rtree);
    free(rtree->server);
    free(rtree);
    return ret;
}

void setup_head_tail(zoo_string *children_list){
    printf("Setting head and tail.\n");

    if(children_list->count == 0){
        printf("No servers present in chain, shutingdown.\n");
        exit(-1);
    }

    //Finding head and tail
    char* head = children_list->data[0];
    char* tail = children_list->data[0];
    int prev_head_still_present = 0;
    int prev_tail_still_present = 0;

    for (int i = 0; i < children_list->count; i++) {
        //Checking for the head
        if(strcmp(children_list->data[i], head) < 0)
            head = children_list->data[i];

        //Checking for the tail
        if(strcmp(children_list->data[i], tail) > 0)
            tail = children_list->data[i];

        //Checking if last_head still present
        if(strcmp(children_list->data[i], current_head_path) == 0)
            prev_head_still_present = 1;
        
        //Checking if last_tail still present
        if(strcmp(children_list->data[i], current_tail_path) == 0)
            prev_tail_still_present = 1;
	}
    
    //Updating the head if needed
    if(strcmp(head, current_head_path) == 0){
        printf("Head has not changed.\n");
    }
    else{
        //Disconnecting from previous if its still presente
        if(prev_head_still_present){
            disconnect_from_server(rtree_ht->head);
        }
        if(rtree_ht->head){
            free(rtree_ht->head->server);
            free(rtree_ht->head);
            rtree_ht->head = NULL;
        }

        //Setting new head
        strcpy(current_head_path, head);
        printf("New head: %s\n", current_head_path);

        //building heads's full node_path
        char node_path[100] = "";
	    strcat(node_path,root_path);
	    strcat(node_path,"/");
	    strcat(node_path,head);

        //getting the heads adress (node data)   
        char *zoo_data = malloc(ZDATALEN * sizeof(char));
	    int zoo_data_len = ZDATALEN;

        zoo_get(zh, node_path, 0, zoo_data, &zoo_data_len, NULL);

        printf("IP adress of head: %s\n", zoo_data);

        //Connecting to head
        if((rtree_ht->head = connect_to_server(zoo_data)) == NULL){
            perror("Error connecting head.");
            exit(EXIT_FAILURE);
        }
        printf("Connectiong to: %s established\n", zoo_data);

    }

    //Updating the tail if needed
    if(strcmp(tail, current_tail_path) == 0){
        printf("Tail has not changed.\n");
    }
    else{
        //Disconnecting from previous if its still presente
        if(prev_tail_still_present){
            disconnect_from_server(rtree_ht->tail);
        }
        if(rtree_ht->tail){
            free(rtree_ht->tail->server);
            free(rtree_ht->tail);
            rtree_ht->tail = NULL;
        }

        //Setting new tail
        strcpy(current_tail_path, tail);
        printf("New tail: %s\n", current_tail_path);

        //building tail's full node_path
        char node_path[100] = "";
	    strcat(node_path,root_path);
	    strcat(node_path,"/");
	    strcat(node_path,tail);

        //getting the tails adress (node data)   
        char *zoo_data = malloc(ZDATALEN * sizeof(char));
	    int zoo_data_len = ZDATALEN;

        zoo_get(zh, node_path, 0, zoo_data, &zoo_data_len, NULL);

        printf("IP adress of tail: %s\n", zoo_data);

        //Connecting to tail
        if((rtree_ht->tail = connect_to_server(zoo_data)) == NULL){
            perror("Error connecting tail.");
            exit(EXIT_FAILURE);
        }
        printf("Connectiong to: %s established\n", zoo_data);

    }
}

void children_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context){
    printf("----------------------//----------------------\n");
    printf("Changes in chain detected.\n");

    //Resetting watcher and getting the a list with the children
    zoo_string* children_list =	(zoo_string *) malloc(sizeof(zoo_string));
    zoo_wget_children(zh, root_path, children_watcher, context, children_list);

    //Resetting connection to next server in chain
    setup_head_tail(children_list);
    free(children_list);
}

/* Função para estabelecer uma associação entre o cliente e o servidor, 
 * em que address_port é uma string no formato <hostname>:<port>.
 * Retorna NULL em caso de erro.
 */
void rtree_connect(const char *address_port) {
    //Conecting to zookeeper
    zh = zookeeper_init(address_port, NULL, 2000, 0, 0, 0);
    if (zh == NULL) {
	    fprintf(stderr, "Error connecting to zookeeper server!\n");
	    exit(EXIT_FAILURE);
    }

	sleep(1); /* Sleep a little for connection to complete */

    //Checking for /chain
    if(zoo_exists(zh, root_path, 0, NULL) != 0) {
        printf("Path \"/chain\" not present, shuting down.\n");
        exit(-1);
    }

    //reserving needed space for global vars
    rtree_ht = malloc(sizeof(struct rtree_ht_t));

    //Setting watcher over /chain's children and getting a list with them
    zoo_string* children_list =	(zoo_string *) malloc(sizeof(zoo_string));
    zoo_wget_children(zh, root_path, children_watcher, NULL, children_list);

    //Connecting to head and tail
    setup_head_tail(children_list);
    free(children_list);
}

/* Termina a associação entre o cliente e o servidor, fechando a 
 * ligação com o servidor e libertando toda a memória local.
 * Retorna 0 se tudo correr bem e -1 em caso de erro.
 */
void rtree_disconnect() {
    disconnect_from_server(rtree_ht->head);
    disconnect_from_server(rtree_ht->tail);
    free(rtree_ht);
}

/* Função para adicionar um elemento na árvore.
 * Se a key já existe, vai substituir essa entrada pelos novos dados.
 * Devolve 0 (ok, em adição/substituição) ou -1 (problemas).
 */
int rtree_put(struct entry_t *entry) {
    MessageT msg;
    MessageT__Entry msg_entry;
    int ret;

    //Create msg
    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_PUT;
    msg.c_type = MESSAGE_T__C_TYPE__CT_ENTRY;

    //Create entry
    message_t__entry__init(&msg_entry);

    //write entrys key
    msg_entry.key = malloc(strlen(entry->key) + 1);
    stpcpy(msg_entry.key, entry->key);

    //write entry data
    msg_entry.data.len = entry->value->datasize;
    msg_entry.data.data = malloc(entry->value->datasize);
    memcpy(msg_entry.data.data, entry->value->data, entry->value->datasize);

    //Put entry on msg
    msg.entry = &msg_entry;

    MessageT *answer = network_send_receive(rtree_ht->head, &msg);
    free(msg_entry.key);
    free(msg_entry.data.data);
    
    if (answer->opcode == MESSAGE_T__OPCODE__OP_PUT + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->op_n;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);

    return ret; 
}

/* Função para obter um elemento da árvore.
 * Em caso de erro, devolve NULL.
 */
struct data_t *rtree_get(char *key) {
    MessageT msg;
    MessageT__Entry msg_entry;
    struct data_t *ret;

    //Create msg
    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_GET;
    msg.c_type = MESSAGE_T__C_TYPE__CT_KEY;

    //Create entry
    message_t__entry__init(&msg_entry);

    //write entrys key
    msg_entry.key = malloc(strlen(key) + 1);
    stpcpy(msg_entry.key, key);

    //Put entry on msg
    msg.entry = &msg_entry;

    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);
    free(msg_entry.key);    

    if (answer->opcode == MESSAGE_T__OPCODE__OP_GET + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_VALUE){
        //In case there is an entry with that key
        if(answer->c_type == MESSAGE_T__C_TYPE__CT_VALUE){
            ret = data_create(answer->entry->data.len);
            memcpy(ret->data, answer->entry->data.data, ret->datasize);     
        }
        //In case there isn't an entry with that key
        else if(answer->c_type == MESSAGE_T__C_TYPE__CT_NONE)
            ret = NULL;
    }
    else
        ret = NULL;

    message_t__free_unpacked(answer, NULL);

    return ret; 
}

/* Função para remover um elemento da árvore. Vai libertar 
 * toda a memoria alocada na respetiva operação rtree_put().
 * Devolve: 0 (ok), -1 (key not found ou problemas).
 */
int rtree_del(char *key) {
    MessageT msg;
    MessageT__Entry msg_entry;
    int ret;

    //Create msg
    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_DEL;
    msg.c_type = MESSAGE_T__C_TYPE__CT_KEY;

    //Create entry
    message_t__entry__init(&msg_entry);

    //write entrys key
    msg_entry.key = malloc(strlen(key) + 1);
    stpcpy(msg_entry.key, key);

    //Put entry on msg
    msg.entry = &msg_entry;

    MessageT *answer = network_send_receive(rtree_ht->head, &msg);
    free(msg_entry.key);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_DEL + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->op_n;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);
    
    return ret; 
}

/* Devolve o número de elementos contidos na árvore.
 */
int rtree_size() {
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_SIZE;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    // send message
    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_SIZE + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->result;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);
    
    return ret;
}

/* Função que devolve a altura da árvore.
 */
int rtree_height() {
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_HEIGHT;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    // send message
    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_HEIGHT + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->result;
    else
        ret = -1;
    
    message_t__free_unpacked(answer, NULL);

    return ret; 
}

/* Devolve um array de char* com a cópia de todas as keys da árvore,
 * colocando um último elemento a NULL.
 */
char **rtree_get_keys() {
    MessageT msg;
    int n_keys;
    char **ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_GETKEYS;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);

    if (answer->opcode != MESSAGE_T__OPCODE__OP_GETKEYS + 1 || answer->c_type != MESSAGE_T__C_TYPE__CT_KEYS){
        return NULL;
    }
    
    n_keys = answer->n_keys;

    ret = calloc(n_keys + 1, sizeof(char *));

    for (size_t i = 0; i < n_keys; i++) {
        ret[i] = malloc(strlen(answer->keys[i]) + 1);
        strcpy(ret[i], answer->keys[i]);
    }

    message_t__free_unpacked(answer, NULL);

    return ret;    
}

/* Devolve um array de void* com a cópia de todas os values da árvore,
 * colocando um último elemento a NULL.
 */
void **rtree_get_values() {
    MessageT msg;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_GETVALUES;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);

    if (answer->opcode != MESSAGE_T__OPCODE__OP_GETVALUES + 1 || answer->c_type != MESSAGE_T__C_TYPE__CT_VALUES){
        return NULL;
    }
    
    int n_values = answer->n_values;

    struct data_t **ret = malloc(sizeof(struct data_t *) * (n_values + 1)); // + 1 to account for NULL terminator

    for (size_t i = 0; i < n_values; i++) { 
        ret[i] = malloc(sizeof(struct data_t));
        ret[i]->datasize = answer->values[i].len;
        ret[i]->data = malloc(ret[i]->datasize);
        memcpy(ret[i]->data, answer->values[i].data, ret[i]->datasize);
    }

    ret[n_values] = NULL;

    message_t__free_unpacked(answer, NULL);

    return (void**)ret;
}

/* Verifica se a operação identificada por op_n foi executada. 
 */ 
int rtree_verify(int op_n){
    //TODO:
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes and op_n to message
    msg.opcode = MESSAGE_T__OPCODE__OP_VERIFY;
    msg.c_type = MESSAGE_T__C_TYPE__CT_RESULT;
    msg.op_n = op_n;

    MessageT *answer = network_send_receive(rtree_ht->tail, &msg);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_VERIFY + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->result;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);
    
    return ret;
}