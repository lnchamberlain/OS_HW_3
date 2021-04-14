/*

  MyFS: a tiny file-system written for educational purposes

  MyFS is 

  Copyright 2018-21 by

  University of Alaska Anchorage, College of Engineering.

  Contributors: Christoph Lauter
                ... and
                ...

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


/* The filesystem you implement must support all the 13 operations
   stubbed out below. There need not be support for access rights,
   links, symbolic links. There needs to be support for access and
   modification times and information for statfs.

   The filesystem must run in memory, using the memory of size 
   fssize pointed to by fsptr. The memory comes from mmap and 
   is backed with a file if a backup-file is indicated. When
   the filesystem is unmounted, the memory is written back to 
   that backup-file. When the filesystem is mounted again from
   the backup-file, the same memory appears at the newly mapped
   in virtual address. The filesystem datastructures hence must not
   store any pointer directly to the memory pointed to by fsptr; it
   must rather store offsets from the beginning of the memory region.

   When a filesystem is mounted for the first time, the whole memory
   region of size fssize pointed to by fsptr reads as zero-bytes. When
   a backup-file is used and the filesystem is mounted again, certain
   parts of the memory, which have previously been written, may read
   as non-zero bytes. The size of the memory region is at least 2048
   bytes.

   CAUTION:

   * You MUST NOT use any global variables in your program for reasons
   due to the way FUSE is designed.

   You can find ways to store a structure containing all "global" data
   at the start of the memory region representing the filesystem.

   * You MUST NOT store (the value of) pointers into the memory region
   that represents the filesystem. Pointers are virtual memory
   addresses and these addresses are ephemeral. Everything will seem
   okay UNTIL you remount the filesystem again.

   You may store offsets/indices (of type size_t) into the
   filesystem. These offsets/indices are like pointers: instead of
   storing the pointer, you store how far it is away from the start of
   the memory region. You may want to define a type for your offsets
   and to write two functions that can convert from pointers to
   offsets and vice versa.

   * You may use any function out of libc for your filesystem,
   including (but not limited to) malloc, calloc, free, strdup,
   strlen, strncpy, strchr, strrchr, memset, memcpy. However, your
   filesystem MUST NOT depend on memory outside of the filesystem
   memory region. Only this part of the virtual memory address space
   gets saved into the backup-file. As a matter of course, your FUSE
   process, which implements the filesystem, MUST NOT leak memory: be
   careful in particular not to leak tiny amounts of memory that
   accumulate over time. In a working setup, a FUSE process is
   supposed to run for a long time!

   It is possible to check for memory leaks by running the FUSE
   process inside valgrind:

   valgrind --leak-check=full ./myfs --backupfile=test.myfs ~/fuse-mnt/ -f

   However, the analysis of the leak indications displayed by valgrind
   is difficult as libfuse contains some small memory leaks (which do
   not accumulate over time). We cannot (easily) fix these memory
   leaks inside libfuse.

   * Avoid putting debug messages into the code. You may use fprintf
   for debugging purposes but they should all go away in the final
   version of the code. Using gdb is more professional, though.

   * You MUST NOT fail with exit(1) in case of an error. All the
   functions you have to implement have ways to indicated failure
   cases. Use these, mapping your internal errors intelligently onto
   the POSIX error conditions.

   * And of course: your code MUST NOT SEGFAULT!

   It is reasonable to proceed in the following order:

   (1)   Design and implement a mechanism that initializes a filesystem
         whenever the memory space is fresh. That mechanism can be
         implemented in the form of a filesystem handle into which the
         filesystem raw memory pointer and sizes are translated.
         Check that the filesystem does not get reinitialized at mount
         time if you initialized it once and unmounted it but that all
         pieces of information (in the handle) get read back correctly
         from the backup-file. 

   (2)   Design and implement functions to find and allocate free memory
         regions inside the filesystem memory space. There need to be 
         functions to free these regions again, too. Any "global" variable
         goes into the handle structure the mechanism designed at step (1) 
         provides.

   (3)   Carefully design a data structure able to represent all the
         pieces of information that are needed for files and
         (sub-)directories.  You need to store the location of the
         root directory in a "global" variable that, again, goes into the 
         handle designed at step (1).
          
   (4)   Write __myfs_getattr_implem and debug it thoroughly, as best as
         you can with a filesystem that is reduced to one
         function. Writing this function will make you write helper
         functions to traverse paths, following the appropriate
         subdirectories inside the file system. Strive for modularity for
         these filesystem traversal functions.

   (5)   Design and implement __myfs_readdir_implem. You cannot test it
         besides by listing your root directory with ls -la and looking
         at the date of last access/modification of the directory (.). 
         Be sure to understand the signature of that function and use
         caution not to provoke segfaults nor to leak memory.

   (6)   Design and implement __myfs_mknod_implem. You can now touch files 
         with 

         touch foo

         and check that they start to exist (with the appropriate
         access/modification times) with ls -la.

   (7)   Design and implement __myfs_mkdir_implem. Test as above.

   (8)   Design and implement __myfs_truncate_implem. You can now 
         create files filled with zeros:

         truncate -s 1024 foo

   (9)   Design and implement __myfs_statfs_implem. Test by running
         df before and after the truncation of a file to various lengths. 
         The free "disk" space must change accordingly.

   (10)  Design, implement and test __myfs_utimens_implem. You can now 
         touch files at different dates (in the past, in the future).

   (11)  Design and implement __myfs_open_implem. The function can 
         only be tested once __myfs_read_implem and __myfs_write_implem are
         implemented.

   (12)  Design, implement and test __myfs_read_implem and
         __myfs_write_implem. You can now write to files and read the data 
         back:

         echo "Hello world" > foo
         echo "Hallo ihr da" >> foo
         cat foo

         Be sure to test the case when you unmount and remount the
         filesystem: the files must still be there, contain the same
         information and have the same access and/or modification
         times.

   (13)  Design, implement and test __myfs_unlink_implem. You can now
         remove files.

   (14)  Design, implement and test __myfs_unlink_implem. You can now
         remove directories.

   (15)  Design, implement and test __myfs_rename_implem. This function
         is extremely complicated to implement. Be sure to cover all 
         cases that are documented in man 2 rename. The case when the 
         new path exists already is really hard to implement. Be sure to 
         never leave the filessystem in a bad state! Test thoroughly 
         using mv on (filled and empty) directories and files onto 
         inexistant and already existing directories and files.

   (16)  Design, implement and test any function that your instructor
         might have left out from this list. There are 13 functions 
         __myfs_XXX_implem you have to write.

   (17)  Go over all functions again, testing them one-by-one, trying
         to exercise all special conditions (error conditions): set
         breakpoints in gdb and use a sequence of bash commands inside
         your mounted filesystem to trigger these special cases. Be
         sure to cover all funny cases that arise when the filesystem
         is full but files are supposed to get written to or truncated
         to longer length. There must not be any segfault; the user
         space program using your filesystem just has to report an
         error. Also be sure to unmount and remount your filesystem,
         in order to be sure that it contents do not change by
         unmounting and remounting. Try to mount two of your
         filesystems at different places and copy and move (rename!)
         (heavy) files (your favorite movie or song, an image of a cat
         etc.) from one mount-point to the other. None of the two FUSE
         processes must provoke errors. Find ways to test the case
         when files have holes as the process that wrote them seeked
         beyond the end of the file several times. Your filesystem must
         support these operations at least by making the holes explicit 
         zeros (use dd to test this aspect).

   (18)  Run some heavy testing: copy your favorite movie into your
         filesystem and try to watch it out of the filesystem.

*/

