#ifndef HELPER_H_
#define HELPER_H_

#include <pthread.h>
#include <semaphore.h>
#include "utils.h"

/*
 * Starile prin care poate trece un thread
 */
#define NEW 0
#define READY 1
#define RUNNING 2
#define WAITING 3
#define TERMINATED 4
/*
 * Starea de insucces, returnata in caz de eroare
 */
#define ERROR -1

/*
 * Numarul maxim de thread-uri
 */
#define NUM_THREADS 1000

/*
 * Structura care defineste un thread
 */
typedef struct {
	tid_t tid;
	int status;
	unsigned int events;
	unsigned int priority;
	unsigned int remaining_time;

	so_handler *handler;
	sem_t permission;
} so_thread;

/*
 * Structura care defineste un planificator
 */
typedef struct {
	unsigned int max_events;
	unsigned int queue_size;
	unsigned int num_threads;
	unsigned int time_quantum;

	so_thread *current_thread;
	so_thread **threads;
	so_thread **ready_queue;
} scheduler;

static int reschedule(void);
static so_thread *get_next_thread(void);
static int Round_Robin(so_thread *current, so_thread *next);
static void switch_threads(so_thread *current_thread, so_thread *next_thread);
static  unsigned int get_position(so_thread *thread);
static void shift_queue(unsigned int pos);
static void insert_queue(so_thread *thread, unsigned int pos);
static void *thread_func(void *arg);
static void init_thread(so_thread *thread, unsigned int priority,
						so_handler func);
static void start_thread(void);

/*
 * Definesc un planificator
 */
static scheduler *sched;

/*
 * Functie care planifica urmatorul thread,
 * ce va rula pe procesor
 */
static int reschedule(void)
{
	int rc = 0;
	so_thread *next_thread;
	so_thread *current_thread = sched->current_thread;

	/*
	 * Daca nu exista thread-uri care asteapta sa intre pe procesor,
	 * eliberez thread-ul curent, ca acesta sa poata rula
	 */
	next_thread = get_next_thread();
	if (!next_thread) {
		rc = sem_post(&current_thread->permission);
		DIE(rc != 0, "sem_pos");

		return 1;
	}

	/*
	 * Daca nu exista niciun thread pe procesor sau
	 * daca thread-ul curent se afla in starile
	 * WAITING sau TERMINATED,
	 * urmatorul thread va intra pe procesor sa isi ruleze task-ul
	 */
	if (!current_thread ||
		current_thread->status == WAITING ||
		current_thread->status == TERMINATED) {
		sched->current_thread = next_thread;
		start_thread();

		return 1;
	}

	/*
	 * Se alege urmatorul thread, conform algoritmului
	 * Round Robin
	 */
	if (Round_Robin(current_thread, next_thread))
		return 1;

	/*
	 * Daca s-a ajuns aici, inseamna ca thread-ul curent
	 * va rula in continuare pe procesor
	 */
	return 0;
}

/*
 * Functie care returneaza urmatorul thread,
 * cel care are cea mai mare prioritate
 */
static so_thread *get_next_thread()
{
	/*
	 * Thread-ul cu cea mai mare prioritate se gaseste pe ultima pozitie
	 * in coada READY, in cazul in care exista elemente in coada
	 */
	if (sched->queue_size)
		return sched->ready_queue[sched->queue_size - 1];

	/*
	 * Daca nu exista elemente in coada, se returneaza NULL
	 */
	return NULL;
}

/*
 * Functie care comuta intre thread-uri
 */
static void switch_threads(so_thread *current_thread, so_thread *next_thread)
{
	unsigned int pos;

	/*
	 * Thread-ul curent este preemptat
	 */
	current_thread->status = READY;
	current_thread->remaining_time = sched->time_quantum;
	pos = get_position(current_thread);
	shift_queue(pos);
	insert_queue(current_thread, pos);

	/*
	 * Thread-ul cu prioritate mai mare este lansat in executie
	 */
	sched->current_thread = next_thread;
	start_thread();
}

/*
 * Functie care aplica algoritmul Round Robin
 */
