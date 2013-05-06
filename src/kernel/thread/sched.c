/**
 * @file
 * @brief TODO --Alina
 *
 * @date 22.04.10
 * @author Dmitry Avdyukhin
 *          - Initial implementation
 * @author Kirill Skorodumov
 *          - Some insignificant contribution
 * @author Alina Kramar
 *          - Initial work on priority inheritance support
 * @author Eldar Abusalimov
 *          - Rewriting from scratch:
 *              - Interrupts safety, adaptation to Critical API
 *              - @c startq for deferred wake/resume processing
 */

#include <assert.h>
#include <errno.h>

#include <kernel/critical.h>
#include <kernel/irq_lock.h>
#include <kernel/thread/sched.h>
#include <kernel/thread/current.h>
#include <kernel/thread/sched_strategy.h>
#include <kernel/thread/state.h>
#include <kernel/time/timer.h>
#include <kernel/task.h>
#include <kernel/task/signal.h>
#include <kernel/work.h>
#include <hal/context.h>
#include <hal/ipl.h>

#include <profiler/tracing/trace.h>

#include <time.h>

#include <kernel/cpu.h>

#include <embox/unit.h>

EMBOX_UNIT(unit_init, unit_fini);

/* Functions to work with waking up in interrupt. Implemented in startq.c */
extern void startq_flush(void);
extern void startq_enqueue_sleepq(struct sleepq *sq, int wake_all);
extern void startq_enqueue_thread(struct thread *t, int sleep_result);

void do_wake_thread(struct thread *thread, int sleep_result);
void do_wake_sleepq(struct sleepq *sq, int wake_all);

static void do_sleep_locked(struct sleepq *sq);

static void __sched_wake(struct sleepq *sq, int wake_all);

static void post_switch_if(int condition);

static void sched_switch(void);

void sched_post_switch(void);

CRITICAL_DISPATCHER_DEF(sched_critical, sched_switch, CRITICAL_SCHED_LOCK);

static struct runq rq;
static struct work_queue startq;

static inline int in_harder_critical(void) {
	return critical_inside(__CRITICAL_HARDER(CRITICAL_SCHED_LOCK));
}

static inline int in_sched_locked(void) {
	return !critical_allows(CRITICAL_SCHED_LOCK);
}

int sched_init(struct thread* current, struct thread *idle) {
	current->last_sync = clock();

	thread_set_current(current);

	runq_init(&rq, current, idle);

	assert(thread_state_started(current->state));
	assert(thread_state_started(idle->state));

	return 0;
}

void sched_start(struct thread *t) {
	assert(!in_harder_critical());

	sched_lock();
	{
		assert(!thread_state_started(t->state));
		post_switch_if(runq_start(&rq, t));
		assert(thread_state_started(t->state));
	}
	sched_unlock();
}

void sched_finish(struct thread *t) {
	assert(!in_harder_critical());

	sched_lock();
	{
		assert(!thread_state_exited(t->state));

		if (thread_state_running(t->state)) {
			post_switch_if(runq_finish(&rq, t));
		} else {
			if (thread_state_sleeping(t->state)) {
				sleepq_finish(t->sleepq, t);
			} else {
				t->state = thread_state_do_exit(t->state);
			}
		}

		assert(thread_state_exited(t->state));
	}
	sched_unlock();
}

void sched_wake_all(struct sleepq *sq) {
	__sched_wake(sq, 1);
}

void sched_wake_one(struct sleepq *sq) {
	__sched_wake(sq, 0);
}

void __sched_wake(struct sleepq *sq, int wake_all) {
	if (in_harder_critical()) {
		startq_enqueue_sleepq(sq, wake_all);
		critical_request_dispatch(&sched_critical);
	} else {
		do_wake_sleepq(sq, wake_all);
	}
}

void do_wake_sleepq(struct sleepq *sq, int wake_all) {
	assert(!in_harder_critical());

	sched_lock();
	{
		post_switch_if(sleepq_wake(&rq, sq, wake_all));
	}
	sched_unlock();
}