/* Helper types and functions */
//off_t to be used instead of pointers
typedef size_t offset;

//Type memory_block stores the used size and total size as well as an offset to the next node. 
typedef struct memory_block{
  size_t size;
  size_t total_size;
  offset next;
}memory_block;

//Type handle checks if fs has been booted before using used_indicator, stores an offset to free memory and the root, as well as it's respective size. 
typedef struct handle_t{
  uint32_t used_indicator;
  offset free;
  offset root;
  size_t size;
}handle_t;

//Type dir_t is a directory, stores the number of children nodes and an offset to them
typedef struct dir_t{
  int num_children;
  offset children;
}dir_t;

//Type file_t is a file, stores the size and the start of the file in the form of an offset
typedef struct file_t{
  size_t size;
  offset start_of_file;
}file_t;
typedef struct tree_node tree_node;
//General node type
typedef struct tree_node{
  int type; //1 for directories, 2 for files
  char name[256];
  uint32_t uid, gid; //User and group ID's
  int nlinks; //Number of links to node
  offset parent;
  tree_node **children; //only applies for directories, same with numChildren
  int numChildren;
  offset startOfData;
  size_t size; // only applies for files
  struct timespec st_atim; //timespec is defined in time.h, access time
  struct timespec st_mtim; //modified time
  struct timespec st_ctim; //status change time
}tree_node; 

