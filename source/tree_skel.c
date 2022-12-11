/*
Trabalho realisado por: Martim Baptista Nº56323
*/

#include "sdmessage.pb-c.h"
#include "tree.h"
#include "tree_skel.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zookeeper/zookeeper.h>
#include "tree_skel-private.h"



/* ZooKeeper Znode Data Length (1MB, the max supported) */
#define ZDATALEN 1024 * 1024

typedef struct String_vector zoo_string;

static char *root_path = "/chain";
static zhandle_t *zh;
char* own_path;
char next_server_path[50];
struct rtree_t *next_server;


struct request_t { 
    int op_n;               //o número da operação 
    int op;                 //a operação a executar. op=0 se for um delete, op=1 se for um put 
    char* key;              //a chave a remover ou adicionar 
    struct data_t* data;    //os dados a adicionar em caso de put, ou NULL em caso de delete 
    struct request_t* next; //o proximo request (linked list)
    //adicionar campo(s) necessário(s) para implementar fila do tipo produtor/consumidor 
};

struct op_proc_t {
    int max_proc;
    int *in_progress;
};

struct tree_t *tree;


int last_assigned;
struct request_t *queue_head;

struct op_proc_t op_proc;

pthread_mutex_t queue_lock;
pthread_mutex_t tree_lock;
pthread_mutex_t op_proc_lock;
pthread_cond_t queue_not_empty;

pthread_t* threads;
size_t threads_amount = 0;

int CLOSE_PROGRAM = 0;

/* Função para estabelecer uma associação entre o cliente(servidor neste caso) e o servidor, 
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
    
    if(s2s_network_connect(rtree) != 0){
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
    s2s_network_send_receive(rtree, &msg);

    int ret = s2s_network_close(rtree);
    free(rtree->server);
    free(rtree);
    rtree = NULL;
    return ret;
}

//TODO: This function ended up a mess (fix later)!!!
void setup_next(zoo_string *children_list){
    printf("Setting next server in chain.\n");

    //Figuring which server is next in the chain
    char* own_name = &own_path[strlen(root_path) + 1];
    char* next_name = NULL;
    int prev_still_present = 0;

    for (int i = 0; i < children_list->count; i++) {
        //Checking for the next_path
        if(strcmp(children_list->data[i], own_name) > 0) {
            if(!next_name)
                next_name = children_list->data[i];
            else if(strcmp(children_list->data[i], next_name) < 0)
                next_name = children_list->data[i];
        }
        //Checking if the next_server is still present
        if(strcmp(children_list->data[i], next_server_path) == 0)
            prev_still_present = 1;
	}


    //Checking if the next server has changed
    if(prev_still_present) {
        printf("Next node in chain remains the same, no actions needed.\n");
        return;
    }

    //Clearing vars from old next_server
    if(next_server){
        free(next_server->server);
        free(next_server);
        next_server = NULL;
    } 
    strcpy(next_server_path ,"");

    //returning in case no next in chain
    if(!next_name) {
        printf("No next node in chain (This server is the tail).\n");
        return;
    }
    
    //building next_server's node_path
    char node_path[100] = "";
	strcat(node_path,root_path);
	strcat(node_path,"/");
	strcat(node_path,next_name);

    //setting the new next_server
    strcpy(next_server_path ,node_path);
    printf("New next node in chain: %s\n", next_server_path);

    //getting the servers adress (node data)   
    char *zoo_data = malloc(ZDATALEN * sizeof(char));
	int zoo_data_len = ZDATALEN;

    zoo_get(zh, next_server_path, 0, zoo_data, &zoo_data_len, NULL);

    printf("IP adress of next node: %s\n", zoo_data);

    //Connecting to the server
    if((next_server = rtree_connect(zoo_data)) == NULL){
        perror("Error connecting to next server in chain.");
        exit(EXIT_FAILURE);
    }
    printf("Connectiong to: %s established\n", zoo_data);
}

void children_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context) {
    printf("----------------------//----------------------\n");
    printf("Changes in chain detected.\n");

    //Resetting watcher and getting the a list with the children
    zoo_string* children_list =	(zoo_string *) malloc(sizeof(zoo_string));
    zoo_wget_children(zh, root_path, children_watcher, context, children_list);

    //Resetting connection to next server in chain
    setup_next(children_list);
    free(children_list);
}

/* Inicia o skeleton da árvore. 
 * O main() do servidor deve chamar esta função antes de poder usar a 
 * função invoke().  
 * A função deve lançar N threads secundárias responsáveis por atender  
 * pedidos de escrita na árvore. 
 * Retorna 0 (OK) ou -1 (erro, por exemplo OUT OF MEMORY) 
 */
