#include "data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Função que cria um novo elemento de dados data_t, reservando a memória
 * necessária para armazenar os dados, especificada pelo parâmetro size
 */
struct data_t *data_create(int size){
	struct data_t* data_ret = malloc(sizeof(struct data_t));

	//check malloc err
	if(data_ret == NULL){
		free(data_ret);
		return NULL;
	}

	//check se size é válido
	if(size <= 0) {
		free(data_ret);
		return NULL;
	}

	data_ret->datasize = size;


	data_ret->data = malloc(size);

	//check malloc err
	if(data_ret->data == NULL) {
		free(data_ret);
		return NULL;
	}

	return data_ret;
}

/* Função que cria um novo elemento de dados data_t, inicializando o campo
 * data com o valor passado no parâmetro data, sem necessidade de reservar
 * memória para os dados.
 */
struct data_t *data_create2(int size, void *data) {
	struct data_t *data_ret =  malloc(sizeof(struct data_t));
	
	//check malloc err
	if(data_ret == NULL){
		free(data_ret);
		return NULL;
	}

	//check se size e válido
	if(size <= 0) {
		free(data_ret);
		return NULL;
	}

	data_ret->datasize = size;

	//check se data e valido
	if(data == NULL){
		free(data_ret);
		return(NULL);
	}

	data_ret->data = data;

	return data_ret;
}

/* Função que elimina um bloco de dados, apontado pelo parâmetro data,
 * libertando toda a memória por ele ocupada.
 */
void data_destroy(struct data_t *data){
	if(data != NULL){
		if(data->data != NULL)
			free(data->data);
		free(data);
		data = NULL;
	}
}

/* Função que duplica uma estrutura data_t, reservando toda a memória
 * necessária para a nova estrutura, inclusivamente dados.
 */
struct data_t *data_dup(struct data_t *data) {
	//check se data e valido
	if(data == NULL || data -> datasize <= 0 || data -> data == NULL)
        return NULL;
	
	//cria data duplicado
	struct data_t *data_ret = data_create(data->datasize);
    
	//copia data->data
	memcpy(data_ret->data, data->data, data->datasize);

	return data_ret;
}

/* Função que substitui o conteúdo de um elemento de dados data_t.
*  Deve assegurar que destroi o conteúdo antigo do mesmo.
*/
void data_replace(struct data_t *data, int new_size, void *new_data) {
	//check se data e valido
	if(data == NULL || data -> datasize <= 0 || data -> data == NULL)
		return;
	
	//check se novo size e valido
	if(new_size <= 0)
		return;

	//check se novo data e valido
	if(new_data == NULL)
		return;
	
	free(data->data);
	data->datasize = new_size;
	data->data = new_data;
}