#define MAX_NAME_LENGTH (256)
#define MAX_PATH_LENGTH (8192)
#define USED_INDICATOR ((uint32_t) (UINT32_C(0xdeadbeef)))
#define PAGE_SIZE (4096)
/* YOUR HELPER FUNCTIONS GO HERE */
//Function to set the current time
static void time_stamp(tree_node* node, int modify_flag){
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
//Converts an offset to a pointer
void * offset_to_ptr(void *fs_start, offset off){
  if(off == ((off_t) 0)){
    return NULL;
  }
  else{
    return (void*)(fs_start + off);
  }
}
//converts a pointer to an offset
offset ptr_to_offset(void* fs_start, void * ptr){
  
  if((ptr == NULL) || (ptr <= fs_start)){
    return ((offset) 0);
  }
  else{
    return (offset)(ptr-fs_start);
  }
}

/*BEGIN MEMORY FUNCTIONS, COPIED FROM HW2 and changed to work with handles rather than pointers*/
static handle_t* get_handle(void *ptr, size_t size){
  handle_t* h;
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
    h->root = (offset) 0;
    h->free = (offset) 0;
  }
  else{
    block = (memory_block *) (ptr + sizeof(handle_t));
    block->size = size;
    block->next = (offset) 0;
    h->free = ptr_to_offset(ptr, block);
  }
    return h;
}
//Returns the largest block size
static size_t largest_free_block(handle_t* h){
  size_t largest, without_header;
  memory_block *curr;
  void *temp;
  //Handle error checking
  if(h == NULL){
    return NULL;
  }
  if(h->size == (size_t) 0){
    return ((size_t) 0);
  }
  temp = offset_to_ptr(h, h->size);
  curr = (memory_block *) temp;
  while(curr != NULL){
    if(curr->size > largest){
      largest = curr->size;
    }
    temp = offset_to_ptr(h, curr->next);
    curr = (memory_block *) temp;
  }
  if(largest < sizeof(memory_block)){
    return ((size_t) 0);
  }
  without_header = largest - sizeof(memory_block);
  return without_header;
}
//Very similar to above, but computes a total rather than a max
static size_t total_free_space(handle_t* h){
  size_t total;
  void * temp;
  memory_block * curr;
  if(h == NULL){
    return NULL;
  }
  if(h->size =(size_t) 0){
    return (size_t) 0;
  }
  temp = offset_to_ptr(h, h->size);
  curr = (memory_block*) temp;
  while(curr != NULL){
    //Discount all headers
    total += (curr->size - sizeof(memory_block));
    temp = offset_to_ptr(h, curr->next);
    curr = (memory_block*) temp;
  }
  return total;
}
static memory_block * get_memory(handle_t *h, size_t size){
  memory_block *curr, *next, *prev;
  //Checks for null conditions
  if(h == NULL){
    return NULL;
  }
  if(size == (size_t) 0){
    return NULL;
  }
  //If not free memory left
  if(h->free == ((offset) 0)){
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
	  prev->next = new;
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
         

}
static offset malloc_impl(handle_t *h, size_t size){
  size_t newSize;
  memory_block* block;
  offset result;
  void * free_space_ptr;
  if(h == NULL){
    return (offset) 0;
  }
  if(size == (size_t)0){
    return (offset) 0;
  }
  newSize = size + sizeof(memory_block);
  //Account for overflow
  if(newSize < 0){
    return (offset)0;
  }
  block = get_memory(h, newSize);
  if(block == NULL){
    return (offset) 0;
  }
  free_space_ptr = ((void *) block) + sizeof(memory_block);
  block->total_size = size;
  
  result = offset_to_ptr(h, free_space_ptr);
  if(result){
    return result;
  }
  else{
    return (offset) 0;
  }
}
static offset realloc_impl(handle_t *h, offset prev, size_t size){
  offset newOff;
  memory_block *prev_block, *new;
  /*STUB */
}

static void add_block(handle_t *h, memory_block *b){
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
    /*ADD MERGE BLOCKS HERE*/
    // merge_blocks_impl(h);
  }
  else{
    b->next =  ptr_to_offset(h, curr);
    prev->next = ptr_to_offset(h, b);
    //merge_blocks_impl(h);
  }
}
static void free_impl(handle_t *h, offset o){
  memory_block *block;
  void *ptr;
  if(h == NULL){
    return;
  }
  if(o == (offset) 0){
    return (offset) 0;
  }
  ptr = (offset_to_ptr(h, o)) - sizeof(memory_block);
  block = (memory_block *) ptr;
  add_block(h, block);
}



    
/*END MEMORY FUNCTIONS */
    
