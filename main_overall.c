#include <uk/arch/atomic.h>
#include <uk/spinlock.h>
#include <uk/mutex.h>
#include <uk/semaphore.h>
#include <uk/rwlock.h>
#include <uk/plat/lcpu.h>
#include <uk/sched.h>
#include <uk/schedcoop.h>
#include <stdlib.h>
#include <uk/vmem.h>
#include <string.h>
#include <stdint.h>

#define STACK_SIZE (4096)
#define ALLOCATION_ROUNDS 10
#define ROUNDS 200000
#define N_CPUS 16

unsigned long count = 0;
unsigned int pcnt;
unsigned int completed1 = 0;
unsigned int completed2 = 0;
unsigned int completed3 = 0;

struct uk_spinlock cnt_lock_spin = UK_SPINLOCK_INITIALIZER();
struct uk_mutex cnt_lock_mutex = UK_MUTEX_INITIALIZER(cnt_lock_mutex);
struct uk_semaphore cnt_lock_semaphore;
uintptr_t vaddrs[2 * CONFIG_UKPLAT_LCPU_MAXCOUNT][ALLOCATION_ROUNDS];
uint8_t secondary_stacks[2 * CONFIG_UKPLAT_LCPU_MAXCOUNT][4 * __PAGE_SIZE];

struct uk_spinlock sched_lock = UK_SPINLOCK_INITIALIZER();

static inline void sync_cpus(unsigned int *var, unsigned int cnt)
{
	ukarch_inc(var);
	while(ukarch_load_n(var) != cnt)
		;
}

void alloc_test(struct __regs *regs, struct ukplat_lcpu_func *fn)
{
	int id;
	unsigned long j;
	
	/* Get CPU id */
	id = ukplat_lcpu_id();

	for (int round = 0; round < ALLOCATION_ROUNDS; round++) {
		uk_spin_lock(&sched_lock);
		vaddrs[id][round] = (uintptr_t) malloc((1 << (id % 12 + 1)) * PAGE_SIZE - PAGE_SIZE);
		uk_spin_unlock(&sched_lock);
	}
}

void free_test(struct __regs *regs, struct ukplat_lcpu_func *fn)
{
	int id;

	id = ukplat_lcpu_id();

	for (int round = 0; round < ALLOCATION_ROUNDS; round++) {
		uk_spin_lock(&sched_lock);
		free(vaddrs[id][round]);
		uk_spin_unlock(&sched_lock);
	}
}

int main() {
	unsigned int lcpuid_start[32];
	unsigned int num_start = N_CPUS;
	unsigned int lcpuid_run[32];
	unsigned int num_run = N_CPUS;

	ukplat_lcpu_entry_t entry[32];
	struct ukplat_lcpu_func alloc_test_func, free_test_func;
	alloc_test_func.fn = alloc_test;
	free_test_func.fn = free_test;
	void *stack[32];
	
	for (int i = 0; i < N_CPUS; i++) {
		stack[i] = secondary_stacks[i];
		entry[i] = 0;
		lcpuid_start[i] = i + 1;
		lcpuid_run[i] = i + 1;
	}

	pcnt = ukplat_lcpu_count();

	ukplat_lcpu_start(lcpuid_start, &num_start, stack, entry, 0);
	ukplat_lcpu_wait(NULL, 0, 0);
	uk_spin_init(&sched_lock);

	__nsec total_time_malloc = 0, start_time_malloc = 0, end_time_malloc = 0;
	__nsec total_time_free = 0, start_time_free = 0, end_time_free = 0;
	__nsec total_time = 0;

	for (int round = 0; round < ROUNDS; round++) {
		start_time_malloc = ukplat_monotonic_clock();
		ukplat_lcpu_run(lcpuid_run, &num_run, &alloc_test_func, 0);
		ukplat_lcpu_wait(NULL, 0, 0);
		end_time_malloc = ukplat_monotonic_clock();

		start_time_free = ukplat_monotonic_clock();
		ukplat_lcpu_run(lcpuid_run, &num_run, &free_test_func, 0);
		ukplat_lcpu_wait(NULL, 0, 0);
		end_time_free = ukplat_monotonic_clock();

		total_time_malloc += end_time_malloc - start_time_malloc;
		total_time_free += end_time_free - start_time_free;
		total_time += end_time_malloc - start_time_malloc;
		total_time += end_time_free - start_time_free;
	}

	uk_pr_info("Allocation time: %lu\n", total_time_malloc);
	uk_pr_info("Free time: %lu\n", total_time_free);
	uk_pr_info("Total time: %lu\n", total_time);


	return 0;
}