void thread_wake_force(struct thread *thread, int sleep_result) {
	if (in_harder_critical()) {
		startq_enqueue_thread(thread, sleep_result);
		critical_request_dispatch(&sched_critical);
	} else {
		do_wake_thread(thread, sleep_result);
	}
}

void do_wake_thread(struct thread *thread, int sleep_result) {
	assert(!in_harder_critical());
	assert(thread_state_sleeping(thread->state));

	sched_lock();
	{
		thread->sleep_data->result = sleep_result;
		wake_work();
		post_switch_if(sleepq_wake_thread(&rq, thread->sleepq, thread));
	}
	sched_unlock();
}

static void do_sleep_locked(struct sleepq *sq) {
	struct thread *current = sched_current();
	assert(in_sched_locked() && !in_harder_critical());
	assert(thread_state_running(current->state));

	runq_sleep(&rq, sq);

	assert(current->sleepq == sq);
	assert(thread_state_sleeping(current->state));

	post_switch_if(1);
}

static void timeout_handler(struct sys_timer *timer, void *sleep_data) {
	struct thread *thread = (struct thread *) sleep_data;
	thread_wake_force(thread, -ETIMEDOUT);
}

static int wake_work(struct work *work) {
	struct sleep_data *sleep_data = (struct sleep_data *) work;
	struct thread *thread = sleep_data->thread;

	assert(in_sched_locked());

	post_switch_if(sleepq_wake_thread(&rq, thread->sleepq, thread)); /* TODO: SMP */

	return 1;
}

void sched_thread_wake(struct thread *thread) {
	ipl_t ipl = ipl_save();
	{
		if (!thread->sleep_data->waked) {
			work_post(thread->sleep_data->work, &startq);
			thread->sleep_data->waked = 1;
		}
	}
	ipl_restore(ipl);

	post_switch_if(1);
}

int sched_sleep_locked(struct sleepq *sq, unsigned long timeout) {
	int ret;
	struct sys_timer tmr;
	struct thread *current = sched_current();
	struct sleep_data sleep_data;

	assert(in_sched_locked() && !in_harder_critical());
	assert(thread_state_running(current->state));

	if (timeout != SCHED_TIMEOUT_INFINITE) {
		ret = timer_init(&tmr, TIMER_ONESHOT, (uint32_t)timeout, timeout_handler, current);
		if (ret != ENOERR) {
			return ret;
		}
	}

	do_sleep_locked(sq);

	sched_unlock();

	/* At this point we have been awakened and are ready to go. */
	assert(!in_sched_locked());
	assert(thread_state_running(current->state));

	sched_lock();

	if (timeout != SCHED_TIMEOUT_INFINITE) {
		timer_close(&tmr);
	}

	return data.result;
}

int sched_sleep(struct sleepq *sq, unsigned long timeout) {
	int sleep_res;
	assert(!in_sched_locked());

	sched_lock();
	{
		sleep_res = sched_sleep_locked(sq, timeout);
	}
	sched_unlock();

	return sleep_res;
}

void sched_post_switch(void) {
	sched_lock();
	{
		post_switch_if(1);
	}
	sched_unlock();
}

clock_t sched_get_running_time(struct thread *thread) {
	sched_lock();
	{
		if (thread_state_oncpu(thread->state)) {
			/* Recalculate time of the thread. */
			clock_t	new_clock = clock();
			thread->running_time += new_clock - thread->last_sync;
			thread->last_sync = new_clock;
		}
	}
	sched_unlock();

	return thread->running_time;
}

