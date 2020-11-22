#include "_test/so_scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include "helper.h"

DECL_PREFIX int so_init(unsigned int time_quantum, unsigned int io)
{
	/*
	 * Pentru a evita dubla initializare,
	 * verific daca planificatorul este diferit de NULL
	 * Verific daca parametri sunt valizi
	 */
	if (sched || time_quantum <= 0 || io > SO_MAX_NUM_EVENTS)
		return ERROR;

	/*
	 * Aloc memorie pentru planificator
	 */
	sched = malloc(sizeof(scheduler));
	DIE(sched == NULL, "malloc scheduler\n");

	/*
	 * Aloc memorie pentru vectorul pentru thread-uri
	 */
	sched->threads = malloc(NUM_THREADS * sizeof(so_thread *));
	DIE(sched->threads == NULL, "malloc threads\n");

	/*
	 * Aloc memorie pentru coada de prioritati
	 */
	sched->ready_queue = malloc(NUM_THREADS * sizeof(so_thread *));
	DIE(sched->ready_queue == NULL, "malloc ready_queue\n");

	/*
	 * Initializez campurile planificatorului
	 */
	sched->max_events = io;
	sched->queue_size = 0;
	sched->num_threads = 0;
	sched->current_thread = NULL;
	sched->time_quantum = time_quantum;

	return 0;
}

DECL_PREFIX tid_t so_fork(so_handler *func, unsigned int priority)
{
	int rc;
	unsigned int pos;
	so_thread *current_thread;

	/*
	 * Verific validitatea parametrilor
	 */
	if (func == NULL || priority > SO_MAX_PRIO)
		return INVALID_TID;

	/*
	 * Creez un thread nou, care va executa functia start_thread
	 */
	current_thread = malloc(sizeof(so_thread));
	DIE(current_thread == NULL, "malloc thread so_fork\n");

	/*
	 * Initializez noul thread
	 */
	init_thread(current_thread, priority, func);

	/*
	 * Adaug thread-ul in vectorul de thread-uri
	 * si incrementez numarul de thread-uri
	 */
	sched->threads[sched->num_threads++] = current_thread;

	/*
	 * Adaug thread-ul in coada READY
	 */
	current_thread->status = READY;
	pos = get_position(current_thread);
	shift_queue(pos);
	insert_queue(current_thread, pos);

	/*
	 * Daca exista un thread care ruleaza,
	 * acesta consuma timp pe procesor,
	 * dupa care se planifica urmatorul thread
	 */
	if (sched->current_thread)
		so_exec();
	/*
	 * Altfel, doar se alege urmatorul thread
	 */
	else
		if (!reschedule()) {
			rc = sem_post(&current_thread->permission);
			DIE(rc != 0, "sem_post\n");
		}

	/*
	 * Returnez id-ul noului thread
	 */
	return current_thread->tid;
}

DECL_PREFIX int so_wait(unsigned int io)
{
	/*
	 * Daca argumentul nu este valid,
	 * functia intoarce un numar negativ (-1)
	 */
	if (io < 0 || io >= sched->max_events)
		return ERROR;

	/*
	 * Thread-ul curent trece in WAITING,
	 * fiind blocat pana la sosirea unui eveniment I/O
	 */
	sched->current_thread->status = WAITING;
	sched->current_thread->events = io;

	/*
	 * Thread-ul curent consuma timp pe procesor
	 * si se planifica urmatorul thread
	 */
	so_exec();

	return 0;
}

DECL_PREFIX int so_signal(unsigned int io)
{
	int unlocked_threads = 0, i, pos;
	so_thread *it;
	/*
	 * Validez parametrul io
	 */
	if (io < 0 || io >= sched->max_events)
		return -1;

	/*
	 * Fiecare thread care asteapta evenimentul io
	 * este deblocat (trecut in starea READY) si
	 * este adaugat in coada de executie.
	 */
	for (i = 0; i < sched->num_threads; ++i) {
		it = sched->threads[i];

		if (it->events == io && it->status == WAITING) {
			it->status = READY;
			pos = get_position(it);
			shift_queue(pos);
			insert_queue(it, pos);

			/*
			 * Numar thread-urile deblocate
			 */
			++unlocked_threads;
		}
	}

	/*
	 * Se thread-ul curent consuma timp pe procesor
	 * si se planifica urmatorul thread
	 */
	so_exec();

	/*
	 * Returnez numarul de thread-ul deblocate
	 */
	return unlocked_threads;
}

DECL_PREFIX void so_exec(void)
{
	int rc = 0;
	so_thread *current_thread = sched->current_thread;

	/*
	 * Marchez faptul ca thread-ul curent consuma
	 * timp pe procesor, prin decrementarea cuantei de timp
	 */
	--current_thread->remaining_time;

	/*
	 * Planific care este urmatorul thread ce va rula pe procesor
	 * Daca se stabileste ca thread-ul curent va rula in continuare,
	 * deblochez semaforul pentru a-si continua programul.
	 * In caz contrar, daca thread-ul a fost preemptat,
	 * semaforul lui va fi blocat pana la revenirea pe procesor
	 */
	if (!reschedule()) {
		rc = sem_post(&current_thread->permission);
		DIE(rc != 0, "sem_post\n");
	}

	rc = sem_wait(&current_thread->permission);
	DIE(rc != 0, "sem_wait\n");
}

DECL_PREFIX void so_end(void)
{
	int i, rc;

	/*
	 * Daca planificatorul este NULL,
	 * nu mai este nevoie de eliberarea memoriei
	 */
	if (!sched)
		return;

	/*
	 * Planificatorul asteapta terminarea tututor thread-urilor
	 * Daca thread-ul i si-a incheiat executia,
	 * semaforul asociat lui este distrus
	 * si se elibereaza memoria alocata pentru acesta.
	 */
	for (i = 0; i < sched->num_threads; ++i) {
		rc = pthread_join(sched->threads[i]->tid, NULL);
		DIE(rc != 0, "pthread_join\n");

		rc = sem_destroy(&sched->threads[i]->permission);
		DIE(rc != 0, "sem_destroy\n");

		free(sched->threads[i]);
	}

	/*
	 * Eliberez vectorul de thread-uri si planificatorul
	 */
	free(sched->threads);
	free(sched->ready_queue);
	free(sched);

	sched = NULL;
}
