/*

  MyFS: a tiny file-system written for educational purposes

  MyFS is 

  Copyright 2018-21 by

  University of Alaska Anchorage, College of Engineering.

  Contributors: Christoph Lauter
                Benjamin Good and
                Logan Chamberlain

  and based on 

  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall myfs.c implementation.c `pkg-config fuse --cflags --libs` -o myfs

*/

#include <stddef.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

/* Helper types and functions */

//off_t to be used instead of pointers, defined in myfs.c

//Type memory_block stores the available size as well as an off_t to the next node. 
typedef struct memory_block{
  size_t size;
  size_t total_size;
  off_t next;
}memory_block;

//Type handle checks if fs has been booted before using special adddress used_indicator, stores an off_t to free memory and the root, as well as it's respective size. 
typedef struct handle_t{
  uint32_t used_indicator;
  off_t free;
  off_t root;
  size_t size;
}  handle_t;


typedef struct inode inode;
//General node type
typedef struct inode{
  int type; //2 for directories, 1 for files
  char name[256];
  uint32_t uid, gid; //User and group ID's
  int nlinks; //Number of links to node
  off_t parent;
  inode **children; //only applies for directories, same with numChildren
  int numChildren;
  off_t startOfData;
  size_t size; // only applies for files
  struct timespec st_atim; //timespec is defined in time.h, access time
  struct timespec st_mtim; //modified time
  struct timespec st_ctim; //status change time
}inode; 

#define MAX_NAME_LENGTH (256)
#define MAX_PATH_LENGTH (8192)
#define USED_INDICATOR ((uint32_t) (UINT32_C(0xdeadbeef)))
#define PAGE_SIZE (4096)

/* HELPER FUNCTION */
/*
time_stamp: sets the access time, and if modify flag is set, the modifed time of the passed node using time functions defined in time.h
*/
static void time_stamp(inode* node, int modify_flag){
  int fail;
  struct timespec time;
  if(node == NULL){
    return;
  }
  fail = clock_gettime(CLOCK_REALTIME, &time);
  if(!fail){
    node->st_atim = time;
    if(modify_flag){
      node->st_mtim = time;
    }
  }
}

/*
offset_to_ptr: takes in a pointer to the file system start or a handle and an offset, adds the offset to the filesystem start and 
converts it to a void *
*/

void * offset_to_ptr(void *fs_start, off_t off){
  if(off == ((off_t) 0)){
    return NULL;
  }
  else{
    return (void*)(fs_start + off);
  }
}

/*
ptr_to_offset, performs the inverse conversion as above, takes in a pointer to the filesystem start and converts it to an offset
via subtraction
*/

off_t ptr_to_offset(void* fs_start, void * ptr){
  
  if((ptr == NULL) || (ptr <= fs_start)){
    return ((off_t) 0);
  }
  else{
    return (off_t)(ptr-fs_start);
  }
}

/*BEGIN MEMORY FUNCTIONS*/

//functions largely modified versions of code written for HW2, modified to work with offsets rather than pointers

/*
get_handle: takes in a pointer to the filesystem start and checks if it has the used indicator set. If so, boot from backup file
*/
static handle_t* get_handle(void *ptr, size_t size){
  handle_t* h = malloc(sizeof(handle_t));
  size_t s;
  memory_block *block;
  //cast pointer to a handle
  h = (handle_t*) ptr;
  //See if has been initialized
  if(h->used_indicator != USED_INDICATOR){
    //If not, account for size of handle header
    s = size - sizeof(handle_t);
    if(h->used_indicator != ((uint32_t) 0)){
      //Here the memory has not been set, set all but header to 0's
      memset(ptr + sizeof(handle_t), 0, s);
    }
  
    h->used_indicator = USED_INDICATOR;
    h->size = s;
    h->root = (off_t) 0;
    h->free = (off_t) 0;
  }
  else{
    block = (memory_block *) (ptr + sizeof(handle_t));
    block->next = (off_t) 0;
    h->free = ptr_to_offset(ptr, block);
  }
    return h;
}

/*
total_free_space: takes in a handle and iterates over the linked list of memory blocks stored in the offset handle->free and computes a 
total free space in bytes. To be used in stat impl to compute the total free blocks available
*/