static int Round_Robin(so_thread *current, so_thread *next)
{
	/*
	 * Daca exista un thread cu o prioritate mai mare sau
	 * thread-ului curent i-a expirat cuanta de timp si
	 * exista un alt thread cu aceeasi prioritate,
	 * thraed-urile se vor interschimba
	 */
	if (current->priority < next->priority ||
		(current->remaining_time <= 0 &&
		current->priority == next->priority)) {
		switch_threads(current, next);

		return 1;
	}

	/*
	 * Daca thread-ului curent i-a expirat cuanta de timp
	 * si nu exista un alt thread cu o prioritate mai mare,
	 * acesta va ramane in continuare sa ruleze pe procesor,
	 * iar cuanta de timp se va reseta
	 */
	if (current->remaining_time <= 0)
		current->remaining_time = sched->time_quantum;

	return 0;
}

static void start_thread(void)
{
	int rc = 0;

	/*
	 * Se scoate thread-ul ales din coada READY si
	 * se marcheaza trecerea in starea RUNNING
	 */
	sched->ready_queue[--sched->queue_size] = NULL;
	sched->current_thread->status = RUNNING;

	/*
	 * Se elibereaza semaforul thread-ului ales,
	 * ca acesta sa poata rula pe procesor
	 */
	rc = sem_post(&sched->current_thread->permission);
	DIE(rc != 0, "sem_post\n");
}

/*
 * Adauga un thread in coada READY
 */
static void insert_queue(so_thread *thread, unsigned int pos)
{
	sched->ready_queue[pos] = thread;
	++sched->queue_size;
}

/*
 * Returneaza pozitia la care ar trebui insert thread-ul primit ca argument
 */
static unsigned int get_position(so_thread *thread)
{
	unsigned int i = 0;

	/*
	 * Se cauta pozitia la care trebuie adaugat noul thread:
	 * dupa primul thread fata de care are o prioritate mai mare
	 */
	while (i < sched->queue_size &&
		   sched->ready_queue[i]->priority < thread->priority)
		++i;

	return i;
}

/*
 * Shifteaza coada cu o pozitie spre stanga,
 * incepand cu pozitia pos, pentru a face loc noul thread, ce urmeaza
 * sa fie adaugat
 */
static void shift_queue(unsigned int pos)
{
	unsigned int j;

	for (j = sched->queue_size; j > pos; --j)
		sched->ready_queue[j] = sched->ready_queue[j - 1];
}

/*
 * Functie care initializeaza un thread primit ca argument
 */
static void init_thread(so_thread *thread, unsigned int priority,
						so_handler func)
{
	int rc = 0;

	/*
	 * Se populeaza campurile din structura thread
	 */
	thread->tid = INVALID_TID;
	thread->status = NEW;
	thread->events = SO_MAX_NUM_EVENTS;
	thread->priority = priority;
	thread->remaining_time = sched->time_quantum;
	thread->handler = func;

	/*
	 * Se initializeaza semaforul propriu thread-ului
	 */
	rc = sem_init(&thread->permission, 0, 0);
	DIE(rc != 0, "sem_init\n");

	/*
	 * Se trimite thread-ul in executie
	 */
	rc = pthread_create(&thread->tid, NULL,
						&thread_func, (void *) thread);
	DIE(rc != 0, "pthread_create\n");
}


/*
 * Functia rulata de fiecare thread, in urma apelului pthread_create
 */
static void *thread_func(void *arg)
{
	int rc = 0;
	so_thread *current_thread = (so_thread *)arg;

	/*
	 * Thread-ul asteapta sa fie deblocat de catre planificator,
	 * pentru a intra pe procesor sa isi ruleze task-ul
	 */
	rc = sem_wait(&current_thread->permission);
	DIE(rc != 0, "sem_wait\n");

	/*
	 * Se executa handler-ul
	 */
	current_thread->handler(current_thread->priority);

	/*
	 * Thread-ul isi actualizeaza starea
	 */
	current_thread->status = TERMINATED;

	/*
	 * Se actualizeaza planificarea
	 */
	if (!reschedule()) {
		rc = sem_post(&current_thread->permission);
		DIE(rc != 0, "sem_post\n");
	}

	return NULL;
}

#endif /* HELPER_H_ */

