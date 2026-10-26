#include "common.h"

#define NCHILD 4

int main(void) {
  int pids[NCHILD];
  int bad = 0, quantum_mask = 0;
  ASSERT(setickets(3) == 0, "setickets should accept positive ticket count");
  ASSERT(setickets(0) == -1, "setickets should reject zero");
  ASSERT(time_rdtime() != 0, "time_rdtime should return hardware time");
  for (int i = 0; i < NCHILD; i++) {
    int pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
      unsigned long end = deadline_after(140);
      burn_until(end);
      exit(i + 1);
    }
    pids[i] = pid;
  }
  for (int t = 0; t < 150; t++) {
    pause(1);
    for (int i = 0; i < NCHILD; i++) {
      int cpu = 0, slice = 0;
      if (schedstat(pids[i], &cpu, &slice) == 0 && cpu > 0 && slice > 0) {
        bad++;
        if (slice == 2) quantum_mask |= 1;
        if (slice == 4) quantum_mask |= 2;
        if (slice == 8) quantum_mask |= 4;
        if (slice == 16) quantum_mask |= 8;
      }
    }
  }
  for (int i = 0; i < NCHILD; i++) {
    int status = 0;
    ASSERT(wait(&status) > 0, "wait child");
    ASSERT(status >= 1 && status <= NCHILD, "child status preserved");
  }
  ASSERT(bad > 0, "timer preemption statistics were not observable");
  ASSERT(quantum_mask == 15, "four MLFQ quantum tiers were not observed");
  printf("RESULT mlfq_observations = %d\n", bad);
  printf("RESULT mlfq_quantum_mask = %d\n", quantum_mask);
  PASS("mlfq timer accounting and process lifecycle");
}
