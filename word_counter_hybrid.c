#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mpi.h>
#include <omp.h>

#define MAX_WORD_LEN 100
#define NUM_THREADS 8

typedef struct {
    char word[MAX_WORD_LEN];
    int count;
} WordCount;

typedef struct {
    WordCount* words;
    int count;
    int capacity;
} WordList;

// Clean word by removing punctuation and converting to lowercase
void cleanWord(char* word) {
    int i, j = 0;
    char temp[MAX_WORD_LEN];
    for (i = 0; word[i] != '\0'; i++) {
        if (isalpha(word[i])) {
            temp[j++] = tolower(word[i]);
        }
    }
    temp[j] = '\0';
    strcpy(word, temp);
}

void initWordList(WordList* list, int capacity) {
    list->words = malloc(capacity * sizeof(WordCount));
    if (!list->words) {
        fprintf(stderr, "Memory allocation failed for WordList\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    list->count = 0;
    list->capacity = capacity;
}

void freeWordList(WordList* list) {
    if (list->words) free(list->words);
    list->words = NULL;
    list->count = 0;
    list->capacity = 0;
}

void ensureCapacity(WordList* list) {
    if (list->count >= list->capacity) {
        int newCapacity = list->capacity * 2;
        WordCount* newWords = realloc(list->words, newCapacity * sizeof(WordCount));
        if (!newWords) {
            fprintf(stderr, "Memory reallocation failed\n");
            freeWordList(list);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        list->words = newWords;
        list->capacity = newCapacity;
    }
}

void addWordWithCount(WordList* list, const char* word, int count) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->words[i].word, word) == 0) {
            list->words[i].count += count;
            return;
        }
    }
    ensureCapacity(list);
    strcpy(list->words[list->count].word, word);
    list->words[list->count].count = count;
    list->count++;
}

void addWordToList(WordList* list, const char* word) {
    addWordWithCount(list, word, 1);
}