/*BEGIN TREE FUNCTIONS */
//create tree is defined as a init function so we can check if the filesystem has been unmounted, and if so then root does not need to be created
/*
void create_tree(){
  tree_node* root = (tree_node*)malloc(sizeof(tree_node)); //TODO Change to use our implementation of malloc
  root->type = 2; //2 is for directories
  root->name = "/";
  root->nlinks = 2; // nlinks is 2 for directories, 1 for files
  root->parent = NULL;
  root->data = NULL;
  root->size = 0;
  root->numChildren = 0;
}
*/
tree_node* getParent(handle_t *h, const char *path);
tree_node* find_node(const char *path, tree_node *curr, int lastIndex){
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
  for(i = 0; i < curr->numChildren; i++){
    //LAST ERROR HERE
    if(strcmp(curr->children[i]->name, subPath) != 0){
      //Found in children, recurse using new information
      if(curr->type != 1){
	//if find node or get parent return -1, set errno to ENOTDIR
	return -1;
      }
      curr = curr->children[i];
      find_node(path, curr, nameLen);
    }
    //If reached end of loop and none are equal, bad path, return, set errno to ENOENT
    return -2;
  }
}
tree_node* getParent(handle_t*h, const char *path){
  tree_node* parent_node = NULL;
  tree_node* root = (tree_node *) h->root;
  int i;
  //Find the length of the path without the last node
  for(i = strlen(path) - 1; path[i] != "/"; i--){}
  int newLength = strlen(path) - i + 1;
  char *subPath = (char*) malloc(sizeof(char) * newLength);
  for(i = 0; i < newLength; i++){
    subPath[i] = path[i];
  }
  parent_node = find_node(subPath, root, 0);
  return parent_node;
    }

int add_tree_node(tree_node* node, const char *path, handle_t *h){
  //Pass around the start of the filessystem to avoid global variables
  tree_node *root = (tree_node*)h->root;
  tree_node *parent_node = getParent(h, path);
  //If parent is null then path was bad
  if(parent_node == -1){
    return -1;
  }
  //Adding a child to parent
  parent_node->numChildren += 1;
  //Account for the new child added
  parent_node->children = realloc(parent_node->children, sizeof(tree_node *) * parent_node->numChildren);
  //Create space for child to be added
  parent_node->children[parent_node->numChildren - 1] = (tree_node *)malloc(sizeof(tree_node));
  //Make node a child of parent node
  parent_node->children[parent_node->numChildren -1] = node;

  return 0;
}

int remove_tree_node(handle_t *h, const char *path) {
  //Can't delete root
  if(strcmp(path, "/") == 0){
    return -1;
  }
  tree_node *root = (tree_node*)h->root;
  tree_node *parent_node = getParent(h, path);
  tree_node *curr = find_node(path, root, 0);
  for(int i = 0; i < (parent_node->numChildren - 1); i++){
    parent_node->children[i] = parent_node->children[i+1];
  }
  parent_node->numChildren -= 1;
  parent_node->children = realloc(parent_node->children, sizeof(tree_node *) * parent_node->numChildren);
  return 0;
}
/*END TREE FUNCTIONS */

