#ifndef _TREE_PRIVATE_H
#define _TREE_PRIVATE_H

#include "tree.h"
#include "entry.h"

struct tree_t {
	struct node_t* root;
	int size;
};

struct node_t {
	struct entry_t* value;
	struct node_t* right_child;
	struct node_t* left_child;
};

#endif
