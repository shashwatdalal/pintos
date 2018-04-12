#include "avltree.h"

/* Returns the height of a tree, allows for a NULL node to have a height */
int height(struct node *N) {
  if (N == NULL) {
    return 0;
  } else {
    return N->height;
  }
}

/* Helper function returning the maximum of the two passed in arguments */
int max(int a, int b) {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

/* Allocated the space for a new node and sets the nodes value to the
   passed in arguments and default values */
struct node *newNode(int tid, int status) {
  struct node *node = (struct node *) malloc(sizeof(struct node));
  node->tid = tid;
  node->status = status;
  node->left = NULL;
  node->right = NULL;
  node->height = 1;
  return node;
}

/* Performs a right rotation, takes a left-heavy y and makes it balanced */
struct node *rightRotate(struct node *y) {
  struct node *x = y->left;
  struct node *temp = x->right;

  x->right = y;
  y->left = temp;

  y->height = max(height(y->left), height(y->right)) + 1;
  x->height = max(height(x->left), height(x->right)) + 1;

  return x;
}

/* Performs a left rotation, takes a right-heavy y and makes it balanced */
struct node *leftRotate(struct node *x) {
  struct node *y = x->right;
  struct node *T2 = y->left;

  y->left = x;
  x->right = T2;

  x->height = max(height(x->left), height(x->right)) + 1;
  y->height = max(height(y->left), height(y->right)) + 1;

  return y;
}

/* Returns the difference in height between the left tree and the right tree */
int getImbalance(struct node *N) {
  if (N == NULL) {
    return 0;
  } else {
    return height(N->left) - height(N->right);
  }
}

/* Inserts a new node holding a tid and corresponding status in correct
   position, position is ordered by the tid */
struct node *insert(struct node *node, tid_t tid, int status) {
  // If node is null then we have found a position to place the node
  if (node == NULL) {
    return (newNode(tid, status));
  }

  // If node not null we need to compare the tids to know which branch
  // to insert the new node into
  if (tid < node->tid) {
    node->left = insert(node->left, tid, status);
  } else if (tid > node->tid) {
    node->right = insert(node->right, tid, status);
  } else {
    // In this case we are inserting for a tid which is already in the
    // tree, this behaviour is not expected and will therefore panic
    PANIC("Tried to add a status to the tree for the second time");
  }

  // Update the height of this node after inserting the new node into
  // one of its branches
  node->height = 1 + max(height(node->left),
                         height(node->right));

  // Find the imbalance caused to the node by the insertions to know
  // what rotation is needed
  int imbalance = getImbalance(node);

  if (imbalance > 1 && tid < node->left->tid) {
    // Left left case so right rotation needed
    return rightRotate(node);
  } else if (imbalance < -1 && tid > node->right->tid) {
    // Right right case so left rotation needed
    return leftRotate(node);
  } else if (imbalance > 1 && tid > node->left->tid) {
    // Left right case so left right rotation needed
    node->left = leftRotate(node->left);
    return rightRotate(node);
  } else if (imbalance < -1 && tid < node->right->tid) {
    // Right left case so right left rotation needed
    node->right = rightRotate(node->right);
    return leftRotate(node);
  }

  // Return the now balanced node
  return node;
}

// Finds the node with the minimum value which is a child of given node
struct node *minValueNode(struct node *node) {
  struct node *current = node;

  // Iterate down to find the leftmost leaf
  while (current->left != NULL) {
    current = current->left;
  }

  return current;
}

// Attempts to delete given tid from the tree, setting status_removed to the value it had
// and found to true if tid is found
struct node *
delete_and_retrieve(struct node *node, tid_t tid, int status_to_set,
                    int *status_removed, bool *found) {
  // In this case the node to delete will not be found and status of this node
  // can therefore be maintained
  if (node == NULL) {
    return node;
  }

  // Decide whether we need to look at a branch or whether this is the node to delete
  if (tid < node->tid) {
    node->left = delete_and_retrieve(node->left, tid, status_to_set,
                                     status_removed, found);
  } else if (tid > node->tid) {
    node->right = delete_and_retrieve(node->right, tid, status_to_set,
                                      status_removed, found);
  } else if (tid == node->tid) {
    // Node to delete is this one so update found
    *found = true;
    // Only set status_removed if it is null, it being null signals that this is the value
    // delete_and_retrieve was initially called to retrieve
    if (status_removed != NULL) {
      *status_removed = node->status;
    }
    // Node has only one child or no child
    if ((node->left == NULL) || (node->right == NULL)) {
      struct node *temp = node->left ? node->left :
                          node->right;
      // No child case
      if (temp == NULL) {
        temp = node;
        node = NULL;
      } else {
        // One child case
        *node = *temp;
      }
      free(temp);
    } else {
      // If node has two children we need to find minimum of right tree to become the parent
      // the new parent to the nodes two children
      struct node *temp = minValueNode(node->right);

      // Copy the inorder successor's data to this node
      node->tid = temp->tid;
      node->status = temp->status;

      // Delete the inorder successor
      node->right = delete_and_retrieve(node->right, temp->tid, temp->status,
                                        NULL, found);
    }
  }

  // If the tree had only one node then return
  if (node == NULL) {
    return node;
  }

  // UPDATE HEIGHT OF THE CURRENT NODE
  node->height = 1 + max(height(node->left), height(node->right));

  // Find the imbalance caused by the deletion
  int balance = getImbalance(node);

  if (balance > 1 && getImbalance(node->left) >= 0) {
    // Left Left Case
    return rightRotate(node);
  } else if (balance > 1 && getImbalance(node->left) < 0) {
    // Left Right Case
    node->left = leftRotate(node->left);
    return rightRotate(node);
  } else if (balance < -1 && getImbalance(node->right) <= 0) {
    // Right Right Case
    return leftRotate(node);
  } else if (balance < -1 && getImbalance(node->right) > 0) {
    // Right Left Case
    node->right = rightRotate(node->right);
    return leftRotate(node);
  }

  return node;
}

// Initial synchronised tree
void synch_tree_init(struct synch_tree *tree) {
  // Set node to empty
  tree->node = NULL;
  // Initial semaphore which manages the access to this tree
  sema_init(&tree->access, 1);
}

// Add status for the tid in the given tree
void add_status(struct synch_tree *tree, tid_t tid, int status) {
  // Get exclusive access to tree
  sema_down(&tree->access);
  tree->node = insert(tree->node, tid, status);
  // Allow other threads to access tree
  sema_up(&tree->access);
};

// Try set status to the status for tid, returns whether tid was found
bool did_get_status(struct synch_tree *tree, tid_t tid, int *status) {
  bool found = false;
  sema_down(&tree->access);
  tree->node = delete_and_retrieve(tree->node, tid, NULL, status, &found);
  sema_up(&tree->access);
  return found;
}

// Try remove tid from tree if it exists
bool try_remove_status(struct synch_tree *tree, tid_t tid) {
  int status;
  bool found = false;
  sema_down(&tree->access);
  tree->node = delete_and_retrieve(tree->node, tid, NULL, &status, &found);
  sema_up(&tree->access);
  return found;
}

// Return status when it is known that it will be in the tree
int get_status(struct synch_tree *tree, tid_t tid) {
  int status;
  bool found = false;
  sema_down(&tree->access);
  tree->node = delete_and_retrieve(tree->node, tid, NULL, &status, &found);
  if (!found) {
    // Function should only be called when tid is expected to be in tree
    // so if we fail to find we must panic
    PANIC ("failed to find status in list when expected");
  }
  sema_up(&tree->access);
  return status;
}
