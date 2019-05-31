#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/*
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/*
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct semaphore *intersectionSem;
static struct lock *intersection_lk;
static int in_intersection;
static int intersection_limit;
static struct cv *N;
static struct cv *W;
static struct cv *S;
static struct cv *E;
static int origins[4] = {0, 0, 0, 0};
static int destinations[4] = {0, 0, 0, 0};


/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */

bool
is_safe(Direction origin) {
  if (origins[origin] > 0 && in_intersection <intersection_limit) {
    return true;
  }
  if (in_intersection == 0) {
    return true;
  }
  return false;
}

void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  intersection_lk = lock_create("intersection_lk");
  if (intersection_lk == NULL) {
    panic("could not create intersection lock");
  }


  static struct cv control_varibles[4];
  N = cv_create("N");
  W = cv_create("W");
  S = cv_create("S");
  E = cv_create("E");

  control_varibles[0] = *N;
  control_varibles[1] = *W;
  control_varibles[2] = *S;
  control_varibles[3] = *E;
  if (N == NULL) {
    panic("could not create N control variable");
  }
  if (W == NULL) {
    panic("could not create N control variable");
  }
  if (S == NULL) {
    panic("could not create N control variable");
  }
  if (E == NULL) {
    panic("could not create N control variable");
  }

  in_intersection = 0;
  intersection_limit = 10;

  intersectionSem = sem_create("intersectionSem",1);
  if (intersectionSem == NULL) {
    panic("could not create intersection semaphore");
  }
  return;
}

/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersectionSem != NULL);
  sem_destroy(intersectionSem);

  KASSERT(intersection_lk != NULL);
  lock_destroy(intersection_lk);
  KASSERT(N != NULL);
  cv_destroy(N);
  KASSERT(W != NULL);
  cv_destroy(W);
  KASSERT(S != NULL);
  cv_destroy(S);
  KASSERT(E != NULL);
  cv_destroy(E);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination)
{
  /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);

  lock_acquire(intersection_lk);

  if (!is_safe(origin)) {
    cv_wait(control_varibles[origin], intersection_lk);
  }
  origins[origins]++;
  in_intersection++;

  lock_release(intersection_lk);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination)
{
  /* replace this default implementation with your own implementation */
  // (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */
  // KASSERT(intersectionSem != NULL);
  // V(intersectionSem);
  lock_acquire(intersection_lk);

  in_intersection--;
  origins[origin]--;
  if (in_intersection == 0) {
    cv_broadcast(control_varibles[origin + 1], intersection_lk);
  }

  lock_release(intersection_lk);

}
