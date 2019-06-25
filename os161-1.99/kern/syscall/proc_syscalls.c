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
#include <vfs.h>
#incldue <machine/trapframe.h>
#include <limits.h>
#include "opt-A2.h"

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

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
  *retval = 1;
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

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
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
  if (child == NULL) {
      return ENOMEM;
  }

  child->parent = curproc->pid;
  child->pid = 117;

  int res;

  int copy_check = as_copy(curproc->p_addrspace, &child->p_addrspace);
  if (copy_check) {
    proc_destroy(child);
    return ENOMEM;
  }

  tf_temp = kmalloc(sizeof(*tf_temp));
  if (tf_temp == NULL) {
      proc_destroy(child);
      return ENOMEM;
  }

  memcpy(tf_temp, tf, sizeof(*tf_temp));

  spinlock_acquire(&child->p_lock);

  curproc_setas(child->p_addrspace);

  spinlock_release(&child->p_lock);

  res = thread_fork(curthread->t_name, child, (void *)&enter_forked_process, (void *)tf_temp, 0);
  if (res) {
      kfree(tf_temp);
      proc_destroy(child);
      return ENOMEM;
  }
  *ret = child->pid;

  return 0;


}
