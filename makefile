#Trabalho realisado por: Martim Baptista Nº56323
#                        Martim Paraíba Nº56273
#                        Daniel Luis Nº56362

OBJ_C = entry.o data.o client_stub.o network_client.o sdmessage.pb-c.o
OBJ_S = tree_server.o entry.o data.o tree.o tree_skel.o network_server.o sdmessage.pb-c.o

main: tree-client tree-server

client-lib.o:	required_dirs $(OBJ_C)
	ld -r $(addprefix object/,$(OBJ_C)) -o object/client-lib.o

tree-client: required_dirs client-lib.o tree_client.o
	gcc object/client-lib.o object/tree_client.o -pthread -lprotobuf-c -lzookeeper_mt -o binary/tree-client

tree-server: required_dirs $(OBJ_S) 
	gcc $(addprefix object/,$(OBJ_S)) -pthread -lprotobuf-c -lzookeeper_mt -o binary/tree-server

tree.o:
	cp lib/tree.o.extra object/tree.o

%.o: source/%.c
	gcc $< -c -I include -o object/$@ -g -Wall -lzookeeper_mt

required_dirs:
	mkdir -p object
	mkdir -p binary

clean:
	rm -f object/* binary/*