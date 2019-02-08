#include <ctype.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <wait.h>

// Pomocna funkce pro uvolneni sdilene pameti a semaforu
void freeResources(int sharedArrayId, sem_t * action, sem_t * finish, sem_t * mutex, sem_t * multiplex) {
    // Uvolneni sdilene pameti pro promennou
    shmctl(sharedArrayId, IPC_RMID, NULL);

    // Uzavreni semaforu
    sem_close(action);
    sem_close(finish);
    sem_close(mutex);
    sem_close(multiplex);

    // Uvolneni semaforu
    sem_unlink("/xjanko10_action");
    sem_unlink("/xjanko10_finish");
    sem_unlink("/xjanko10_mutex");
    sem_unlink("/xjanko10_multiplex");
}

// Pomocna funkce pro overeni, je-li retezec cislo
int isStringNumber(char * string) {
    for(unsigned int i = 0; i < strlen(string); i++)
        if(!isdigit(string[i]))
            return 0;

    return 1;
}

// Pomocna funkce pro vypis akce
void printAction(char * message, sem_t * action, int * actionCounter, int numProcess, int numCurrAdult, int numCurrChild) {
    sem_wait(action);

    FILE * outputFile = fopen("proj2.out", "a");
    fprintf(outputFile, message, * actionCounter, numProcess, numCurrAdult, numCurrChild);
    fclose(outputFile);

    * actionCounter += 1;

    sem_post(action);
}

// Pomocna funkce pro vypis chyby
void printError(char * errorMessage) {
    fprintf(stderr, "%s\n", errorMessage);
}

// Pomocna funkce pro vygenerovani nahodneho cisla a prevedeni na mikrosekundy
long randomNumberTimesThousand(int maxNumber, int extraSeed) {
    srand(time(0) + extraSeed);

    unsigned long numBox = (unsigned long) maxNumber + 1;
    unsigned long numRand = (unsigned long) RAND_MAX + 1;
    unsigned long boxSize = numRand / numBox;
    unsigned long defect   = numRand % numBox;

    long x = 0;

    do {
        x = random();
    } while (numRand - defect <= (unsigned long)x);

    return x / boxSize * 1000;
}

// Pomocna funkce pro prevod retezce na cele cislo
int stringToInt(char * string) {
    return (int)strtol(string, '\0', 10);
}

// Pomocna funkce pro zpracovani argumentu
void handleArguments(int * argValues, int argc, char * argv[]) {
    // Overeni poctu argumentu
    if(argc != 7) {
        printError("Wrong number of arguments.");
        argValues[0] = -1;
    } else {
        // Overeni, jestli jsou argumenty cisla
        for(int i = 1; i < argc; i++) {
            if(!isStringNumber(argv[i])) {
                printError("Argument(s) not a number.");
                argValues[0] = -1;
                return;
            } else
                argValues[i - 1] = stringToInt(argv[i]);
        }

        // Overeni rozmezi argumentu
        if(argValues[0] <= 0 ||
                argValues[1] <= 0 ||
                (argValues[2] < 0 && argValues[2] >= 5001) ||
                (argValues[3] < 0 && argValues[3] >= 5001) ||
                (argValues[4] < 0 && argValues[4] >= 5001) ||
                (argValues[5] < 0 && argValues[5] >= 5001)) {
            printError("Argument(s) out of bounds.");
            argValues[0] = -1;
        }
    }
}

