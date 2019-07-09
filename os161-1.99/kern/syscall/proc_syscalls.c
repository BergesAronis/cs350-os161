#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <synch.h>
#include <vfs.h>
#include <machine/trapframe.h>
#include <limits.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */
  // :)

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  if (p->parent) {
      lock_acquire(p->parent->lk);
      lock_acquire(p->lk);
      p->exit_code = exitcode;
      p->killed = true;
      lock_release(p->lk);
      cv_signal(p->terminating, p->parent->lk);
      lock_release(p->parent->lk);

      /* detach this thread from its process */
      /* note: curproc cannot be used after this call */
      proc_remthread(curthread);
      thread_exit();
  }

      DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

      KASSERT(curproc->p_addrspace != NULL);
      as_deactivate();
      /*
       * clear p_addrspace before calling as_destroy. Otherwise if
       * as_destroy sleeps (which is quite possible) when we
       * come back we'll be calling as_activate on a
       * half-destroyed address space. This tends to be
       * messily fatal.
       */
      as = curproc_setas(NULL);
      as_destroy(as);

      /* detach this thread from its process */
      /* note: curproc cannot be used after this call */
      proc_remthread(curthread);

      /* if this is the last user process in the system, proc_destroy()
         will wake up the kernel menu thread */
      proc_destroy(p);

      thread_exit();

  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  *retval = curproc->pid;
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

    lock_acquire(curproc->lk);
    bool isChild = false;
    for (unsigned int i = 0; i < array_num(curproc->children); ++i) {
        struct proc *child = array_get(curproc->children, i);
        lock_acquire(child->lk);
        if (pid == child->pid) {
            isChild = true;
            if (child->parent->pid != curproc->pid) {
                lock_release(child->lk);
                lock_release(curproc->lk);
                return ECHILD;
            }
            lock_release(child->lk);
            break;
        }
        lock_release(child->lk);
    }
    if (isChild == false) {
        lock_release(curproc->lk);
        *retval = -1;
        return ESRCH;
    }


    for (unsigned int i = 0; i < array_num(curproc->children); ++i) {
        struct proc *child = array_get(curproc->children, i);
        lock_acquire(child->lk);
        if (pid == child->pid) {
            if (child->killed) {
                exitstatus = _MKWAIT_EXIT(child->exit_code);
                lock_release(child->lk);
                break;
            }
            while(!child->killed) {
                lock_release(child->lk);
                cv_wait(child->terminating, curproc->lk);
            }
            exitstatus = _MKWAIT_EXIT(child->exit_code);
            break;
        }
        lock_release(child->lk);
    }

    lock_release(curproc->lk);

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

int
sys_fork(struct trapframe *tf, pid_t *ret) {
  struct proc *child = proc_create_runprogram(curproc->p_name);
  struct trapframe *tf_temp;
  struct addrspace *as_temp = NULL;
  if (child == NULL) {
      return ENOMEM;
  }

  child->parent = curproc;
  child->exit_code = -1;
  array_add(curproc->children, child, NULL);

  int res;

  int copy_check = as_copy(curproc->p_addrspace, &as_temp);
  if (copy_check) {
    proc_destroy(child);
    return ENOMEM;
  }


  tf_temp = kmalloc(sizeof(struct trapframe));
  if (tf_temp == NULL) {
      proc_destroy(child);
      return ENOMEM;
  }


  memcpy(tf_temp, tf, sizeof(struct trapframe));

  spinlock_acquire(&child->p_lock);

  child->p_addrspace = as_temp;

  spinlock_release(&child->p_lock);

  res = thread_fork(curthread->t_name, child, (void *)&enter_forked_process, tf_temp, 0);
  if (res) {
      kfree(tf_temp);
      proc_destroy(child);
      return ENOMEM;
  }
  *ret = child->pid;

  return 0;


}

int
sys_execv(char *progname, char **args) {
    struct addrspace *as;
    struct vnode *v;
    vaddr_t entrypoint, stackptr;
    int result;

    // Count the number of arguments
    int args_many = 0;
    while (args[args_many] != NULL) {
        args_many++;
    }

    size_t arg_size = sizeof(char *) * (args_many + 1);
    char ** arg_kern = kmalloc(arg_size);

    arg_kern[args_many] = NULL;

    for (int i = 0; i < args_many; ++i) {
        size_t argument_size = sizeof(char) * (strlen(args[i]) + 1);
        arg_kern[i] = kmalloc(argument_size);
        copyin((const_userptr_t) args[i], (void *) arg_kern[i], argument_size);
    }

    // Copy the program path into the kernel
    size_t progname_size = sizeof(char) * (strlen(progname) + 1);
    char * progname_kern = kmalloc(progname_size);
    copyin((const_userptr_t) progname, (void *) progname_kern, progname_size);

    /* Open the file. */
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if (result) {
        return result;
    }


    /* Create a new address space. */
    as = as_create();
    if (as ==NULL) {
        vfs_close(v);
        return ENOMEM;
    }

    /* Switch to it and activate it. */
    struct addrspace * elder = curproc_setas(as);
    as_activate();

    /* Load the executable. */
    result = load_elf(v, &entrypoint);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        vfs_close(v);
        return result;
    }

    /* Done with the file now. */
    vfs_close(v);

    /* Define the user stack in the address space */
    result = as_define_stack(as, &stackptr);
    if (result) {
        /* p_addrspace will go away when curproc is destroyed */
        return result;
    }


    // Copy some arguments
    vaddr_t *new_arguments = kmalloc(sizeof(vaddr_t) * (args_many + 1));
    new_arguments[args_many] = (vaddr_t) NULL;
    size_t ptr_size = sizeof(vaddr_t);
    vaddr_t new_stack = stackptr;
    new_stack -= ptr_size;
    copyout((void *) &new_arguments[args_many], (userptr_t) new_stack, ptr_size);
    new_stack += ptr_size;

    for (int i = (args_many - 1); i >= 0; --i) {
        size_t new_arg_len = ROUNDUP(strlen(arg_kern[i]) + 1, 8);
        size_t new_arg_size = sizeof(char) * new_arg_len;
        new_stack -= new_arg_size;
        copyoutstr((void *) arg_kern[i], (userptr_t) new_stack, new_arg_len, (size_t *) new_arg_size);
        new_arguments[i] = new_stack;
        size_t ptr_size = sizeof(vaddr_t);
        new_stack -= ptr_size;
        copyout((void *) &new_arguments[i], (userptr_t) new_stack, ptr_size);
        new_stack += ptr_size;
    }

    for (int i = args_many; i >= 0; --i) {
    }

    // Delete old address space
    as_destroy(elder);
    kfree(progname_kern);
    for (int i = 0; i < args_many + 1; ++i) {
        kfree(arg_kern[i]);
    }
    kfree(arg_kern);

    /* Warp to user mode. */
    enter_new_process(args_many/*argc*/, (userptr_t) new_stack /*userspace addr of argv*/,
                      ROUNDUP(new_stack, 8), entrypoint);

    /* enter_new_process does not return. */
    panic("enter_new_process returned\n");
    return EINVAL;
}