/*

  MyFS: a tiny file-system written for educational purposes

  MyFS is 

  Copyright 2018-21 by

  University of Alaska Anchorage, College of Engineering.

  Contributors: Christoph Lauter
                ... and
                ...

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


/* The filesystem you implement must support all the 13 operations
   stubbed out below. There need not be support for access rights,
   links, symbolic links. There needs to be support for access and
   modification times and information for statfs.

   The filesystem must run in memory, using the memory of size 
   fssize pointed to by fsptr. The memory comes from mmap and 
   is backed with a file if a backup-file is indicated. When
   the filesystem is unmounted, the memory is written back to 
   that backup-file. When the filesystem is mounted again from
   the backup-file, the same memory appears at the newly mapped
   in virtual address. The filesystem datastructures hence must not
   store any pointer directly to the memory pointed to by fsptr; it
   must rather store offsets from the beginning of the memory region.

   When a filesystem is mounted for the first time, the whole memory
   region of size fssize pointed to by fsptr reads as zero-bytes. When
   a backup-file is used and the filesystem is mounted again, certain
   parts of the memory, which have previously been written, may read
   as non-zero bytes. The size of the memory region is at least 2048
   bytes.

   CAUTION:

   * You MUST NOT use any global variables in your program for reasons
   due to the way FUSE is designed.

   You can find ways to store a structure containing all "global" data
   at the start of the memory region representing the filesystem.

   * You MUST NOT store (the value of) pointers into the memory region
   that represents the filesystem. Pointers are virtual memory
   addresses and these addresses are ephemeral. Everything will seem
   okay UNTIL you remount the filesystem again.

   You may store offsets/indices (of type size_t) into the
   filesystem. These offsets/indices are like pointers: instead of
   storing the pointer, you store how far it is away from the start of
   the memory region. You may want to define a type for your offsets
   and to write two functions that can convert from pointers to
   offsets and vice versa.

   * You may use any function out of libc for your filesystem,
   including (but not limited to) malloc, calloc, free, strdup,
   strlen, strncpy, strchr, strrchr, memset, memcpy. However, your
   filesystem MUST NOT depend on memory outside of the filesystem
   memory region. Only this part of the virtual memory address space
   gets saved into the backup-file. As a matter of course, your FUSE
   process, which implements the filesystem, MUST NOT leak memory: be
   careful in particular not to leak tiny amounts of memory that
   accumulate over time. In a working setup, a FUSE process is
   supposed to run for a long time!

   It is possible to check for memory leaks by running the FUSE
   process inside valgrind:

   valgrind --leak-check=full ./myfs --backupfile=test.myfs ~/fuse-mnt/ -f

   However, the analysis of the leak indications displayed by valgrind
   is difficult as libfuse contains some small memory leaks (which do
   not accumulate over time). We cannot (easily) fix these memory
   leaks inside libfuse.

   * Avoid putting debug messages into the code. You may use fprintf
   for debugging purposes but they should all go away in the final
   version of the code. Using gdb is more professional, though.

   * You MUST NOT fail with exit(1) in case of an error. All the
   functions you have to implement have ways to indicated failure
   cases. Use these, mapping your internal errors intelligently onto
   the POSIX error conditions.

   * And of course: your code MUST NOT SEGFAULT!

   It is reasonable to proceed in the following order:

   (1)   Design and implement a mechanism that initializes a filesystem
         whenever the memory space is fresh. That mechanism can be
         implemented in the form of a filesystem handle into which the
         filesystem raw memory pointer and sizes are translated.
         Check that the filesystem does not get reinitialized at mount
         time if you initialized it once and unmounted it but that all
         pieces of information (in the handle) get read back correctly
         from the backup-file. 

   (2)   Design and implement functions to find and allocate free memory
         regions inside the filesystem memory space. There need to be 
         functions to free these regions again, too. Any "global" variable
         goes into the handle structure the mechanism designed at step (1) 
         provides.

   (3)   Carefully design a data structure able to represent all the
         pieces of information that are needed for files and
         (sub-)directories.  You need to store the location of the
         root directory in a "global" variable that, again, goes into the 
         handle designed at step (1).
          
   (4)   Write __myfs_getattr_implem and debug it thoroughly, as best as
         you can with a filesystem that is reduced to one
         function. Writing this function will make you write helper
         functions to traverse paths, following the appropriate
         subdirectories inside the file system. Strive for modularity for
         these filesystem traversal functions.

   (5)   Design and implement __myfs_readdir_implem. You cannot test it
         besides by listing your root directory with ls -la and looking
         at the date of last access/modification of the directory (.). 
         Be sure to understand the signature of that function and use
         caution not to provoke segfaults nor to leak memory.

   (6)   Design and implement __myfs_mknod_implem. You can now touch files 
         with 

         touch foo

         and check that they start to exist (with the appropriate
         access/modification times) with ls -la.

   (7)   Design and implement __myfs_mkdir_implem. Test as above.

   (8)   Design and implement __myfs_truncate_implem. You can now 
         create files filled with zeros:

         truncate -s 1024 foo

   (9)   Design and implement __myfs_statfs_implem. Test by running
         df before and after the truncation of a file to various lengths. 
         The free "disk" space must change accordingly.

   (10)  Design, implement and test __myfs_utimens_implem. You can now 
         touch files at different dates (in the past, in the future).

   (11)  Design and implement __myfs_open_implem. The function can 
         only be tested once __myfs_read_implem and __myfs_write_implem are
         implemented.

   (12)  Design, implement and test __myfs_read_implem and
         __myfs_write_implem. You can now write to files and read the data 
         back:

         echo "Hello world" > foo
         echo "Hallo ihr da" >> foo
         cat foo

         Be sure to test the case when you unmount and remount the
         filesystem: the files must still be there, contain the same
         information and have the same access and/or modification
         times.

   (13)  Design, implement and test __myfs_unlink_implem. You can now
         remove files.

   (14)  Design, implement and test __myfs_unlink_implem. You can now
         remove directories.

   (15)  Design, implement and test __myfs_rename_implem. This function
         is extremely complicated to implement. Be sure to cover all 
         cases that are documented in man 2 rename. The case when the 
         new path exists already is really hard to implement. Be sure to 
         never leave the filessystem in a bad state! Test thoroughly 
         using mv on (filled and empty) directories and files onto 
         inexistant and already existing directories and files.

   (16)  Design, implement and test any function that your instructor
         might have left out from this list. There are 13 functions 
         __myfs_XXX_implem you have to write.

   (17)  Go over all functions again, testing them one-by-one, trying
         to exercise all special conditions (error conditions): set
         breakpoints in gdb and use a sequence of bash commands inside
         your mounted filesystem to trigger these special cases. Be
         sure to cover all funny cases that arise when the filesystem
         is full but files are supposed to get written to or truncated
         to longer length. There must not be any segfault; the user
         space program using your filesystem just has to report an
         error. Also be sure to unmount and remount your filesystem,
         in order to be sure that it contents do not change by
         unmounting and remounting. Try to mount two of your
         filesystems at different places and copy and move (rename!)
         (heavy) files (your favorite movie or song, an image of a cat
         etc.) from one mount-point to the other. None of the two FUSE
         processes must provoke errors. Find ways to test the case
         when files have holes as the process that wrote them seeked
         beyond the end of the file several times. Your filesystem must
         support these operations at least by making the holes explicit 
         zeros (use dd to test this aspect).

   (18)  Run some heavy testing: copy your favorite movie into your
         filesystem and try to watch it out of the filesystem.

*/