int main(int argc, char * argv[]) {
    // Nastaveni bufferu
    setbuf(stdout, NULL);

    // Promenne pro argumenty
    int numAdult = 0;
    int numChild = 0;
    int maxNewAdult = 0;
    int maxNewChild = 0;
    int maxSimAdult = 0;
    int maxSimChild = 0;

    int argValues[6];
    handleArguments(argValues, argc, argv);

    if(argValues[0] == -1)
        return 1;

    // Prirazeni overenych hodnot promennym
    numAdult = argValues[0];
    numChild = argValues[1];
    maxNewAdult = argValues[2];
    maxNewChild = argValues[3];
    maxSimAdult = argValues[4];
    maxSimChild = argValues[5];

    // Definice semaforu
    sem_t * action;
    sem_t * mutex;
    sem_t * multiplex;
    sem_t * finish;

    // Inicializace sdilene promenne a identifikatoru
    int sharedArrayId = 0;
    int * sharedArray = NULL;

    if ((sharedArrayId = shmget(IPC_PRIVATE, sizeof(int) * 5, IPC_CREAT | 0666)) == -1 ||
            (sharedArray = (int *)shmat(sharedArrayId, NULL, 0)) == NULL) {
        printError("Shared memory error.");
        return 2;
    }

    /**
     * Sdilena promenna:
     * 0 -> poradi akci
     * 1 -> pocet aktivnich dospelych
     * 2 -> pocet aktivnich deti
     * 3 -> pomocna promenna pro zruseni podminky, jakmile se negeneruji dalsi dospeli
     * 4 -> pocet dokoncenych procesu (dekrementuje)
    */
    sharedArray[0] = 1;
    sharedArray[4] = numAdult + numChild;

    // Vytvoreni semaforu
    if((action = sem_open("/xjanko10_action", O_CREAT | O_EXCL, 0666, 1)) == SEM_FAILED ||
            (finish = sem_open("/xjanko10_finish", O_CREAT | O_EXCL, 0666, 0)) == SEM_FAILED||
            (mutex = sem_open("/xjanko10_mutex", O_CREAT | O_EXCL, 0666, 1)) == SEM_FAILED||
            (multiplex = sem_open("/xjanko10_multiplex", O_CREAT | O_EXCL, 0666, 0)) == SEM_FAILED) {
        printError("Unable to create semaphore.");
        freeResources(sharedArrayId, action, finish, mutex, multiplex);
        return 2;
    }

    FILE * outputFile = fopen("proj2.out", "w");
    if (outputFile == NULL) {
        printError("Cannot open file.");
        freeResources(sharedArrayId, action, finish, mutex, multiplex);
        return 2;
    } else
        fclose(outputFile);

    // Pomocne procesy pro vytvareni dospelych a deti
    pid_t helperAdult = fork();

    if(helperAdult == 0) {
        pid_t helperChild = fork();

        if(helperChild == 0) {
            for(int i = 1; i <= numChild; i++) {
                // Uspani na delku generace ditete
                usleep(randomNumberTimesThousand(maxNewChild, helperChild + 64));
                helperChild = fork();

                if(helperChild == 0) {
                    sem_wait(mutex);

                    printAction("%d: C %d: started\n", action, &sharedArray[0], i, 0, 0);

                    // Dite ignoruje pri vstupu podminku, nebude-li se generovat novy dospely
                    if(sharedArray[3] == numAdult)
                        sem_post(multiplex);

                    // Uspani ditete v pripade, ze by porusilo vstupem podminku
                    else if((sharedArray[2] + 1) > sharedArray[1] * 3)
                        printAction("%d: C %d: waiting: %d : %d\n", action, &sharedArray[0], i, sharedArray[1], sharedArray[2]);

                    sem_post(mutex);

                    sem_wait(multiplex);

                    printAction("%d: C %d: enter\n", action, &sharedArray[0], i, 0, 0);

                    // Inkrementace aktivniho poctu deti
                    sharedArray[2]++;

                    // Simulace cinnosti ditete v centru
                    usleep(randomNumberTimesThousand(maxSimChild, helperChild));

                    printAction("%d: C %d: trying to leave\n", action, &sharedArray[0], i, 0, 0);

                    printAction("%d: C %d: leave\n", action, &sharedArray[0], i, 0, 0);

                    sem_post(multiplex);

                    // Dekrementace aktivniho poctu deti
                    sharedArray[2]--;

                    // Dekrementace od celkoveho poctu
                    sharedArray[4]--;

                    // Cekani na dokonceni vsech procesu
                    if(sharedArray[4] != 0)
                        sem_wait(finish);

                    else
                        for(int j = 0; j < numAdult + numChild; j++)
                            sem_post(finish);

                    printAction("%d: C %d: finished\n", action, &sharedArray[0], i, 0, 0);

                    break;
                } else if(helperChild < 0) {
                    printError("Unable to fork process.");
                    return 2;
                } else if(i == numChild)
                    waitpid(-1, NULL, 0);
            }
        } else if (helperChild < 0) {
            printError("Unable to fork process.");
            freeResources(sharedArrayId, action, finish, mutex, multiplex);
            return 2;
        } else {
            for(int i = 1; i <= numAdult; i++) {
                // Uspani na delku generace dospeleho
                usleep(randomNumberTimesThousand(maxNewAdult, helperAdult + 32));
                helperAdult = fork();

                if (helperAdult == 0) {
                    printAction("%d: A %d: started\n", action, &sharedArray[0], i, 0, 0);

                    printAction("%d: A %d: enter\n", action, &sharedArray[0], i, 0, 0);

                    for (int j = 0; j < 3; j++)
                        sem_post(multiplex);

                    // Inkrementace aktivniho poctu dospelych
                    sharedArray[1]++;

                    // Simulace cinnosti dospeleho v centru
                    usleep(randomNumberTimesThousand(maxSimAdult, helperAdult));

                    printAction("%d: A %d: trying to leave\n", action, &sharedArray[0], i, 0, 0);

                    // Uspani dospeleho v pripade, ze by porusil odchodem podminku
                    if (sharedArray[2] > (sharedArray[1] - 1) * 3)
                        printAction("%d: A %d: waiting: %d : %d\n", action, &sharedArray[0], i, sharedArray[1], sharedArray[2]);

                    sem_wait(mutex);

                    for (int j = 0; j < 3; j++)
                        sem_wait(multiplex);

                    printAction("%d: A %d: leave\n", action, &sharedArray[0], i, 0, 0);

                    // Dekrementace aktivniho poctu dospelych
                    sharedArray[1]--;

                    // Inkrementace ukoncenych dospelych
                    sharedArray[3]++;

                    // Dekrementace od celkoveho poctu
                    sharedArray[4]--;

                    sem_post(mutex);

                    // Cekani na dokonceni vsech procesu
                    if(sharedArray[4] != 0)
                        sem_wait(finish);

                    else
                        for(int j = 0; j < numAdult + numChild; j++)
                            sem_post(finish);

                    printAction("%d: A %d: finished\n", action, &sharedArray[0], i, 0, 0);

                    break;
                } else if(helperAdult < 0) {
                    printError("Unable to fork process.");
                    freeResources(sharedArrayId, action, finish, mutex, multiplex);
                    return 2;
                } else if(i == numAdult)
                    waitpid(-1, NULL, 0);
            }
        }
    } else if(helperAdult < 0) {
        printError("Unable to fork process.");
        freeResources(sharedArrayId, action, finish, mutex, multiplex);
        return 2;
    } else {
        waitpid(-1, NULL, 0);
        freeResources(sharedArrayId, action, finish, mutex, multiplex);
        return 0;
    }
}