#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
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
std::string filename = "log.txt";

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
bool detectDeadlock(PCB table[], int activeChildren) {
	if (activeChildren <= 1)
		return false;

	for (int i = 0; i < MAX_PROCESSES; i++) {
		if (table[i].occupied && !table[i].blocked)
			return false;
	}
	return true;
}

// display resource table
void printResourceTable(Resource resources[], std::ofstream &logFile) {
	logFile << "OSS: Resource Table:\n";
	for (int i = 0; i < MAX_RESOURCES; i++) {
		logFile << "R" << (i + 1) << ": "
			<< resources[i].available << "/"
			<< resources[i].total << " ";
	}
	logFile << "\n";
}

// display process table
void printProcessTable(PCB table[], std::ofstream &logFile) {
	logFile << "OSS: Process Table:\n";
	for (int i = 0; i < MAX_PROCESSES; i++) {
		if (table[i].occupied) {
			logFile << "P" << i
				<< " PID " << table[i].pid
				<< " Blocked " << table[i].blocked << "\n";
		}
	}
}

// main function
int main(int argc, char** argv) {
	
	// Default values for command line parameters
        int n = 1;
        int s = 1;
        float t = 1;
        float i = 1;
        int opt;

        // parse command line arguments
        while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
                switch (opt) {
                        case 'h':
                                std::cout << "To run program:\n\t ./oss -n # -s # -t # -i # -f file name\n";
                                return 0;
                        // Total number of children to launch
                        case 'n': n = atoi(optarg); break;
                        // Maximum simultaneous children 
                        case 's': s = atoi(optarg); break;
                        // Maximum time children ran before termination
                        case 't': t = atof(optarg); break;
                        // Allowed time between children launched
                        case 'i': i = atof(optarg); break;
                        // logfile
                        case 'f': filename = optarg; break;
                }
        }

        // Prints error message and exits program if the value of n, s, or t are out of range
        if (n <= 0 || n > 20 || s <= 0 || s > n || t <= 0) {
                std::cout << "Invalid argument values\n";
                exit(1);
        }
	
	signal(SIGINT, signal_handler);
	signal(SIGALRM, signal_handler);
	alarm(5);

	std::ofstream logFile(filename);

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
	for (int j = 0; j < MAX_RESOURCES; j++) {
		resources[j].total = 5;
		resources[j].available = 5;
	}

	std::queue<int> readyQueue;

	int totalLaunched = 0;
	int activeChildren = 0;
	
	unsigned int lastDeadlockCheck = 0;
	unsigned int lastPrint = 0;

	// stats
	int totalRequests = 0;
	int grantedRequests = 0;
	int blockedRequests = 0;
	int deadlocks = 0;

	// Main loop
	while (totalLaunched < n || activeChildren > 0) {
		// Launch children
		if (totalLaunched < n && activeChildren < s) {
			int idx = -1;
			for (int j = 0; j < MAX_PROCESSES; j++) {
				if (!table[j].occupied) {
					idx = j;
					break;
				}
			}

			if (idx != -1) {
				pid_t pid = fork();

				if (pid == 0) {
					char msqidStr[16];
					sprintf(msqidStr, "%d", msqid);
					execl("./user_proc", "./user_proc", msqidStr, nullptr);
					exit(1);
				}

				table[idx].occupied = 1;
				table[idx].pid = pid;
				table[idx].blocked = 0;
				table[idx].requestResource = -1;
				memset(table[idx].resourcesAllocated, 0, sizeof(int) * MAX_RESOURCES);

				readyQueue.push(idx);

				logFile << "OSS: Created P" << idx << " (PID " << pid << ")\n";

				totalLaunched++;
				activeChildren++;
			}
		}

		// Unblock processes
		for (int j = 0; j < MAX_PROCESSES; j++) {
			if (table[j].occupied && table[j].blocked) {
				int r = table[j].requestResource;
				if (r >= 0 && resources[r].available > 0) {
					resources[r].available--;
					table[j].resourcesAllocated[r]++;
					table[j].blocked = 0;
					table[j].requestResource = -1;

					readyQueue.push(j);

					logFile << "OSS: Unblocked P" << j << " granted R" << (r + 1) << "\n";
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
			if (msgrcv(msqid, &res, sizeof(int), pid, 0) == -1) {
				perror("msgrcv");
				exit(1);
			}

			int val = res.value;

			// Request
			if (val > 0) {
				int r = val -1;
				totalRequests++;

				logFile << "OSS: P" << idx << " requesting R" << (r + 1) << "\n";

				if (resources[r].available > 0) {
					resources[r].available--;
					table[idx].resourcesAllocated[r]++;
					readyQueue.push(idx);
					grantedRequests++;

					logFile << "OSS: granted R" << (r + 1) << " to P" << idx << "\n";
				} else {

					if (activeChildren == 1) {
						logFile << "OSS: Only one process, forcing termination of P" << idx << "\n";
						kill(pid, SIGTERM);
						waitpid(pid, nullptr, 0);
						
						table[idx].occupied = 0;
						activeChildren--;
						continue;
					}

					table[idx].blocked = 1;
					table[idx].requestResource = r;
					blockedRequests++;

					logFile << "OSS: blocking P" << idx << " for R" << (r + 1) << "\n";
				}
			}

			// Release
			else if (val < 0) {
				int r = abs(val) - 1;

				if (table[idx].resourcesAllocated[r] > 0) {
					table[idx].resourcesAllocated[r]--;
					resources[r].available++;
				}

				readyQueue.push(idx);

				logFile << "OSS: P" << idx << " released R" << (r + 1) << "\n";
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
		if (simClock->seconds > lastDeadlockCheck + 1) {
			lastDeadlockCheck = simClock->seconds;

			if (detectDeadlock(table, activeChildren)) {
				deadlocks++;
				logFile << "OSS: Deadlock detected\n";

				// kill first blocked process
				for (int j = 0; j < MAX_PROCESSES; j++) {
					if (table[j].occupied && table[j].blocked) {

						kill(table[j].pid, SIGTERM);

						for (int r = 0; r < MAX_RESOURCES; r++) {
							resources[r].available += table[j].resourcesAllocated[r];
						}

						waitpid(table[j].pid, nullptr, 0);
						table[j].occupied = 0;
						activeChildren--;

						logFile << "OSS: Killed P" << j << " to resolve deadlock\n";
						break;
					}
				}
			}
		}

		// display table
		if (simClock->seconds > lastPrint) {
			printResourceTable(resources, logFile);
			printProcessTable(table, logFile);
			lastPrint = simClock->seconds;
		}

		incrementClock(10000000);
		usleep(1000);
	}

	// final stats
	logFile << "\n ---------Statistics---------\n";
	logFile << "Total Requests: " << totalRequests << "\n";
	logFile << "Granted: " << grantedRequests << "\n";
	logFile << "Blocked: " << blockedRequests << "\n";
	logFile << "Deadlocks detected: " << deadlocks << "\n";

	cleanup();
	logFile.close();
	return 0;
}



