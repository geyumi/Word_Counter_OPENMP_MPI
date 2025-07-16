#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_WORD_LEN 100
#define MAX_WORDS 10000

typedef struct {
    char word[MAX_WORD_LEN];
    int count;
} WordCount;

int readWordCounts(const char* filename, WordCount* arr, int* size) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        return -1;
    }

    char line[256];
    int i = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (sscanf(line, "%99[^:]: %d", arr[i].word, &arr[i].count) == 2) {
            i++;
            if (i >= MAX_WORDS) break;
        }
    }
    fclose(f);
    *size = i;
    return 0;
}

int findWordIndex(WordCount* arr, int size, const char* word) {
    for (int i = 0; i < size; i++) {
        if (strcmp(arr[i].word, word) == 0) return i;
    }
    return -1;
}

double computeRMSE(WordCount* serial, int serialSize, WordCount* parallel, int parallelSize) {
    double mse = 0.0;
    int n = serialSize;

    for (int i = 0; i < serialSize; i++) {
        int pIndex = findWordIndex(parallel, parallelSize, serial[i].word);
        int pCount = (pIndex == -1) ? 0 : parallel[pIndex].count;
        int diff = serial[i].count - pCount;
        mse += diff * diff;
    }

    mse /= n;
    return sqrt(mse);
}

int main() {
    WordCount serial[MAX_WORDS], openmp[MAX_WORDS], mpi[MAX_WORDS], hybrid[MAX_WORDS];
    int serialSize, openmpSize, mpiSize, hybridSize;

    if (readWordCounts("word_frequencies.txt", serial, &serialSize) < 0) return 1;
    if (readWordCounts("word_frequencies._output_openmp.txt", openmp, &openmpSize) < 0) return 1;
    if (readWordCounts("word_frequencies_output_mpi.txt", mpi, &mpiSize) < 0) return 1;
    if (readWordCounts("final_word_count.txt", hybrid, &hybridSize) < 0) return 1;

    double rmseOpenMP = computeRMSE(serial, serialSize, openmp, openmpSize);
    double rmseMPI = computeRMSE(serial, serialSize, mpi, mpiSize);
    double rmseHybrid = computeRMSE(serial, serialSize, hybrid, hybridSize);

    printf("RMSE OpenMP vs Serial: %f\n", rmseOpenMP);
    printf("RMSE MPI vs Serial: %f\n", rmseMPI);
    printf("RMSE Hybrid vs Serial: %f\n", rmseHybrid);

    return 0;
}
