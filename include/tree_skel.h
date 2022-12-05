#ifndef _TREE_SKEL_H
#define _TREE_SKEL_H

#include "sdmessage.pb-c.h"
#include "tree.h"

/* Inicia o skeleton da árvore. 
 * O main() do servidor deve chamar esta função antes de poder usar a 
 * função invoke().  
 * A função deve lançar N threads secundárias responsáveis por atender  
 * pedidos de escrita na árvore. 
 * Retorna 0 (OK) ou -1 (erro, por exemplo OUT OF MEMORY) 
 */
int tree_skel_init(int N);

/* Função da thread secundária que vai processar pedidos de escrita. 
*/ 
void * process_request (void *params);

/* Liberta toda a memória e recursos alocados pela função tree_skel_init.
 */
void tree_skel_destroy();

/* Executa uma operação na árvore (indicada pelo opcode contido em msg)
 * e utiliza a mesma estrutura message_t para devolver o resultado.
 * Retorna 0 (OK) ou -1 (erro, por exemplo, árvore nao incializada)
*/
int invoke(MessageT *msg);

/* Verifica se a operação identificada por op_n foi executada. 
 */ 
int verify(int op_n); 

#endif
