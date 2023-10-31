#include "wut.h"

#include <assert.h> // assert
#include <errno.h> // errno
#include <stddef.h> // NULL
#include <stdio.h> // perror
#include <stdlib.h> // reallocarray
#include <sys/mman.h> // mmap, munmap
#include <sys/signal.h> // SIGSTKSZ
#include <sys/queue.h> // TAILQ_*
#include <sys/ucontext.h>
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <valgrind/valgrind.h> // VALGRIND_STACK_REGISTER


/*
  Thread Ready Queue
*/
typedef struct THREAD {
  int thread_id_;
  TAILQ_ENTRY(THREAD) pointers;
} THREAD;
TAILQ_HEAD(Q_head, THREAD) Q_head;

/*
  Thread Control Block 
  id_: thread id 
  status_: running status num, -1 for running
  join_: join status, 0 for off, 1 for on
  join_from_: id of the thread joined by
  join_to_: id to the thread join to
  block_: 0 for off, 1 for on
  enable_: flag for creating (used for join), 1 for yes, 0 for no
  thread_in_q_: thread in queue
  stack_:
  ucontext_: next context
*/
typedef struct TCB {
  int id_;
  int status_; 
  int join_;
  int join_from_;
  int join_to_;
  int block_;
  int enable_; 
  THREAD* thread_in_q_;
  char* stack_; 
  ucontext_t* ucontext_;
} TCB;

/*
  Default Arrary Properties
  TCB_ARRAY: global array stores the thread info
  MAX_THREAD_NUM: max number of the threads(size of TCB_ARRAY)
  THREAD_NUM: # of threads have so far
  CURRENT_RUNNING_THREAD_ID: id of current thread
  EXIT_CONTEXT: global context for all thread when they are ready to terminate
*/
TCB** TCB_ARRAY = NULL;
int MAX_THREAD_NUM = 256;
int THREAD_NUM = 0;
int CURRENT_RUNNING_THREAD_ID = 0; 
ucontext_t EXIT_CONTEXT;


static void die(const char* message) {
  int err = errno;
  perror(message);
  exit(err);
}

static char* new_stack(void) {
  char* stack = mmap(
    NULL,
    SIGSTKSZ,
    PROT_READ | PROT_WRITE | PROT_EXEC,
    MAP_ANONYMOUS | MAP_PRIVATE,
    -1,
    0
  );
  if (stack == MAP_FAILED) {
    die("mmap stack failed");
  }
  VALGRIND_STACK_REGISTER(stack, stack + SIGSTKSZ);
  return stack;
}

static void delete_stack(char* stack) {
  if (munmap(stack, SIGSTKSZ) == -1) {
    die("munmap stack failed");
  }
}

/* 
  function to clear all the array and queue
*/
void clear_global() {
  for (int i = 0; i < MAX_THREAD_NUM; ++ i) {
    TCB* thread = TCB_ARRAY[i];
    if (thread) {
      if (thread->thread_in_q_) {
        free(thread->thread_in_q_);
        thread->thread_in_q_ = NULL;
      }
      if (thread->stack_) {
        delete_stack(thread->stack_);
      }
      if (thread->ucontext_) {
        free(thread->ucontext_);
        thread->ucontext_ = NULL;
      }
    }
    free(TCB_ARRAY[i]);
    TCB_ARRAY[i] = NULL;
  }
  free(TCB_ARRAY);
  TCB_ARRAY = NULL;
}