static size_t total_free_space( handle_t* h){
  size_t total;
  void * temp;
  memory_block * curr;
  if(h == NULL){
    return NULL;
  }
  if(h->size ==(size_t) 0){
    return (size_t) 0;
  }
  temp = offset_to_ptr(h, h->free);
  curr = (memory_block*) temp;
  while(curr != NULL){
    //Discount all headers
    total += (curr->size - sizeof(memory_block));
    temp = offset_to_ptr(h, curr->next);
    curr = (memory_block*) temp;
  }
  return total;
}

/*
get_memory: takes in a handle and a size, slices out a block of size bytes and adds the remainder to the free block list if it is large enough to be viable 
*/

static memory_block * get_memory( handle_t* h, size_t size){
  memory_block *curr, *prev;
  //Checks for null conditions
  if(h == NULL){
    return NULL;
  }
  if(size == (size_t) 0){
    return NULL;
  }
  //If no free memory left
  if(h->free == ((off_t) 0)){
    return NULL;
  }
  prev = NULL;
  curr = (memory_block*) offset_to_ptr(h, h->free);
  while(curr != NULL){
    if(curr->size >= size){
      if((curr->size - size) > sizeof(memory_block)){
	memory_block * new = (memory_block*) ((void*) curr + size);
	new->size = curr->size - size;
	new->next = curr->next;
	if(prev != NULL){
	  prev->next = ptr_to_offset(h, new);
	}
	else{
	  h->free = ptr_to_offset(h, new);
	}
	curr->size = size;
	return curr;
      }
      //size is too small, new slice will not be viable by itself
      else{
	if(prev == NULL){
	  h->free = curr->next;
	}
	else{
	  prev->next = curr->next;
	}
	curr->size = size;
	return curr;
      }
    }
  }

  /*
malloc_impl: an implemenation of a malloc like function, finds a free memory region within the bounds of the fs and returns an offset to that region
  */
}
static off_t malloc_impl( handle_t* h, size_t size){
  size_t newSize;
  memory_block* block;
  off_t result;
  void * free_space_ptr;
  if(h == NULL){
    return (off_t) 0;
  }
  if(size == (size_t)0){
    return (off_t) 0;
  }
  newSize = size + sizeof(memory_block);
  //Account for overflow
  if(newSize < 0){
    return (off_t)0;
  }
  block = get_memory(h, newSize);
  if(block == NULL){
    return (off_t) 0;
  }
  free_space_ptr = ((void *) block) + sizeof(memory_block);
  block->total_size = size;
  
  result = ptr_to_offset(h, free_space_ptr);
  if(result){
    return result;
  }
  else{
    return (off_t) 0;
  }
}

static void free_impl( handle_t* h, off_t o);

/*
realloc_impl: an implementation of a realloc like function, takes an offset to previously allocated region and finds a space of size bytes. Returns the old 
region to the free space and returns an offset to the new region. If size is smaller than current, the node is truncated. If larger, the remaining bytes are 
set to 0. If size is 0, acts as free_impl
*/

static off_t realloc_impl( handle_t* h, off_t prev, size_t size){
  void * prev_pointer, *new_pointer;
  memory_block *prev_block, *new;
  if(h == NULL){
    return (off_t) 0;
  }
  if(size == (size_t) 0){
    free_impl(h, prev);
    return ((off_t) 0);
  }
  prev_pointer = offset_to_ptr(h, prev);
  //Account for size of header
  prev_block = (memory_block*)prev_pointer - (sizeof(memory_block));
  size_t newSize = size + sizeof(memory_block);
  //find memory region within the bounds fs
  new = (memory_block*)offset_to_ptr(h, malloc_impl(h, newSize));
  new_pointer = (void*)new;
  new->size = size;
  new->next = ((off_t) 0);
  new->total_size = ((size_t) 0);
  if(newSize < prev_block->size){
    //Here we are truncating the file
    memcpy(new_pointer, prev_pointer, newSize);
  }
  else{
    //Here we are adding bytes, new memory is set to 0
    memcpy(new_pointer, prev_pointer, prev_block->size);
  }
  //Add old block to free list
  free_impl(h, prev);
}