/* Helper types and functions */

/* YOUR HELPER FUNCTIONS GO HERE */

/* End of helper functions */

/* Implements an emulation of the stat system call on the filesystem 
   of size fssize pointed to by fsptr. 
   
   If path can be followed and describes a file or directory 
   that exists and is accessable, the access information is 
   put into stbuf. 

   On success, 0 is returned. On failure, -1 is returned and 
   the appropriate error code is put into *errnoptr.

   man 2 stat documents all possible error codes and gives more detail
   on what fields of stbuf need to be filled in. Essentially, only the
   following fields need to be supported:

   st_uid      the value passed in argument
   st_gid      the value passed in argument
   st_mode     (as fixed values S_IFDIR | 0755 for directories,
                                S_IFREG | 0755 for files)
   st_nlink    (as many as there are subdirectories (not files) for directories
                (including . and ..),
                1 for files)
   st_size     (supported only for files, where it is the real file size)
   st_atim
   st_mtim

*/
int __myfs_getattr_implem(void *fsptr, size_t fssize, int *errnoptr,
                          uid_t uid, gid_t gid,
                          const char *path, struct stat *stbuf) {
  handle_t* h = get_handle(fsptr, fssize);
  if(h == NULL){
    return NULL;
  }
  tree_node * root = (tree_node*) h->root;
  //Account for more errors here
  
  tree_node* found = find_node(path, root, 0);
  //STUB
  if(found == -1){
    *errnoptr = ENOTDIR;
    return -1;
  }
  if(found == -2){
    *errnoptr = ENOENT;
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
  else{
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2 + found->nlinks;
      }
  }
  return 0;
}
  /* STUB */