/*
  init the array and thread 0
*/
void wut_init() {
  TAILQ_INIT(&Q_head);
  TCB_ARRAY = malloc(MAX_THREAD_NUM * sizeof(TCB*));
  for (int i = 0; i < MAX_THREAD_NUM; ++ i) {
    TCB_ARRAY[i] = NULL;
  }

  //thread 0, set the default info and set uc_link to EXIT_CONTEXT
  TCB* thread_0 = malloc(sizeof(TCB));
  thread_0->id_ = 0;
  thread_0->join_ = 0;
  thread_0->join_from_ = -1;
  thread_0->join_to_ = -1;
  thread_0->status_ = -1;
  thread_0->block_ = 0;
  thread_0->enable_ = 1;
  thread_0->thread_in_q_ = NULL;
  ucontext_t* ucontext = malloc(sizeof(ucontext_t));
  getcontext(ucontext);
  thread_0->ucontext_ = ucontext;
  
  TCB_ARRAY[0] = thread_0;

  CURRENT_RUNNING_THREAD_ID = 0;

  char* stack = new_stack();
  thread_0->ucontext_->uc_link = &EXIT_CONTEXT;
  getcontext(&EXIT_CONTEXT);
  EXIT_CONTEXT.uc_stack.ss_sp = stack;
  EXIT_CONTEXT.uc_stack.ss_size = SIGSTKSZ;
  
  makecontext(&EXIT_CONTEXT, (void (*)())wut_exit, 1, 0);
  ++ THREAD_NUM;
}

int wut_id() {
  return CURRENT_RUNNING_THREAD_ID;
}

/*
  resize array when it is full
*/
void resize() {
  MAX_THREAD_NUM *= 2;
  TCB** new_tcb_array = reallocarray(TCB_ARRAY, MAX_THREAD_NUM, sizeof(TCB*));
  TCB_ARRAY = new_tcb_array;
  for (int i = MAX_THREAD_NUM / 2; i < MAX_THREAD_NUM; ++ i) {
    TCB_ARRAY[i] = NULL;
  }
}

/*
  create new thread, set the default info and uc_link, insert to queue
*/
int wut_create(void (*run)(void)) {
  if (THREAD_NUM >= MAX_THREAD_NUM) {
    resize();
  }
  //create a new thread, set uc_link to EXIT_CONTEXT
  TCB* new_thread = malloc(sizeof(TCB));

  //initialize the thread info
  ucontext_t* ucontext = malloc(sizeof(ucontext_t));
  getcontext(ucontext);
  char* stack = new_stack();
  ucontext->uc_stack.ss_sp = stack;
  ucontext->uc_stack.ss_size = SIGSTKSZ;
  
  new_thread->ucontext_ = ucontext;
  new_thread->ucontext_->uc_link = &EXIT_CONTEXT;
  makecontext(ucontext, run, 0);
  
  new_thread->join_ = 0;
  new_thread->join_from_ = -1;
  new_thread->join_to_ = -1;
  new_thread->stack_ = stack;
  new_thread->status_ = -1;
  new_thread->block_ = 0;
  new_thread->enable_= 1;
 
  //find the thread id (refer to handout)
  for (int i = 0; i < MAX_THREAD_NUM; ++ i) {
    if((TCB_ARRAY[i] == NULL || (TCB_ARRAY[i]->status_ != -1 && TCB_ARRAY[i]->enable_ == 1))) {
      new_thread->id_ = i;
      TCB_ARRAY[i] = new_thread;
      ++ THREAD_NUM;
      break;
    }
  }

  //add the new thread to ready queue
  THREAD* new_thread_in_q = malloc(sizeof(THREAD));
  new_thread_in_q->thread_id_ = new_thread->id_;
  TAILQ_INSERT_TAIL(&Q_head, new_thread_in_q, pointers);
  new_thread->thread_in_q_ = new_thread_in_q;

  return new_thread->id_;
}

/*
  remove the thread (TCB_ARRAY & queue)
*/
int wut_cancel(int id) {
  if (id < 0 || id == CURRENT_RUNNING_THREAD_ID || id > MAX_THREAD_NUM){
    return -1;
  }
  
  TCB* cancel_thread = TCB_ARRAY[id];
  if(cancel_thread == NULL || cancel_thread->status_ != -1) {
    return -1;
  }

  cancel_thread->status_ = 128;

  //if the thread is joining to other
  if (cancel_thread->block_ == 1) {
    cancel_thread->block_ = 0;
    TCB* the_thread = TCB_ARRAY[cancel_thread->join_to_];
    the_thread->join_from_ = -1;
    the_thread->join_ = 0;
  } else {
    //remove from queue
    delete_stack(cancel_thread->stack_);
    free(cancel_thread->ucontext_ );
    cancel_thread->ucontext_ = NULL;
    TAILQ_REMOVE(&Q_head, cancel_thread->thread_in_q_, pointers);
    free(cancel_thread->thread_in_q_);
    cancel_thread->thread_in_q_ = NULL;
  }
  
  return 0;
}