int sched_change_scheduling_priority(struct thread *thread,
		sched_priority_t new_priority) {
	assert((new_priority >= SCHED_PRIORITY_MIN)
			&& (new_priority <= SCHED_PRIORITY_MAX));

	sched_lock();
	{
		assert(!thread_state_exited(thread->state));

		if (thread_state_running(thread->state)) {
			post_switch_if(runq_change_priority(thread->runq, thread, new_priority));
		} else if (thread_state_sleeping(thread->state)) {
			sleepq_change_priority(thread->sleepq, thread, new_priority);
		} else {
			thread->sched_priority = new_priority;
		}

		assert(thread->sched_priority == new_priority);
	}
	sched_unlock();

	return 0;
}

void sched_set_priority(struct thread *thread,
		sched_priority_t new_priority) {
	assert((new_priority >= SCHED_PRIORITY_MIN)
			&& (new_priority <= SCHED_PRIORITY_MAX));

	sched_lock();
	{
		if (!thread_state_exited(thread->state)
				&& (thread != cpu_get_idle_thread())) {
			sched_change_scheduling_priority(thread, new_priority);
		}
		thread->initial_priority = new_priority;
	}
	sched_unlock();
}

static int switch_posted;

static void post_switch_if(int condition) {
	assert(in_sched_locked());

	if (condition) {
		switch_posted = 1;
		critical_request_dispatch(&sched_critical);
	}
}

/**
 * Called by critical dispatching code with IRQs disabled.
 */
static void sched_switch(void) {
	struct thread *prev, *next;
	clock_t new_clock;

	assert(!in_sched_locked());

	sched_lock();
	{
		work_queue_run(&startq);
		startq_flush();

		if (!switch_posted) {
			goto out;
		}
		switch_posted = 0;

		ipl_enable();

		prev = sched_current();

		if (prev == (next = runq_switch(&rq))) {
			ipl_disable();
			goto out;
		}

		/* Running time recalculation */
		new_clock = clock();
		prev->running_time += new_clock - prev->last_sync;
		next->last_sync = new_clock;

		assert(thread_state_running(next->state));

		trace_point("context switch");

		ipl_disable();

		//task_notify_switch(prev, next);

		thread_set_current(next);
		context_switch(&prev->context, &next->context);
	}

out:
	task_signal_hnd();
	sched_unlock_noswitch();
}

int sched_tryrun(struct thread *thread) {
	int res = 0;

	if (in_harder_critical()) {
		startq_enqueue_thread(thread, -EINTR);
		critical_request_dispatch(&sched_critical);
	} else {
		sched_lock();
		{
			if (thread_state_sleeping(thread->state)) {
				do_wake_thread(thread, -EINTR);
			} else if (!thread_state_running(thread->state)) {
				res = -1;
			}
		}
		sched_unlock();
	}

	return res;
}

int sched_cpu_init(struct thread *current) {
	extern int runq_cpu_init(struct runq *rq, struct thread *current);

	runq_cpu_init(&rq, current);

	current->last_sync = clock();
	thread_set_current(current);

	return 0;
}

void sched_prepare_wait(struct wait_data *wait_data) {
	assert(wait_data->status == WAIT_DATA_STATUS_NONE);
	IPL_SAFE_DO(wait_data->status = WAIT_DATA_STATUS_WAITING);
}

void sched_cleanup_wait(struct wait_data *wait_data) {
	IPL_SAFE_DO({
		wait_data->status == WAIT_DATA_STATUS_NONE;
		work_disable(wait_data->work);
	});
}



static inline void wait_data_init(struct wait_data *wait_data) {
	work_init(&wait_data->work, &wake_work);
	wait_data->waked = 0;
	wait_data->result = ENOERR;
}

static inline void wait_queue_enqueue(struct wait_queue *wait_queue,
		struct wait_data *wait_data) {
	assert(in_sched_locked());
	/* TODO: Priority */
	dlist_add_prev(&wait_data->link, &wait_queue->list);
}

static inline void wait_queue_dequeue(struct wait_queue *wait_queue) {
	assert(in_sched_locked());

	dlist_add_prev(&wait_data->link, &wait_queue->list);
}


static int unit_init(void) {
	return 0;
}

static int unit_fini(void) {
	return 0;
}