int main(int argc, char** argv) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char (*allWords)[MAX_WORD_LEN] = NULL;
    int totalWords = 0;

    double start_time, end_time;

    if (rank == 0) {
        FILE* file = fopen("input.txt", "r");
        if (!file) {
            perror("Error opening file");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int capacity = 100000;
        allWords = malloc(capacity * sizeof(*allWords));
        char tempWord[MAX_WORD_LEN];

        while (fscanf(file, "%99s", tempWord) == 1) {
            cleanWord(tempWord);
            if (strlen(tempWord) > 0) {
                if (totalWords >= capacity) {
                    capacity *= 2;
                    allWords = realloc(allWords, capacity * sizeof(*allWords));
                    if (!allWords) {
                        fprintf(stderr, "Memory allocation failed\n");
                        MPI_Abort(MPI_COMM_WORLD, 1);
                    }
                }
                strcpy(allWords[totalWords++], tempWord);
            }
        }
        fclose(file);
    }

    MPI_Bcast(&totalWords, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int chunkSize = totalWords / size;
    int remainder = totalWords % size;
    int localSize = (rank < remainder) ? chunkSize + 1 : chunkSize;

    char (*localWords)[MAX_WORD_LEN] = malloc(localSize * sizeof(*localWords));
    if (!localWords) {
        fprintf(stderr, "Memory allocation failed for localWords\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int* sendcounts = NULL;
    int* displs = NULL;
    if (rank == 0) {
        sendcounts = malloc(size * sizeof(int));
        displs = malloc(size * sizeof(int));
        int offset = 0;
        for (int i = 0; i < size; i++) {
            int cnt = (i < remainder) ? chunkSize + 1 : chunkSize;
            sendcounts[i] = cnt * MAX_WORD_LEN;
            displs[i] = offset;
            offset += sendcounts[i];
        }
    }

    MPI_Scatterv(
        allWords ? &allWords[0][0] : NULL,
        sendcounts,
        displs,
        MPI_CHAR,
        &localWords[0][0],
        localSize * MAX_WORD_LEN,
        MPI_CHAR,
        0,
        MPI_COMM_WORLD
    );

    if (rank == 0) {
        free(sendcounts);
        free(displs);
        free(allWords);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    start_time = MPI_Wtime();

    WordList threadWordLists[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        initWordList(&threadWordLists[i], 1000);
    }

    omp_set_num_threads(NUM_THREADS);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int chunk_per_thread = localSize / NUM_THREADS;
        int extra = localSize % NUM_THREADS;
        int start_idx = tid * chunk_per_thread + (tid < extra ? tid : extra);
        int length = chunk_per_thread + (tid < extra ? 1 : 0);

        for (int i = start_idx; i < start_idx + length; i++) {
            addWordToList(&threadWordLists[tid], localWords[i]);
        }
    }

    WordList localList;
    initWordList(&localList, 2000);
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = 0; j < threadWordLists[i].count; j++) {
            addWordWithCount(&localList, threadWordLists[i].words[j].word, threadWordLists[i].words[j].count);
        }
        freeWordList(&threadWordLists[i]);
    }

    int local_count = localList.count;
    char* local_words_flat = malloc(local_count * MAX_WORD_LEN * sizeof(char));
    int* local_counts = malloc(local_count * sizeof(int));
    for (int i = 0; i < local_count; i++) {
        strcpy(&local_words_flat[i * MAX_WORD_LEN], localList.words[i].word);
        local_counts[i] = localList.words[i].count;
    }

    int* recv_counts = NULL;
    if (rank == 0) recv_counts = malloc(size * sizeof(int));

    MPI_Gather(&local_count, 1, MPI_INT, recv_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int* word_counts_sizes = NULL, *word_displs = NULL, *count_displs = NULL;
    char* all_words_flat = NULL;
    int* all_counts = NULL;
    int totalCollectedWords = 0;

    if (rank == 0) {
        word_counts_sizes = malloc(size * sizeof(int));
        word_displs = malloc(size * sizeof(int));
        count_displs = malloc(size * sizeof(int));

        word_displs[0] = count_displs[0] = 0;
        word_counts_sizes[0] = recv_counts[0] * MAX_WORD_LEN;

        for (int i = 1; i < size; i++) {
            word_counts_sizes[i] = recv_counts[i] * MAX_WORD_LEN;
            word_displs[i] = word_displs[i-1] + word_counts_sizes[i-1];
            count_displs[i] = count_displs[i-1] + recv_counts[i-1];
        }
        totalCollectedWords = count_displs[size-1] + recv_counts[size-1];

        all_words_flat = malloc(totalCollectedWords * MAX_WORD_LEN * sizeof(char));
        all_counts = malloc(totalCollectedWords * sizeof(int));
    }

    MPI_Gatherv(local_words_flat, local_count * MAX_WORD_LEN, MPI_CHAR,
                all_words_flat, word_counts_sizes, word_displs, MPI_CHAR,
                0, MPI_COMM_WORLD);

    MPI_Gatherv(local_counts, local_count, MPI_INT,
                all_counts, recv_counts, count_displs, MPI_INT,
                0, MPI_COMM_WORLD);

    free(local_words_flat);
    free(local_counts);
    freeWordList(&localList);
    free(localWords);

    if (rank == 0) {
        WordList finalList;
        initWordList(&finalList, totalCollectedWords > 1000 ? totalCollectedWords : 1000);

        for (int i = 0; i < totalCollectedWords; i++) {
            addWordWithCount(&finalList, &all_words_flat[i * MAX_WORD_LEN], all_counts[i]);
        }

        end_time = MPI_Wtime();

        printf("Final Word Count:\n");
        for (int i = 0; i < finalList.count; i++) {
            printf("%s: %d\n", finalList.words[i].word, finalList.words[i].count);
        }

        printf("\nTotal Time: %f seconds\n", end_time - start_time);
        
        // Save the output to a file after printing
FILE* file1 = fopen("final_word_count.txt", "w");
if (file1 != NULL) {
    fprintf(file1, "Final Word Count:\n");
    for (int i = 0; i < finalList.count; i++) {
        fprintf(file1, "%s: %d\n", finalList.words[i].word, finalList.words[i].count);
    }
    fprintf(file1, "\nTotal Time: %f seconds\n", end_time - start_time);
    fclose(file1);
    printf("Output also saved to 'final_word_count.txt'\n");
} else {
    perror("Error opening file to save output");
}


        freeWordList(&finalList);
        free(word_counts_sizes);
        free(word_displs);
        free(count_displs);
        free(all_words_flat);
        free(all_counts);
        free(recv_counts);
    }

    
    MPI_Finalize();
    return 0;
}