/* Implements an emulation of the readdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   If path can be followed and describes a directory that exists and
   is accessable, the names of the subdirectories and files 
   contained in that directory are output into *namesptr. The . and ..
   directories must not be included in that listing.

   If it needs to output file and subdirectory names, the function
   starts by allocating (with calloc) an array of pointers to
   characters of the right size (n entries for n names). Sets
   *namesptr to that pointer. It then goes over all entries
   in that array and allocates, for each of them an array of
   characters of the right size (to hold the i-th name, together 
   with the appropriate '\0' terminator). It puts the pointer
   into that i-th array entry and fills the allocated array
   of characters with the appropriate name. The calling function
   will call free on each of the entries of *namesptr and 
   on *namesptr.

   The function returns the number of names that have been 
   put into namesptr. 

   If no name needs to be reported because the directory does
   not contain any file or subdirectory besides . and .., 0 is 
   returned and no allocation takes place.

   On failure, -1 is returned and the *errnoptr is set to 
   the appropriate error code. 

   The error codes are documented in man 2 readdir.

   In the case memory allocation with malloc/calloc fails, failure is
   indicated by returning -1 and setting *errnoptr to EINVAL.

*/
int __myfs_readdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, char ***namesptr) {
  tree_node *dir;
  int numChildren;
  handle_t* h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errno = EFAULT;
    return -1;
  }
  dir = find_node(path, (tree_node*) h->root, 0);
  if((dir == -1) || (dir->type != 2)) {
    *errnoptr = ENOTDIR;
    return -1;
  }
  if(dir == -2){
    *errnoptr = ENOENT;
    return -1;
  }
  if(dir->numChildren == 0){
    return 0;
  }

  //update timestamp for directory, but not modified time
  timestamp(dir,0);
  namesptr = (char*)malloc((dir->numChildren) * sizeof(void *));
  for(int i = 0; i < dir->numChildren; i++){
    *namesptr = (char*)calloc(sizeof(char), strlen(dir->children[i]));
    strcpy(dir->children[i], *namesptr[i]);
   }
  return dir->numChildren;
}

/* Implements an emulation of the mknod system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the creation of regular files.

   If a file gets created, it is of size zero and has default
   ownership and mode bits.

   The call creates the file indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mknod.

*/
int __myfs_mknod_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {

  int i, path_length, file_index, file_length;
  tree_node *parent_node = NULL;
  handle_t *h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  parent_node = getParent(h, path);
  if(parent_node == -1){
    *errnoptr = ENOTDIR;
    return -1;
  }
  if(parent_node == -2){
    *errnoptr = ENOENT;
    return -1;
  }
  path_length = strlen(path) - 1;
  for(i = path_length; path[i] != "/"; i--){
  }
  file_index = i;
  file_length = path_length - file_index + 1;
  
  char* fileName = (char*)malloc(sizeof(char) * (file_length));
  fileName[file_length] = "\0";
  for(i = file_index, int j 0; i < strlen(path); i++, j++){
    fileName[j] = path[i];
  }
  tree_node *newNode = (tree_node*)malloc(sizeof(tree_node));
  *newNode->name = fileName;
  newNode->parent = parent_node;
  newNode->size = (size_t) 0;
  newNode->type = 1;
  //Add new access and modify times for node, set to current time
  timestamp(newNode, 1);
  //Parent directory has been accessed and modified as well
  timestamp(parent_node, 1);
  int fail = add_tree_node(newNode, path, h);
  if(!fail)
    {return 0;}
  else{
    return -1;
  }
}

/* Implements an emulation of the unlink system call for regular files
   on the filesystem of size fssize pointed to by fsptr.

   This function is called only for the deletion of regular files.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 unlink.

*/
int __myfs_unlink_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the rmdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call deletes the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The function call must fail when the directory indicated by path is
   not empty (if there are files or subdirectories other than . and ..).

   The error codes are documented in man 2 rmdir.

*/
int __myfs_rmdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the mkdir system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call creates the directory indicated by path.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 mkdir.

*/
int __myfs_mkdir_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path) {
  handle_t *h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  tree_node * parent_node = getParent(h, path);
  if(parent_node == -1){
    *errnoptr = ENOTDIR;
    return -1;
  }
  if(parent_node == -2){
    *errnoptr = ENOENT;
    return -1;
  }
  for(int i = strlen(path) -1; path[i] != "/"; i--){}
  int dirIndex = i;
  int dirNameLen = strlen(path) - i;
  char *dirName = (char*) malloc(sizeof(char) * dirNameLen);
  dirName[dirNameLen] = "\0";
  for(i = dirIndex, int j = 0; i < strlen(path); i++, j++){
    dirName[j] = path[i];
  }
  tree_node* newDir = (tree_node*) malloc(sizeof(tree_node));
  *newDir->name = dirName;
  newDir->type = 2;
  newDir->parent = parent_node;
  newDir->size = 0;
  newDir->numChildren = 0;
  timestamp(newDir, 1);
  timestamp(parent_node, 1);
  int fail = add_tree_node(newDir, path, h);
  if(!fail){
    return 0;
  }
  else{    
  return -1;
  }
}