int tree_skel_init(char* host_port, char* own_port){
    //Creating tree
    tree = tree_create();

    if(tree == NULL)
        return -1;

    threads_amount = 1;

    //Reserving in_progress array and setting last_assigned and max_proc
    last_assigned = 1;
    op_proc.max_proc = 0;
    op_proc.in_progress = calloc((threads_amount + 1), sizeof(int));
    op_proc.in_progress[threads_amount] = -1; //-1 used as terminator

    //Initialising locks
    pthread_mutex_init(&queue_lock, NULL);
    pthread_mutex_init(&tree_lock, NULL);
    pthread_mutex_init(&op_proc_lock, NULL);
    pthread_cond_init(&queue_not_empty, NULL);

    threads = calloc(threads_amount, sizeof(pthread_t));

    for (int i = 0; i < threads_amount; i++) {
        int id = i + 1;
        if(pthread_create(&threads[i], NULL, process_request, (void *) (intptr_t) id) != 0){
            perror("Error creating thread");
            exit(EXIT_FAILURE);
        }
        if(pthread_detach(threads[i]) != 0){
            perror("Error detaching thread");
            exit(EXIT_FAILURE);
        }
    }

    //Conecting to zookeeper
    zh = zookeeper_init(host_port, NULL, 2000, 0, 0, 0);
    if (zh == NULL) {
	    fprintf(stderr, "Error connecting to zookeeper server!\n");
	    exit(EXIT_FAILURE);
    }

	sleep(1); /* Sleep a little for connection to complete */

    //Checking for /chain and creating if needed
    if(zoo_exists(zh, root_path, 0, NULL) != 0) {
        printf("Path \"\\chain\" not present, creating node.\n");
        if (ZOK != zoo_create(zh, root_path, NULL, -1, & ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0)) {
				fprintf(stderr, "Error creating znode from path %s!\n", root_path);
			    exit(EXIT_FAILURE);
			}
        printf("Path \"\\chain\" created.\n");
    }

    //Getting own IP adress
    char hostbuffer[256];
    char *IPadress;
    struct hostent *host_entry;

    gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);
    IPadress = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    //Building the node path
    char node_path[100] = "";
	strcat(node_path,root_path);
	strcat(node_path,"/node");

    //Builidng the node data
    char node_data[100] = "";
	strcat(node_data,IPadress);
	strcat(node_data,":");
	strcat(node_data,own_port);

    //Creating node and storing its path
	int own_path_len = 1024;
	own_path = malloc (own_path_len);

    if (ZOK != zoo_create(zh, node_path, node_data, strlen(node_data) + 1, & ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL | ZOO_SEQUENCE, own_path, own_path_len)) {
		fprintf(stderr, "Error creating znode from path %s!\n", node_path);
		exit(EXIT_FAILURE);
	}

    printf("Server's znode path: %s\n", own_path);

    //Setting watcher over /chain's children and getting a list with them
    zoo_string* children_list =	(zoo_string *) malloc(sizeof(zoo_string));
    zoo_wget_children(zh, root_path, children_watcher, NULL, children_list);

    //Connecting to next server in the chain
    setup_next(children_list);
    free(children_list);

    return 0;
}

void queue_add_task(struct request_t *request){

    if (CLOSE_PROGRAM) return;
    
    pthread_mutex_lock(&queue_lock);
    if (queue_head == NULL){ /* Adiciona na cabeça da fila */
        queue_head = request;
        request->next = NULL;
    }
    else{ /* Adiciona no fim da fila */
        struct request_t *tptr = queue_head;
        while (tptr->next != NULL)
            tptr = tptr->next;
        tptr->next = request;
        request->next = NULL;
    }
    pthread_cond_signal(&queue_not_empty); /* Avisa um bloqueado nessa condição */
    pthread_mutex_unlock(&queue_lock);
}

struct request_t *queue_get_task(){
    pthread_mutex_lock(&queue_lock);

    if (CLOSE_PROGRAM){
        pthread_mutex_unlock(&queue_lock);
        return NULL;
    }

    while (queue_head == NULL) {
        puts("  Thread waiting");
        pthread_cond_wait(&queue_not_empty, &queue_lock); /* Espera haver algo */
        if (CLOSE_PROGRAM){
            pthread_mutex_unlock(&queue_lock);
            return NULL;
        }
    }

    puts("  Started working");
    struct request_t *task = queue_head;
    queue_head = task->next;

    pthread_mutex_unlock(&queue_lock);
    return task;
}

void add_to_in_progress(int op_n){
    pthread_mutex_lock(&op_proc_lock);
    
    int i = 0;
    while (op_proc.in_progress[i] != 0)
        i++;
    op_proc.in_progress[i] = op_n;
    
    pthread_mutex_unlock(&op_proc_lock);
}

