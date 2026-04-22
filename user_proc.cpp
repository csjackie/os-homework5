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
    int value; // >0 request, <0 release, 0 terminate
};

int main(int argc, char** argv) {

    if (argc < 2) return 1;

    int msqid = atoi(argv[1]);
    pid_t pid = getpid();

    srand(time(nullptr) ^ pid);

    // Track resources this process owns
    int allocated[MAX_RESOURCES];
    memset(allocated, 0, sizeof(allocated));

    int lifetime = 1 + rand() % 5; // pseudo "time to live" loop count
    int iterations = 0;

    while (true) {

        // ===== WAIT FOR OSS SIGNAL =====
        msgbuffer msg;
        if (msgrcv(msqid, &msg, sizeof(int), pid, 0) == -1) {
            perror("msgrcv");
            exit(1);
        }

        iterations++;

        msgbuffer response;
        response.mtype = pid;

        // ===== TERMINATION CHECK =====
        if (iterations >= lifetime) {
            response.value = 0; // terminate
            msgsnd(msqid, &response, sizeof(int), 0);
            break;
        }

        int action = rand() % 100;

        // ===== REQUEST (≈70%) =====
        if (action < 70) {
            int r = rand() % MAX_RESOURCES;

            // ensure we don’t exceed some reasonable amount
            if (allocated[r] < 5) {
                response.value = r;
            } else {
                // fallback: release instead
                action = 100;
            }
        }

        // ===== RELEASE (≈30%) =====
        if (action >= 70) {
            std::vector<int> owned;

            for (int i = 0; i < MAX_RESOURCES; i++) {
                if (allocated[i] > 0)
                    owned.push_back(i);
            }

            if (!owned.empty()) {
                int r = owned[rand() % owned.size()];
                allocated[r]--;
                response.value = -r;
            } else {
                // nothing to release → request instead
                int r = rand() % MAX_RESOURCES;
                response.value = r;
            }
        }

        // ===== SEND ACTION TO OSS =====
        msgsnd(msqid, &response, sizeof(int), 0);

        // ===== WAIT FOR POSSIBLE GRANT =====
        // If request was granted, OSS will eventually let us run again.
        // We assume grant happened if we requested and were not blocked.
        if (response.value > 0) {
            allocated[response.value]++;
        }
    }

    return 0;
}