/* Implements an emulation of the rename system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call moves the file or directory indicated by from to to.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   Caution: the function does more than what is hinted to by its name.
   In cases the from and to paths differ, the file is moved out of 
   the from path and added to the to path.

   The error codes are documented in man 2 rename.

*/
int __myfs_rename_implem(void *fsptr, size_t fssize, int *errnoptr,
                         const char *from, const char *to) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the truncate system call on the filesystem 
   of size fssize pointed to by fsptr. 

   The call changes the size of the file indicated by path to offset
   bytes.

   When the file becomes smaller due to the call, the extending bytes are
   removed. When it becomes larger, zeros are appended.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 truncate.

*/
int __myfs_truncate_implem(void *fsptr, size_t fssize, int *errnoptr,
                           const char *path, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the open system call on the filesystem 
   of size fssize pointed to by fsptr, without actually performing the opening
   of the file (no file descriptor is returned).

   The call just checks if the file (or directory) indicated by path
   can be accessed, i.e. if the path can be followed to an existing
   object for which the access rights are granted.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The two only interesting error codes are 

   * EFAULT: the filesystem is in a bad state, we can't do anything

   * ENOENT: the file that we are supposed to open doesn't exist (or a
             subpath).

   It is possible to restrict ourselves to only these two error
   conditions. It is also possible to implement more detailed error
   condition answers.

   The error codes are documented in man 2 open.

*/
int __myfs_open_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the read system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call copies up to size bytes from the file indicated by 
   path into the buffer, starting to read at offset. See the man page
   for read for the details when offset is beyond the end of the file etc.
   
   On success, the appropriate number of bytes read into the buffer is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 read.

*/
int __myfs_read_implem(void *fsptr, size_t fssize, int *errnoptr,
                       const char *path, char *buf, size_t size, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the write system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call copies up to size bytes to the file indicated by 
   path into the buffer, starting to write at offset. See the man page
   for write for the details when offset is beyond the end of the file etc.
   
   On success, the appropriate number of bytes written into the file is
   returned. The value zero is returned on an end-of-file condition.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 write.

*/
int __myfs_write_implem(void *fsptr, size_t fssize, int *errnoptr,
                        const char *path, const char *buf, size_t size, off_t offset) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the utimensat system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call changes the access and modification times of the file
   or directory indicated by path to the values in ts.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 utimensat.

*/
int __myfs_utimens_implem(void *fsptr, size_t fssize, int *errnoptr,
                          const char *path, const struct timespec ts[2]) {
  /* STUB */
  return -1;
}

/* Implements an emulation of the statfs system call on the filesystem 
   of size fssize pointed to by fsptr.

   The call gets information of the filesystem usage and puts in 
   into stbuf.

   On success, 0 is returned.

   On failure, -1 is returned and *errnoptr is set appropriately.

   The error codes are documented in man 2 statfs.

   Essentially, only the following fields of struct statvfs need to be
   supported:

   f_bsize   fill with what you call a block (typically 1024 bytes)
   f_blocks  fill with the total number of blocks in the filesystem
   f_bfree   fill with the free number of blocks in the filesystem
   f_bavail  fill with same value as f_bfree
   f_namemax fill with your maximum file/directory name, if your
             filesystem has such a maximum

*/
int __myfs_statfs_implem(void *fsptr, size_t fssize, int *errnoptr,
                         struct statvfs* stbuf) {
  handle_t*h = get_handle(fsptr, fssize);
  if(h == NULL){
    *errnoptr = EFAULT;
    return -1;
  }
  //Set to zeros
  size_t total_mem = total_free_space(h);
  memset(stbuf, 0, sizeof(statvfs));
  stbuf->f_bsize = (__fsword_t) 1024;
  stbuf->blocks = ((fsblkcnt_t) (fssize / 1024));
  stbuf->bfree = ((fsblkcnt_t) (total_mem / 1024));
  stbuf->bavail = ((fsblkcnt_t) (total_mem / 1024));
  stbuf->f_namemax = 256;
  return 0;
}