void remove_from_in_progress(int op_n){
    pthread_mutex_lock(&op_proc_lock);

    int i = 0;
    while (op_proc.in_progress[i] != op_n)
        i++;
    op_proc.in_progress[i] = 0;

    if (op_n > op_proc.max_proc)
            op_proc.max_proc = op_n;

    pthread_mutex_unlock(&op_proc_lock);
}

/* Função da thread secundária que vai processar pedidos de escrita. 
*/ 
void * process_request (void *params){
    int id = (intptr_t) params;

    while (1)
    {
        struct request_t *task = queue_get_task();

        if (CLOSE_PROGRAM){
            printf("  Thread %d closing\n", id);
            pthread_exit(NULL);
        }

        struct data_t *data = task->data;
        char *key = task->key;
        int op = task->op;
        int op_n = task->op_n;

        printf("  Thread %d started processing task %d\n", id, task->op_n);

        add_to_in_progress(op_n);

        pthread_mutex_lock(&tree_lock);
        switch (op)
        {
            case 0:     // DELETE
                tree_del(tree, key);
                break;
            case 1:     // PUT
                tree_put(tree, key, data);
                break;
        }
        pthread_mutex_unlock(&tree_lock);

        remove_from_in_progress(op_n);

        printf("  Thread %d finished processing task %d\n", id, task->op_n);

        free(task->key);
        data_destroy(task->data);
        free(task);
    }

    return 0;
}

/* Liberta toda a memória e recursos alocados pela função tree_skel_init.
 */
void tree_skel_destroy(){
    CLOSE_PROGRAM = 1;

    for (size_t i = 0; i < threads_amount; i++){
        pthread_cond_signal(&queue_not_empty);
    }

    usleep(threads_amount * 5000); //to give time for the threads to free themslefs

    //freeing in_progress array
    free(op_proc.in_progress);

    //freeing list of threads
    free(threads);

    //destroying locks
    pthread_mutex_destroy(&queue_lock);
    pthread_mutex_destroy(&tree_lock);
    pthread_mutex_destroy(&op_proc_lock);
    pthread_cond_destroy(&queue_not_empty);
    
    //destroying tree
    tree_destroy(tree);

    //disconnecting from the next server in the chain
    rtree_disconnect(next_server);
}

/* Verifica se a operação identificada por op_n foi executada. 
 */
//Depricated!!!
int verify(int op_n){
    int ret = 0;
    if(op_n <= 0)
        return -1;
    if(op_n <= op_proc.max_proc){
        ret = 1;
        pthread_mutex_lock(&op_proc_lock);
        for (size_t i = 0; op_proc.in_progress[i] != -1; i++){
            if(op_n == op_proc.in_progress[i]){
                ret = 0;
                break;
            }
        }
        pthread_mutex_unlock(&op_proc_lock);
    }
    return (ret);
}