/*
  block the current thread, run next until get the thread that is being joined
  put the block thread back to queue
*/
int wut_join(int id) {
  if (id < 0 || CURRENT_RUNNING_THREAD_ID == id || id > MAX_THREAD_NUM) {
    return -1;
  }
  if (TCB_ARRAY[id] == NULL || TCB_ARRAY[id]->status_ != - 1 || TCB_ARRAY[id]->join_ == 1) {
    return TCB_ARRAY[id]->status_;
  }

  //set the join info of the thread
  TCB* current_thread = TCB_ARRAY[CURRENT_RUNNING_THREAD_ID];
  current_thread->block_ = 1;
  current_thread->join_to_ = id;

  TCB* join_thread = TCB_ARRAY[id];
  join_thread->join_ = 1;
  join_thread->enable_= 0;
  join_thread->join_from_ = CURRENT_RUNNING_THREAD_ID;
  
  //move to next thread
  THREAD* next_thread_in_q = TAILQ_FIRST(&Q_head);
  TAILQ_REMOVE(&Q_head, next_thread_in_q, pointers);
  TCB* next_thread = TCB_ARRAY[next_thread_in_q->thread_id_];
  CURRENT_RUNNING_THREAD_ID = next_thread->id_;
  swapcontext(current_thread->ucontext_, next_thread->ucontext_);

  join_thread->enable_ = 1;
  return join_thread->status_;
}

/*
  move the current thread to the end of queue and run next one until
  we get back to current thread (if it still exisit)
*/
int wut_yield() {
  if TAILQ_EMPTY(&Q_head) {
    return -1;
  }

  //move the current thread to the end of queue
  TCB* current_thread = TCB_ARRAY[CURRENT_RUNNING_THREAD_ID];

  THREAD* current_thread_in_q = malloc(sizeof(THREAD));
  int current_thread_id = current_thread->id_;
  current_thread_in_q->thread_id_ = current_thread_id;
  TAILQ_INSERT_TAIL(&Q_head, current_thread_in_q, pointers);

  //start running next thread
  THREAD* next_thread_in_q = TAILQ_FIRST(&Q_head);
  TAILQ_REMOVE(&Q_head, next_thread_in_q, pointers);
  CURRENT_RUNNING_THREAD_ID = next_thread_in_q->thread_id_;

  TCB* next_thread = TCB_ARRAY[next_thread_in_q->thread_id_];

  swapcontext(current_thread->ucontext_, next_thread->ucontext_);
  
  return 0;
}

/*
  set the exit status and move to next thread, clear all otherwise
*/
void wut_exit(int status) {
  TCB* current_thread = TCB_ARRAY[CURRENT_RUNNING_THREAD_ID];
  status &= 0xFF;
  
  if (current_thread->status_ == -1) {
    current_thread->status_ = status;
  }

  //if the thread is being joined, the joinning thread will be put back to queue 
  if (current_thread->join_ == 1) {
    current_thread->join_ = 0;
    TCB* current_blocked_thread = TCB_ARRAY[current_thread->join_from_];
    current_thread->join_from_ = -1;
    THREAD* current_blocked_thread_in_q = malloc(sizeof(THREAD));
    current_blocked_thread_in_q->thread_id_ = current_blocked_thread->id_;
    TAILQ_INSERT_TAIL(&Q_head, current_blocked_thread_in_q, pointers);
  }
  
  //if there is thread in queue, run next, exit and clear otherwise
  if (!TAILQ_EMPTY(&Q_head)) {
    THREAD* next_thread_in_q = TAILQ_FIRST(&Q_head);
    TAILQ_REMOVE(&Q_head, next_thread_in_q, pointers);
    CURRENT_RUNNING_THREAD_ID = next_thread_in_q->thread_id_;
    TCB* next_thread = TCB_ARRAY[next_thread_in_q->thread_id_];
    setcontext(next_thread->ucontext_);
  } else {
    clear_global();
  }
}
