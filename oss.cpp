#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.>
#include <signal.h>
#include <queue>
#include <fstream>
#include <cstring>

#define MAX_PROCESSES 20
#define MAX_RESOURCES 10

// ** Structs **
struct SimulatedClock {
	unsigned int seconds;
	unsigned int nanoseconds;
};

struct PCB {
	int occupied;
	pid_t pid;
	int blocked;

	int resourcesAllocated[MAX_RESOURCES];
	int requestResource;
};

struct Resource {
	int total;
	int available;
};

struct msgbuffer {
	long mtype;
	int value;
};

// ** Globals **
int shmid, msqid;
SimulatedClock* simClock = nullptr;

// ** Utilities **
void incrementClock(int ns) {
	simClock->nanoseconds += ns;
	while (simClock->nanoseconds >= 1000000000) {
		simClock->seconds++;
		simClock->nanoseconds -= 1000000000;
	}
}

void cleanup() {
	if (simClock) shmdt(simClock);
	shmctl(shmid, IPC_RMID, nullptr);
	msgctl(msqid, IPC_RMID, nullptr);
}

void signal_handler(int sig) {
	kill(0, SIGTERM);
	cleanup();
	exit(1);
}

// deadlock
bool detectDeadlock(PCB tabld[]) {
	for (int i = 0; i < MAX_PROCESSES; i++) {
		if (table[i].occupied && !table[i].blocked)
			return false;
	}
	return true;
}

// main function
int main(int argc, char** argv) {
	signal(SIGINT, signal_handler);
	std::ofstream logFile("log.txt");

	// Shared memory
	shmid = shmget(IPC_PRIVATE, sizeof(SimulatedClock), IPC_CREAT | 0666);
	simClock = (SimulatedClock*) shmat(shmid, nullptr, 0);
	simClock->seconds = 0;
	simClock->nanoseconds = 0;

	// Message queue
	msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

	// Process table
	PCB table[MAX_PROCESSES] = {};

	// Resource table
	Resource resources[MAX_RESOURCES];
	for (int i = 0; i < MAX_RESOURCES; i++) {
		resources[i].total = 5;
		resources[i].available = 5;
	}

	std::queue<int> readyQueue;

	int totalLaunched = 0;
	int activeChildren = 0;

	// Main loop
	while (totalLaunched < 5 || activeChildren > 0) {
		// Launch children
		if (activeChildren < 5) {
			int idx = -1;
			for (int i = 0; i < MAX_PROCESSES; i++) {
				if (!table[i].occupied) {
					idx = 0;
					break;
				}
			}

			if (idx != -1) {
				pid_t pid = fork();

				if (pid == 0) {
					char msqidStr[16];
					sprintf(msqidStr, "%d", msqid);
					execl("./worker", "./worker", msqidStr, nullptr);
					exit(1);
				}

				table[idx].occupied = 1;
				table[idx].pid = pid;
				table[idx].blocked = 0;
				table[idx].requestedResource = -1;
				memset(table[idx].resourcesAllocated, 0, sizeof(int) * MAX_RESOURCES);

				readyQueue.push(idx);

				logFile << "OSS: Created P" << idx << "\n";

				totalLaunched++;
				activeChildren++;
			}
		}

		// Unblock processes
		for (int i = 0; i < MAX_PROCESSES; i++) {
			if (table[i].occupied && table[i].blocked) {
				int r = table[i].requestedResource;
				if (r >= 0 && resources[r].available > 0) {
					resources[r].available--;
					table[i].resourcesAllocated[r]++;
					table[i].blocked = 0;

					readyQueue.push(i);

					logFile << "OSS: Unblocked P" << i << " granted R" << r << "\n";
				}
			}
		}

		// Dispatch
		if (!readyQueue.empty()) {
			int idx = readyQueue.front();
			readyQueue.pop();

			if (!table[idx].occupied) continue;

			pid_t pid = table[idx].pid;

			// send 'go'
			msgbuffer msg;
			msg.mtype = pid;
			msg.value = 1;
			msgsnd(msqid, &msg, sizeof(int), 0);

			// receive response
			msgbuffer res;
			msgrc(msqid, &res, sizeof(int), pid, 0);

			int val = res.value;

			// Request
			if (val > 0) {
				logFile << "OSS: P" << idx " requesting R" << val << "\n";

				if (resources[val].available > 0) {
					resources[val].available--;
					table[idx].resourcesAllocated[val]++;
					readyQueue.push(idx);

					logFile << "OSS: granted R" << val << " to P" << idx << "\n";
				} else {
					table[idx].blocked = 1;
					table[idx].requestedResource = val;

					logFile << "OSS: blocking P" << idx << " for R" << val << "\n";
				}
			}

			// Release
			else if (val < 0) {
				int r = abs(val);

				resources[r].available++;
				table[idx].resourcesAllocated[r]--;

				readyQueue.push(idx);

				logFile << "OSS: P" << idx << " released R" << r << "\n";
			}

			// Terminate
			else {
				logFile << "OSS: P" << idx << " terminating\n";

				for (int r = 0; r < MAX_RESOURCES; r++) {
					resources[r].available += table[idx].resourcesAllocated[r];
				}
				waitpid(pid, nullptr, 0);
				table[idx].occupied = 0;
				activeChildren--;
			}
		}

		// Deadlock detection
		if (simClock->seconds % 1 == 0) {
			if (detectDeadlock(table)) {
				logFile << "OSS: Deadlock detected\n";

				// kill first blocked process
				for (int i = 0; i < MAX_PROCESSES; i++) {
					if (table[i].occupied && table[i].blocked) {

						kill(table[i].pid, SIGTERM);

						for (int r = 0; r < MAX_RESOURCES; r++) {
							resources[r].available += table[i].resourcesAllocated[r];
						}

						waitpid(table[i].pid, nullptr, 0);
						table[i].occupied = 0;
						activeChildren--;

						logFile << "OSS: Killed P" << i << " to resolve deadlock\n";
						break;
					}
				}
			}
		}

		incrementClock(10000000);
	}

	cleanup();
	logFile.close();
	return 0;
}



