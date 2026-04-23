#include <iostream>
#include <unistd.h>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctime>
#include <vector>
#include <cstring>

#define MAX_RESOURCES 10

struct msgbuffer {
    long mtype;
    int value;
};

int main(int argc, char** argv) {

    if (argc < 2) return 1;

    int msqid = atoi(argv[1]);
    pid_t pid = getpid();

    srand(time(nullptr) ^ pid);

    // Track resources this process owns
    int allocated[MAX_RESOURCES];
    memset(allocated, 0, sizeof(allocated));

    while (true) {

        // Wait for OSS
        msgbuffer msg;
        if (msgrcv(msqid, &msg, sizeof(int), pid, 0) == -1) {
            perror("msgrcv");
            exit(1);
        }

        msgbuffer response;
        response.mtype = pid;

        // Random termination
        if (rand() % 100 < 10) {
            response.value = 0; // terminate
            msgsnd(msqid, &response, sizeof(int), 0);
            break;
        }

        int action = rand() % 100;

        // Request (70%)
        if (action < 70) {
            int r = rand() % MAX_RESOURCES;

            if (allocated[r] >= 5) {
                action = 100;
            } else {
                response.value = r + 1;
            }
        }

        // Release (30%)
        if (action >= 70) {
            std::vector<int> owned;

            for (int i = 0; i < MAX_RESOURCES; i++) {
                if (allocated[i] > 0)
                    owned.push_back(i);
            }

            if (!owned.empty()) {
                int r = owned[rand() % owned.size()];
                allocated[r]--;
                response.value = -(r + 1);
            } else {
		// nothing to release, request instead
                int r = rand() % MAX_RESOURCES;
                response.value = r + 1;
            }
        }

        // Send to OSS
        msgsnd(msqid, &response, sizeof(int), 0);

        // Assume granted if request
        if (response.value > 0) {
		int r = response.value -1;
            	allocated[r]++;
        }
    }

    return 0;
}