/* Executa uma operação na árvore (indicada pelo opcode contido em msg)
 * e utiliza a mesma estrutura message_t para devolver o resultado.
 * Retorna 0 (OK) ou -1 (erro, por exemplo, árvore nao incializada)
*/
int invoke(MessageT *msg) {
    MessageT__Opcode op = msg->opcode;

    struct data_t* data;
    struct request_t* new_request;

    switch(op) {
        case MESSAGE_T__OPCODE__OP_SIZE: ;
            printf("Requested: size\n");

            pthread_mutex_lock(&tree_lock);
            msg->result = tree_size(tree);
            pthread_mutex_unlock(&tree_lock);

            msg->opcode = MESSAGE_T__OPCODE__OP_SIZE + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
            return 0;


        case MESSAGE_T__OPCODE__OP_HEIGHT: ;
            printf("Requested: height\n");

            pthread_mutex_lock(&tree_lock);
            msg->result = tree_height(tree);
            pthread_mutex_unlock(&tree_lock);

            msg->opcode = MESSAGE_T__OPCODE__OP_HEIGHT + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
            return 0;
            break;

        case MESSAGE_T__OPCODE__OP_DEL: ;

            printf("Requested: del %s\n", msg->entry->key);

            //creating request
            new_request = malloc(sizeof(struct request_t));
            //inserting key
            new_request->key = malloc(strlen(msg->entry->key) + 1);
            strcpy(new_request->key, msg->entry->key);
            //setting op and op_n
            new_request->op = 0;
            new_request->op_n = last_assigned;
            last_assigned++;
            
            queue_add_task(new_request);

            //propagating to next server
            if(next_server){
                printf("Propagating message to next in chain\n");
                message_t__free_unpacked(s2s_network_send_receive(next_server, msg), NULL);
            }

            //creating answer msg
            msg->opcode = MESSAGE_T__OPCODE__OP_DEL + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
            msg->op_n = new_request->op_n;

            return 0;

        case MESSAGE_T__OPCODE__OP_GET: ;
            printf("Requested: get %s\n", msg->entry->key);

            pthread_mutex_lock(&tree_lock);
            data = tree_get(tree, msg->entry->key);
            pthread_mutex_unlock(&tree_lock);

            //caso a key nao esteja presente
            if(data == NULL){
                msg->opcode = MESSAGE_T__OPCODE__OP_GET + 1;
                msg->c_type = MESSAGE_T__C_TYPE__CT_NONE;
                msg->entry->data.data = NULL;
                msg->entry->data.len = 0;
                return 0;
            }

            msg->opcode = MESSAGE_T__OPCODE__OP_GET + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_VALUE;
            msg->entry->data.data = data->data;
            msg->entry->data.len = data->datasize;

            free(data);

            return 0;

        case MESSAGE_T__OPCODE__OP_PUT: ;
            printf("Requested: put %s %s\n", msg->entry->key, (char*)msg->entry->data.data);

            //creating request
            new_request = malloc(sizeof(struct request_t));
            //inserting key
            new_request->key = malloc(strlen(msg->entry->key) + 1);
            strcpy(new_request->key, msg->entry->key);
            //inserting data
            new_request->data = data_create(msg->entry->data.len);
            memcpy(new_request->data->data, msg->entry->data.data, new_request->data->datasize);
            //setting op and op_n
            new_request->op = 1;
            new_request->op_n = last_assigned;
            last_assigned++;
            
            queue_add_task(new_request);

            //propagating to next server
            if(next_server){
                printf("Propagating message to next in chain\n");
                message_t__free_unpacked(s2s_network_send_receive(next_server, msg), NULL);
            }

            //creating answer msg
            msg->opcode = MESSAGE_T__OPCODE__OP_PUT + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
            msg->op_n = new_request->op_n;

            return 0;

        case MESSAGE_T__OPCODE__OP_VERIFY: ;
            printf("Requested: verify %d\n", msg->op_n);

            //creating answer msg
            msg->opcode = MESSAGE_T__OPCODE__OP_VERIFY + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_RESULT;
            msg->result = verify(msg->op_n);

            return 0;

        case MESSAGE_T__OPCODE__OP_GETKEYS: ;
            printf("Requested: getkeys\n");

            pthread_mutex_lock(&tree_lock);
            char** keys = tree_get_keys(tree);
            pthread_mutex_unlock(&tree_lock);

            //caso arvore vazia
            if(keys == NULL){
                msg->opcode = MESSAGE_T__OPCODE__OP_BAD;
                msg->c_type = MESSAGE_T__C_TYPE__CT_BAD;
                return 0;
            }

            msg->opcode = MESSAGE_T__OPCODE__OP_GETKEYS + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_KEYS;
            pthread_mutex_lock(&tree_lock);
            msg->n_keys = tree_size(tree);
            pthread_mutex_unlock(&tree_lock);
            msg->keys = keys;

            return 0;

        case MESSAGE_T__OPCODE__OP_GETVALUES: ;
            printf("Requested: getvalues\n");

            pthread_mutex_lock(&tree_lock);
            void** datas = tree_get_values(tree);
            pthread_mutex_unlock(&tree_lock);

            //caso arvore vazia
            if(datas == NULL){
                msg->opcode = MESSAGE_T__OPCODE__OP_BAD;
                msg->c_type = MESSAGE_T__C_TYPE__CT_BAD;
                return 0;
            }

            pthread_mutex_lock(&tree_lock);
            msg->n_values = tree_size(tree);
            pthread_mutex_unlock(&tree_lock);

            msg->values = malloc(msg->n_values * sizeof(ProtobufCBinaryData));

            for (size_t i = 0; i < msg->n_values; i++){
                ProtobufCBinaryData data_temp;
                data = (struct data_t*)datas[i];
                data_temp.len = data->datasize;
                data_temp.data = malloc(data->datasize);
                memcpy(data_temp.data, data->data, data->datasize);
                msg->values[i] = data_temp;
                data_destroy(data);
            }
            free(datas);
            
            msg->opcode = MESSAGE_T__OPCODE__OP_GETVALUES + 1;
            msg->c_type = MESSAGE_T__C_TYPE__CT_VALUES;

            return 0;

        case MESSAGE_T__OPCODE__OP_ERROR: ;
            printf("Received message signaling error.\n");
            return -1;
        default: ;
            printf("Request not recognised.\n");
            return -1;
    }

    return -1;
};
