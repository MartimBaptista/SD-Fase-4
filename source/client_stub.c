/*
Trabalho realisado por: Martim Baptista Nº56323
                        Martim Paraíba Nº56273
                        Daniel Luis Nº56362
*/

#include "client_stub.h"
#include "client_stub-private.h"
#include "inet.h"

/* Função para estabelecer uma associação entre o cliente e o servidor, 
 * em que address_port é uma string no formato <hostname>:<port>.
 * Retorna NULL em caso de erro.
 */
struct rtree_t *rtree_connect(const char *address_port) {
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

/* Termina a associação entre o cliente e o servidor, fechando a 
 * ligação com o servidor e libertando toda a memória local.
 * Retorna 0 se tudo correr bem e -1 em caso de erro.
 */
int rtree_disconnect(struct rtree_t *rtree) {
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

/* Função para adicionar um elemento na árvore.
 * Se a key já existe, vai substituir essa entrada pelos novos dados.
 * Devolve 0 (ok, em adição/substituição) ou -1 (problemas).
 */
int rtree_put(struct rtree_t *rtree, struct entry_t *entry) {
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

    MessageT *answer = network_send_receive(rtree, &msg);
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
struct data_t *rtree_get(struct rtree_t *rtree, char *key) {
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

    MessageT *answer = network_send_receive(rtree, &msg);
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
int rtree_del(struct rtree_t *rtree, char *key) {
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

    MessageT *answer = network_send_receive(rtree,&msg);
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
int rtree_size(struct rtree_t *rtree) {
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_SIZE;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    // send message
    MessageT *answer = network_send_receive(rtree, &msg);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_SIZE + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->result;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);
    
    return ret;
}

/* Função que devolve a altura da árvore.
 */
int rtree_height(struct rtree_t *rtree) {
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_HEIGHT;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    // send message
    MessageT *answer = network_send_receive(rtree, &msg);

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
char **rtree_get_keys(struct rtree_t *rtree) {
    MessageT msg;
    int n_keys;
    char **ret;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_GETKEYS;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    MessageT *answer = network_send_receive(rtree, &msg);

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
void **rtree_get_values(struct rtree_t *rtree) {
    MessageT msg;

    message_t__init(&msg);

    // write codes to message
    msg.opcode = MESSAGE_T__OPCODE__OP_GETVALUES;
    msg.c_type = MESSAGE_T__C_TYPE__CT_NONE;

    MessageT *answer = network_send_receive(rtree, &msg);

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
int rtree_verify(struct rtree_t *rtree, int op_n){
    MessageT msg;
    int ret;

    message_t__init(&msg);

    // write codes and op_n to message
    msg.opcode = MESSAGE_T__OPCODE__OP_VERIFY;
    msg.c_type = MESSAGE_T__C_TYPE__CT_RESULT;
    msg.op_n = op_n;

    MessageT *answer = network_send_receive(rtree, &msg);

    if (answer->opcode == MESSAGE_T__OPCODE__OP_VERIFY + 1 && answer->c_type == MESSAGE_T__C_TYPE__CT_RESULT)
        ret = answer->result;
    else
        ret = -1;

    message_t__free_unpacked(answer, NULL);
    
    return ret;
}