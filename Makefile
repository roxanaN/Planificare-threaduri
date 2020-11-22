CFLAGS = -fPIC -Wall

.PHONY: build
build: libscheduler.so

libscheduler.so: so_scheduler.o
	gcc $(CFLAGS) -shared -o libscheduler.so so_scheduler.o

so_scheduler.o: so_scheduler.c so_scheduler.h utils.h helper.h
	gcc $(CFLAGS) -o so_scheduler.o -c so_scheduler.c

.PHONY: clean
clean:
	-rm -rf so_scheduler.o libscheduler.so