/*
merge_blocks: takes a handle and checks if any of the blocks in the free memory list are consecutive, if so they can be merged into one larger block to reduce fragmentation
and speed up the system
*/

static void merge_blocks_impl(handle_t*h){
  inode* root = (inode*)h->root;
  memory_block *curr = (memory_block*)offset_to_ptr(h, h->free);
  while(curr->next != ((off_t) 0)){
    if(ptr_to_offset(h, (curr + sizeof(memory_block))) + curr->size == curr->next){
      memory_block *next = (memory_block*)offset_to_ptr(h, curr->next);
      curr->size += next->size + (sizeof(memory_block));
      curr->next = next->next;
      next = NULL;
    }
    curr = (memory_block*) offset_to_ptr(h, curr->next);
  }
}

/*
add_block: takes a handle to determine the root node and a memory block pointer, adds it in order of ascending offsets to the linked list of memory blocks
*/

static void add_block(handle_t* h, memory_block *b){
  memory_block *prev, *curr;
  if(h == NULL){
    return;
  }
  if(b == NULL){
    return;
  }
  prev = NULL;
  curr = (memory_block *) offset_to_ptr(h, h->free);
  while(curr != NULL){
    if(b < curr){
      break;
    }
    prev = curr;
    curr = (memory_block *) offset_to_ptr(h, curr->next);
  }
  if(prev == NULL){
    b->next = h->free;
    h->free = ptr_to_offset(h, b);
    merge_blocks_impl(h);
  }
  else{
    b->next =  ptr_to_offset(h, curr);
    prev->next = ptr_to_offset(h, b);
    merge_blocks_impl(h);
  }
}

/*
free_impl: takes a handle pointer to determine the root node and an offset, adds the offset to the list after accounting for the size of the header
*/

static void free_impl( handle_t* h, off_t o){
  memory_block *block;
  void *ptr;
  if(h == NULL){
    return;
  }
  if(o == (off_t) 0){
    return;
  }
  ptr = (offset_to_ptr(h, o)) - sizeof(memory_block);
  block = (memory_block *) ptr;
  add_block(h, block);
}
    
/*END MEMORY FUNCTIONS */
    
/*BEGIN TREE FUNCTIONS */
inode* getParent(handle_t* h, const char *path);

/*
find_node: a recursize function to find the last node in the path, while checking that each step is a directory and that the next step is a child of 
the parent.
*/

inode* find_node(handle_t* h, const char *path, inode *curr, int lastIndex){
  int i, j, oldnameLen, nameLen;
  char* subPath = NULL;
  if(path[lastIndex] != "/"){
    return NULL;
  }
  
  //Account for leading '/'
  nameLen = 1 + lastIndex;
  oldnameLen = nameLen;
  while(path[nameLen] != "/"){
    nameLen++;
  }
  //set subpath equal to slice of path 
  subPath = (char *)malloc((nameLen - oldnameLen) * sizeof(char));
  for(i = 1, j = 0; i <= nameLen; i++, j++){
    subPath[j] = path[i];
  }
  //Check if node was found
  if(strcmp((curr->name), subPath) != 0){
    return curr;
  }
  //Found name of the next path step
  //Search current's children for that slice
  char * child = (char*) malloc(sizeof(char) * MAX_NAME_LENGTH);
  for(i = 0; i < curr->numChildren; i++){
    child = curr->children[i]->name;
    if(strcmp(child, subPath) != 0){
      //Found in children, recurse using new information
      if(curr->type != 2){
	//if not directory return NULL
	return NULL;
      }
      curr = curr->children[i];
      free(child);
      free(subPath);
      find_node(h, path, curr, nameLen);
    }
    //If reached end of loop and none are equal, bad path, return NULL. If node is null, calling functions can set errno to ENOENT
    return NULL;
  }
}

/*
getParent: takes a handle pointer and a path, finds the second to last item in the path using the above find_node function
*/

