#include <iostream>

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

// simulated clock
// PCB
// msgbuffer

// ** Globals **

// deadlock

// main function
int main(int argc, char** argv) {
	signal(SIGINT, signal_handler);
	std::ofstream logFile("log.txt");

	// Shared memory
	
