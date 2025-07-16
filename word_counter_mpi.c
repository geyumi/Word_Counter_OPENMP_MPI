#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mpi.h>

#define MAX_WORD_LEN 100

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

// WordList handling functions
void initWordList(WordList* list, int capacity) {
    list->words = malloc(capacity * sizeof(WordCount));
    list->count = 0;
    list->capacity = capacity;
}

void freeWordList(WordList* list) {
    if (list->words) free(list->words);
    list->count = 0;
    list->capacity = 0;
}

void ensureCapacity(WordList* list) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->words = realloc(list->words, list->capacity * sizeof(WordCount));
    }
}

// Add word, increment count by 1 if exists, else add new
void addWordToList(WordList* list, const char* word) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->words[i].word, word) == 0) {
            list->words[i].count++;
            return;
        }
    }
    ensureCapacity(list);
    strcpy(list->words[list->count].word, word);
    list->words[list->count].count = 1;
    list->count++;
}

// NEW: Add word with specified count (for merging)
void addWordCountToList(WordList* list, const char* word, int count) {
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

        int capacity = 10000;
        allWords = malloc(capacity * sizeof(*allWords));
        char tempWord[MAX_WORD_LEN];

        while (fscanf(file, "%99s", tempWord) == 1) {
            cleanWord(tempWord);
            if (strlen(tempWord) > 0) {
                if (totalWords >= capacity) {
                    capacity *= 2;
                    allWords = realloc(allWords, capacity * sizeof(*allWords));
                }
                strcpy(allWords[totalWords++], tempWord);
            }
        }
        fclose(file);
    }

    // Broadcast totalWords to all processes
    MPI_Bcast(&totalWords, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Distribute words evenly among processes
    int chunkSize = totalWords / size;
    int remainder = totalWords % size;
    int localSize = (rank < remainder) ? chunkSize + 1 : chunkSize;

    char (*localWords)[MAX_WORD_LEN] = malloc(localSize * sizeof(*localWords));

    // Calculate displacements and counts (in chars)
    int* sendcounts = NULL;
    int* displs = NULL;
    if (rank == 0) {
        sendcounts = malloc(size * sizeof(int));
        displs = malloc(size * sizeof(int));
        int offset = 0;
        for (int i = 0; i < size; i++) {
            int countWords = (i < remainder) ? chunkSize + 1 : chunkSize;
            sendcounts[i] = countWords * MAX_WORD_LEN;
            displs[i] = offset;
            offset += sendcounts[i];
        }
    }

    // Scatterv sends different chunk sizes to each process
    MPI_Scatterv(
        allWords, sendcounts, displs, MPI_CHAR,
        localWords, localSize * MAX_WORD_LEN, MPI_CHAR,
        0, MPI_COMM_WORLD
    );

    if (rank == 0) {
        free(sendcounts);
        free(displs);
        free(allWords);
    }

    start_time = MPI_Wtime();

    // Local word count
    WordList localList;
    initWordList(&localList, 1000);
    for (int i = 0; i < localSize; i++) {
        addWordToList(&localList, localWords[i]);
    }

    // Prepare buffers for sending counts and words from all processes to rank 0
    int local_count = localList.count;

    int* local_counts = malloc(local_count * sizeof(int));
    char* local_words_flat = malloc(local_count * MAX_WORD_LEN);

    for (int i = 0; i < local_count; i++) {
        strcpy(&local_words_flat[i * MAX_WORD_LEN], localList.words[i].word);
        local_counts[i] = localList.words[i].count;
    }

    int* recv_counts = NULL; // number of unique words per process
    if (rank == 0) {
        recv_counts = malloc(size * sizeof(int));
    }

    MPI_Gather(&local_count, 1, MPI_INT, recv_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Calculate receive displacements for words and counts on rank 0
    int* word_counts_sizes = NULL; // counts of chars to receive per process
    int* word_displs = NULL;
    int* count_displs = NULL;

    char* all_words_flat = NULL;
    int* all_counts = NULL;
    int totalCollectedWords = 0;

    if (rank == 0) {
        word_counts_sizes = malloc(size * sizeof(int));
        word_displs = malloc(size * sizeof(int));
        count_displs = malloc(size * sizeof(int));

        int offset_words = 0;
        int offset_counts = 0;
        for (int i = 0; i < size; i++) {
            totalCollectedWords += recv_counts[i];
            word_counts_sizes[i] = recv_counts[i] * MAX_WORD_LEN;
            word_displs[i] = offset_words;
            count_displs[i] = offset_counts;
            offset_words += word_counts_sizes[i];
            offset_counts += recv_counts[i];
        }

        all_words_flat = malloc(offset_words * sizeof(char));
        all_counts = malloc(offset_counts * sizeof(int));
    }

    MPI_Gatherv(local_words_flat, local_count * MAX_WORD_LEN, MPI_CHAR,
                all_words_flat, word_counts_sizes, word_displs, MPI_CHAR,
                0, MPI_COMM_WORLD);

    MPI_Gatherv(local_counts, local_count, MPI_INT,
                all_counts, recv_counts, count_displs, MPI_INT,
                0, MPI_COMM_WORLD);

    end_time = MPI_Wtime();

    if (rank == 0) {
        WordList globalList;
        initWordList(&globalList, 1000);

        for (int i = 0; i < totalCollectedWords; i++) {
            addWordCountToList(&globalList, &all_words_flat[i * MAX_WORD_LEN], all_counts[i]);
        }

        printf("Word Frequencies:\n");
        for (int i = 0; i < globalList.count; i++) {
            printf("%s: %d\n", globalList.words[i].word, globalList.words[i].count);
        }

        printf("Execution Time: %f seconds\n", end_time - start_time);

        // Save the output to a file after printing
FILE *file = fopen("word_frequencies_output_mpi.txt", "w");
if (file != NULL) {
    fprintf(file, "Word Frequencies:\n");
    for (int i = 0; i < globalList.count; i++) {
        fprintf(file, "%s: %d\n", globalList.words[i].word, globalList.words[i].count);
    }
    fclose(file);
    printf("Output saved to 'word_frequencies_output.txt'\n");
} else {
    perror("Error opening file for writing");
}


        freeWordList(&globalList);
        free(recv_counts);
        free(word_counts_sizes);
        free(word_displs);
        free(count_displs);
        free(all_words_flat);
        free(all_counts);
    }

    freeWordList(&localList);
    free(localWords);
    free(local_counts);
    free(local_words_flat);

    MPI_Finalize();
    return 0;
}