inode* getParent( handle_t* h, const char *path){
  inode* parent_node = NULL;
  inode* root = (inode *) h->root;
  int i;
  //Find the length of the path without the last node
  for(i = strlen(path) - 1; path[i] != "/"; i--){}
  int newLength = strlen(path) - i + 1;
  char *subPath = (char*) malloc(sizeof(char) * newLength);
  for(i = 0; i < newLength; i++){
    subPath[i] = path[i];
  }
  parent_node = find_node(h, subPath, root, 0);
  //If not found or path invalid
  if(parent_node == NULL){
    return NULL;
  }
  return parent_node;
  }

/*
add_tree_node: takes a node and path, as well as handle to determine the root, getParent is called to find the directory to add to, then the array of nodes is reallocated 
using realloc impl to account for larger space and then it is added to the end. Returns -1 on failure and 0 on success
*/

int add_tree_node(inode* node, const char *path, handle_t* h){
  inode *parent_node = getParent(h, path);
  //If parent is null then path was bad
  if(parent_node == NULL){
    return -1;
  }
  //Increment num children
  parent_node->numChildren += 1;
  //Account for the new child added
  parent_node->children = realloc_impl(h, parent_node->children, sizeof(inode *) * parent_node->numChildren);
  //Create space for child to be added
  parent_node->children[parent_node->numChildren - 1] = malloc_impl(h,(sizeof(inode)));
  //Make node a child of parent node
  parent_node->children[parent_node->numChildren -1] =  node;

  return 0;
}

/*
remove_tree_node: takes a handle and a path, follows the path and removes the child from the parent directory, changing the number of child nodes and required space
in similar function to add_tree_node in inverse. Returns -1 on failure and 0 on success. 
*/

int remove_tree_node( handle_t* h, const char *path) {
  int i, nodeIndex;
  //Can't remove root node
  if(strcmp(path, "/") == 0){
    return -1;
  }
  inode *parent_node = getParent(h, path);
  inode *deleted_node = find_node(h, path, (inode*) h->root, 0);
  deleted_node->size = (size_t) 0;
  //Free memory space used by node
  free_impl(h, deleted_node->startOfData);  
  if(parent_node == NULL){
    return NULL;
  }
  char * child = (char*) malloc(sizeof(char)*MAX_NAME_LENGTH);
  for(i = 0; i < parent_node->numChildren; i++){
    child = parent_node->children[i]->name;
    if(!strcmp(child, deleted_node->name)){
      nodeIndex = i;
      free(child);
      break;
    }
  }
  //Ensure that the node to be deleted is the last in the array so truncation with realloc will delete the intended node
  if(nodeIndex != parent_node->numChildren - 1){
    inode *temp = offset_to_ptr(h, parent_node->children[parent_node->numChildren -1]);
    parent_node->children[parent_node->numChildren -1] = deleted_node;
    parent_node->children[nodeIndex] = temp;
  }
  
  parent_node->numChildren -= 1;
  //Calling realloc with a smaller size will free the last position
  parent_node->children = realloc(parent_node->children, sizeof(inode *) * parent_node->numChildren);
  free(deleted_node);
  free(parent_node);
  return 0;
}
/*END TREE FUNCTIONS */

/* 
__myfs_getattr_implem: Implements an emulation of the stat system call on the filesystem 
of size fssize pointed to by fsptr.  If path can be followed and describes a file or directory 
that exists and is accessable, the access information is 
put into stbuf. On success, 0 is returned. On failure, -1 is returned and 
the appropriate error code is put into *errnoptr.
 */

int __myfs_getattr_implem(void *fsptr, size_t fssize, int *errnoptr,
                          uid_t uid, gid_t gid,
                          const char *path, struct stat *stbuf) {
  handle_t* h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = ENOMEM;
    return -1;
  }
  inode * root = (inode*) h->root;
  inode * found = find_node(h, path, root, 0);
  inode *parent = getParent(h, path);
  //Account for cases where path ends of '.' (current directory) and '..' (parent directory)
  if(path[strlen(path - 2)] == '.'){
      if(path[strlen(path - 3)] == '.'){
	found = parent;
      }
      char subpath[strlen(path - 2)];
      strcpy(subpath, path);
      found = find_node(h, subpath, root, 0);
    }
  if(found == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  else{
    //Found file or dir, populate stbuf with info provided
  stbuf->st_uid = uid;
  stbuf->st_gid = gid;
  stbuf->st_atim = (found->st_atim);
  stbuf->st_mtim = (found->st_mtim);
  //If type is 1, found file. Fill in size
  if(found->type == 1){
    stbuf->st_mode = S_IFREG | 0755;
    stbuf->st_nlink = 1 + found->nlinks;
    stbuf->st_size = found->size;
  }
  //If type is 2, found directory, fill in nlinks + 2
  else{
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2 + found->nlinks;
      }
  }
  return 0;
}

