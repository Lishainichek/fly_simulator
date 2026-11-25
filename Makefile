CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -Iinc -pthread
MODEL = src/map.c src/plane.c src/order.c src/station.c src/dispatcher.c src/weather.c src/simulation.c src/scenario.c
IPC = src/process_snapshot.c src/process_ipc.c
HEADERS = $(wildcard inc/*.h)

all: fly_simulator fly_fork_unnamed fly_fork_named fly_external_shm fly_process_worker

fly_simulator: $(MODEL) src/main.c $(HEADERS)
	$(CC) $(CFLAGS) $(MODEL) src/main.c -o $@

tests/test_sim: $(MODEL) tests/test_sim.c $(HEADERS)
	$(CC) $(CFLAGS) $(MODEL) tests/test_sim.c -o $@

fly_fork_unnamed: $(MODEL) $(IPC) src/process_controller.c $(HEADERS)
	$(CC) $(CFLAGS) -DDEFAULT_MODE='"fork-unnamed"' $(MODEL) $(IPC) src/process_controller.c -o $@ -lrt

fly_fork_named: $(MODEL) $(IPC) src/process_controller.c $(HEADERS)
	$(CC) $(CFLAGS) -DDEFAULT_MODE='"fork-named"' $(MODEL) $(IPC) src/process_controller.c -o $@ -lrt

fly_external_shm: $(MODEL) $(IPC) src/process_controller.c $(HEADERS)
	$(CC) $(CFLAGS) -DDEFAULT_MODE='"external-shm"' $(MODEL) $(IPC) src/process_controller.c -o $@ -lrt

fly_process_worker: $(MODEL) $(IPC) src/process_worker.c $(HEADERS)
	$(CC) $(CFLAGS) $(MODEL) $(IPC) src/process_worker.c -o $@ -lrt

process: fly_fork_unnamed fly_fork_named fly_external_shm fly_process_worker

legacy-test: tests/test_sim
	./tests/test_sim

process-test: all
	bash tests/test_process.sh

test: all tests/test_sim
	./tests/test_sim
	bash tests/test_process.sh

clean:
	rm -f fly_simulator fly_fork_unnamed fly_fork_named fly_external_shm fly_process_worker tests/test_sim

.PHONY: all clean test process process-test legacy-test