/*
__myfs_readdir_implem: Implements an emulation of the readdir system call on the filesystem 
of size fssize pointed to by fsptr.   If path can be followed and describes a directory that exists and
is accessable, the names of the subdirectories and files contained in that directory are output 
into *namesptr. If it needs to output file and subdirectory names, the function starts by allocating 
(with calloc) an array of pointers to characters of the right size (n entries for n names). Sets*namesptr
to that pointer. It then goes over all entries in that array and allocates, for each of them an array of
characters of the right size (to hold the i-th name, together with the appropriate '\0' terminator). 
It puts the pointer into that i-th array entry and fills the allocated array of characters with the 
appropriate name.  The function returns the number of names that have beenput into namesptr.
*/
  
int __myfs_readdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, char ***namesptr) {
  inode *dir;
  int i;
  char ** nameArray;
  handle_t* h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  dir = find_node(h, path, (inode*) h->root, 0);
  if(dir == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  if((dir->type != 2)) {
    *errnoptr = ENOTDIR;
    return -1;
  }
  //No allocation if empty directory
  if(dir->numChildren == 0){
    return 0;
  }

  //update timestamp for directory, but not modified time
  time_stamp(dir,0);
  //Create array for child names, set all to 0
  *nameArray = (char*) calloc(sizeof(char), sizeof(char) * dir->numChildren);
  //If cannot allocate because too small
  if(nameArray == NULL){
    free(nameArray);
    *errnoptr = EINVAL;
    return -1;
  }
  for(i = 0; i < dir->numChildren; i++){
    strcpy(nameArray[i], dir->children[i]->name);
  }
  *namesptr = nameArray;
  return dir->numChildren;
}

/* 
__myfs_mknod_implem: Implements an emulation of the mknod system call for regular files on the 
filesystem of size fssize pointed to by fsptr. This function is called only for the creation of regular files.
Creates the file indicated by path if it does not exist, and the size is set to 0.
*/

int __myfs_mknod_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  int i, j, path_length, file_index, file_length;
  inode *parent_node = NULL;
  inode *new_file = NULL;
  handle_t* h = get_handle(fsptr, fssize);
  if(path == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  parent_node = getParent(h, path);
  if(parent_node == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  //If parent node is not a directory, set errno to ENOTDIR
  if(parent_node->type != 2){
    *errnoptr = ENOTDIR;
    return -1;
  }
  //If a there is already a node, exit
  if(find_node(h,path, (inode*)h->root, 0)){
    *errnoptr = EEXIST;
    return -1;
  }
  //Max name length is 256, if too long, exit
  path_length = strlen(path) - 1;
  for(i = path_length; path[i] != "/"; i--){
  }
  file_index = i;
  file_length = path_length - file_index + 1;
  if(file_length > MAX_NAME_LENGTH){
    *errnoptr = ENAMETOOLONG;
    return -1;
  }
  char* fileName = (char*)malloc(sizeof(char) * (file_length));
  fileName[file_length] = "\0";
  for(i = file_index, j = 0; i < strlen(path); i++, j++){
    fileName[j] = path[i];
  }
  new_file = (inode*)offset_to_ptr(h, malloc_impl(h, (sizeof(inode))));
  *new_file->name = fileName;
  new_file->parent = ptr_to_offset(h, parent_node);
  new_file->size = (size_t) 0;
  new_file->type = 1;
  //Add new access and modify times for node, set to current time
  time_stamp(new_file, 1);
  //Parent directory has been accessed and modified as well
  time_stamp(parent_node, 1);
  int fail = add_tree_node(new_file, path, h);
  if(!fail)
    {
      return 0;
    }
  else{
    return -1;
  }
}

/* 
__myfs_unlink_implem: Implements an emulation of the unlink system call for regular files
on the filesystem of size fssize pointed to by fsptr. On success, 0 is returned, on failure
-1 is returned and errno is set 
*/

int __myfs_unlink_implem(void *fsptr, size_t fssize, int *errnoptr,const char *path){
  
  if (path == NULL) {
	  // ENOENT if path is empty
	  *errnoptr = ENOENT;
	  return -1;
  }
  
  handle_t* h = get_handle(fsptr, fssize);  
  if (h == NULL) {
	  *errnoptr = EFAULT;
	  return -1;
  }
  inode* root = (inode*)h->root;
  inode* node = find_node(h,path, root, 0);
  inode*parent_node = getParent(h, path);
  if(parent_node == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  //Parent directory is being both accessed and modified
  time_stamp(parent_node, 1);
  if(node->type != 1){
    *errnoptr = EISDIR;
    return -1;
  }
  //Remove tree node returns 0 on succes
  if(!remove_tree_node(h,path)){
    return 0;
    }
    else{
      //Unspecifed error must have occured
      return -1;
    }
}

/* 
__myfs_rmdir_implem: Implements an emulation of the rmdir system call on the filesystem 
of size fssize pointed to by fsptr. The call deletes the directory indicated by path.
On success, 0 is returned. On failure, -1 is returned and *errnoptr is set appropriately.
*/

int __myfs_rmdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  handle_t* h;
  inode *parent_node;
  //Must pass valid path name
  if(path == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  h = get_handle(fsptr, fssize);
  //Pointer outside fs space
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  parent_node = getParent(h, path);
  //Bad path
  if(parent_node == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  //cannot call rmdir on a file
  if(parent_node->type != 2){
    *errnoptr = ENOTDIR;
    return -1;
  }
  //Cannot remove a non empty directory
  if(parent_node->numChildren != 0){
    *errnoptr = ENOTEMPTY;
    return -1;
  }
  //Remove tree node returns 0 on success
  if(!remove_tree_node(h, path)){
    return 0;
  }
  else{ 
    return -1;
  }
}

/* 
__myfs_mkdir_implem: Implements an emulation of the mkdir system call on the filesystem 
of size fssize pointed to by fsptr. The call creates the directory indicated by path.
On success, 0 is returned. On failure, -1 is returned and *errnoptr is set appropriately.
*/

int __myfs_mkdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  handle_t* h = get_handle(fsptr, fssize);
  int dirIndex, dirNameLen, i, j;
  char *dirName;
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  inode * parent_node = getParent(h, path);
  //Bad path
  if(parent_node == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  //If parent is not a directory
  if(parent_node->type != 2){
    *errnoptr = ENOTDIR;
    free(h);
    free(parent_node);
    return -1;
  }
  //Cannot make a directory that already exists
  if(find_node(h, path, (inode*)h->root, 0)){
    *errnoptr = EEXIST;
    free(h);
    free(parent_node);
    return -1;
  }
  for(i = strlen(path) -1; path[i] != "/"; i--){
    dirIndex = i;
  }
  
  dirNameLen = strlen(path) - i;
  if(dirNameLen > MAX_NAME_LENGTH){
    free(h);
    free(parent_node);
    *errnoptr = ENAMETOOLONG;
    return -1;
  }
  dirName = (char*) malloc(sizeof(char) * dirNameLen);
  dirName[dirNameLen] = "\0";
  for(i = dirIndex, j = 0; i < strlen(path); i++, j++){
    dirName[j] = path[i];
  }
  //Allocate space for new directory within fs memory
  inode* newDir = (inode*)offset_to_ptr(h, malloc_impl(h, sizeof(inode)));
  *newDir->name = dirName;
  newDir->type = 2;
  newDir->parent = ptr_to_offset(h, parent_node);
  newDir->size = 0;
  newDir->numChildren = 0;
  //Parent and new have both been access and modified, timestamp
  time_stamp(newDir, 1);
  time_stamp(parent_node, 1);
  int fail = add_tree_node(newDir, path, h);
  if(!fail){
    return 0;
  }
  else{
    //Error adding tree node but path was traversed previously, if
    // reached this is an unspecifed error
  return -1;
  }
}

/*
__myfs_rename_implem: Implements an emulation of the rename system call on the filesystem 
of size fssize pointed to by fsptr. The call moves the file or directory indicated by from to to.
It does this by making a new node at the new address, and then copying information from old to new. 
This allows for both changing the name but keeping the directory, keeping the name but changing the
directory, or both. On success, 0 is returned. On failure, -1 is returned and *errnoptr is set appropriately
*/

int __myfs_rename_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *from, const char *to) {
  if (from == NULL || to == NULL) {
	  // if either paths are empty
	  *errnoptr = ENOENT;
	  return -1;
  }
  handle_t* h = get_handle(fsptr, fssize);
  if (h == NULL) {
	  *errnoptr = EFAULT;
	  return -1;
  }  
  inode* root = (inode*)h->root;
  inode* node = find_node(h, from, root, 0);
  inode*newNode = (inode*)offset_to_ptr(h, malloc_impl(h, sizeof(inode)));
  add_tree_node(newNode, to, h);
  newNode->type = node->type;
  newNode->nlinks = node->nlinks;
  if(newNode->type == 2){
    newNode->children = node->children;
    newNode->numChildren = node->numChildren;
  }
  else{
    newNode->size = node->size;
  }
  //Copy reference to data from old to new
  newNode->startOfData = node->startOfData;
  time_stamp(newNode, 1);
  remove_tree_node(h, node);
  return 0;
}

/*
__myfs_truncate_implem: Implements an emulation of the truncate system call on the filesystem 
of size fssize pointed to by fsptr. The call changes the size of the file indicated by path to off_t
using realloc_impl  When the file becomes smaller due to the call, the extending bytes are
removed. When it becomes larger, zeros are appended. On success, 0 is returned. On failure, -1 is 
returned and *errnoptr is set appropriately.
*/

int __myfs_truncate_implem(void *fsptr, size_t fssize, int *errnoptr,
                           const char *path, off_t offset) {
  //Must pass path
  if(path == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  handle_t* h = get_handle(fsptr, fssize);
  //Outside fs memory
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  inode *found = find_node(h, path, (inode*) h->root, 0);
  //Bad path
  if(found == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  //Not file
  if(found->type != 1){
    *errnoptr = EISDIR;
    return -1;
  }
  //offset 0 is the root dir, can't truncate
  if(offset == 0){
    *errnoptr = EINVAL;
    return -1;
  }
  size_t size_request = (size_t) offset;
  //Account for if file is already the right length
  if(found->size == size_request){
    //Change access but not modify time for dir
    time_stamp(found, 0);
    return 0;
  }
  if(found->size < size_request){
    //Account for case where file is empty
    if(found->size == ((size_t) 0)){
      //Allocate within fs mem
      found->startOfData = malloc_impl(h, size_request);
    }
    else{
      //realloc within fs mem
      found->startOfData = realloc_impl(h, found->startOfData, size_request);
      if(found->size < size_request){
	memset(offset_to_ptr(fsptr, (found->startOfData + size_request)), '0', sizeof(char));
      }
    }
  }
  return 0;
}

/* 
__myfs_open_implem: Implements an emulation of the open system call on the filesystem 
of size fssize pointed to by fsptr, without actually performing the opening
of the file (no file descriptor is returned). The call just checks if the file (or directory) 
indicated by path can be accessed, i.e. if the path can be followed to an existing
object for which the access rights are granted. On success, 0 is returned. On failure, -1 is 
returned and *errnoptr is set appropriately.
*/

int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path) {
  handle_t* h = get_handle(fsptr, fssize);
  //Outside fs memory
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  inode *found = find_node(h, path, (inode*) h->root, 0);
  //Bad path
  if(found == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  return 0;
}

/*
__myfs_read_implem: Implements an emulation of the read system call on the filesystem 
of size fssize pointed to by fsptr. The call copies up to size bytes from the file indicated by 
path into the buffer, starting to read at off_t. On success, the appropriate number of bytes read 
into the buffer is returned. The value zero is returned on an end-of-file condition. On failure, -1 
is returned and *errnoptr is set appropriately.
*/

int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
  //If path is empty
  if (path == NULL) {
	  *errnoptr = ENOENT;
	  return -1;
  }
  handle_t* h = get_handle(fsptr, fssize);
  //Outside fs memory
  if (h == NULL) {
	  *errnoptr = EFAULT;
	  return -1;
  }
  inode* root = (inode*)h->root;
  inode* node = find_node(h, path, root, 0);
  //Bad path
  if (node == NULL) {
    *errnoptr = ENOENT;
    return -1;
  }
  if (offset > node->size) {
	  *errnoptr = EINVAL;
	  return -1;
  }
  void* cursor = offset_to_ptr(h, node->startOfData+offset);
  off_t endOfFile = node->startOfData + node->size;
  size_t i;
  char* content = (char*) cursor;
  for (i = 0; i < size; i++) {
    //Check for end of file condition
    if(content + i == endOfFile){
      return 0;
    }
     *(buf + i) = *(content + i);
  } 
  return i;
}

/* 
__myfs_write_implem: Implements an emulation of the write system call on the filesystem 
of size fssize pointed to by fsptr. The call copies up to size bytes to the file indicated by 
path into the buffer, starting to write at off_t.  On success, the appropriate number of bytes written 
into the file is returned. The value zero is returned on an end-of-file condition. On failure, -1 
is returned and *errnoptr is set appropriately.
*/

int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size, off_t offset) {
  //Must pass path
  if(path == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  handle_t *h = get_handle(fsptr, fssize);
  //Outside fs memory
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  inode* node = find_node(h, path, (inode*)h->root, 0);
  //Bad path condition
  if(node == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  //If file not large enough, resize
  if(offset + node->size < size){
    node->startOfData = realloc_impl(h, node->startOfData, size);
  }
  //Trying to write outside file mem
  if(offset > node->startOfData + node->size){
    *errnoptr = EINVAL;
    return -1;
  }
  char*filecontents = (char*)offset_to_ptr(h, node->startOfData+offset);
  size_t i;
  off_t endOfFile = node->startOfData + node->size;
  for(i = 0; i < size; i++){
    //end of file condition
    if(offset + i > endOfFile){
      return 0;
    }
    *(filecontents + i) = *(buf + i);
  }
  
  return i;
}

/*
__myfs_utimens_implem: Implements an emulation of the utimensat system call on the filesystem 
of size fssize pointed to by fsptr. The call changes the access and modification times of the file
or directory indicated by path to the values in ts. On success, 0 is returned. On failure, -1 is 
returned and *errnoptr is set appropriately.
*/

int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
  handle_t* h = get_handle(fsptr, fssize);
  //Outside fs mem
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  inode* node = find_node(h, path, (inode*)h->root, 0);
  //Bad path
  if(node == NULL){
    *errnoptr = ENOENT;
    return -1;
  }
  node->st_atim = ts[0];
  node->st_mtim = ts[1];
  return 0;
}

/* 
__myfs_statfs_implem: Implements an emulation of the statfs system call on the filesystem 
of size fssize pointed to by fsptr. The call gets information of the filesystem usage and puts in 
into stbuf. On success, 0 is returned. On failure, -1 is returned and *errnoptr is set appropriately
*/

int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr,
                         struct statvfs* stbuf) {
  handle_t* h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  //Total free space iterates over free space offsets and computes a total
  size_t total_mem = total_free_space(h);
  memset(stbuf, 0, sizeof(statvfs));
  //Block size is standard size of 1024
  stbuf->f_bsize = (__fsword_t) 1024;
  stbuf->f_blocks = ((fsblkcnt_t) (fssize / 1024));
  stbuf->f_bfree = ((fsblkcnt_t) (total_mem / 1024));
  stbuf->f_bavail = ((fsblkcnt_t) (total_mem / 1024));
  //Max name length is 256
  stbuf->f_namemax = MAX_NAME_LENGTH;
  return 0;
}





