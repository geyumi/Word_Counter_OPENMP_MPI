#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <omp.h>

#define MAX_WORD_LEN 100
#define NUM_THREADS 8

typedef struct {
    char word[MAX_WORD_LEN];
    int count;
} WordCount;

typedef struct {
    WordCount* words;   // dynamically allocated array
    int count;          // number of unique words
    int capacity;       // current capacity of words array
} WordList;

// Dynamic array for all words read from file
char (*allWords)[MAX_WORD_LEN] = NULL;
int totalWords = 0;
int allWordsCapacity = 10000;  // initial capacity, will grow as needed

// Array of thread-local WordLists
WordList threadWordLists[NUM_THREADS];

// Global WordList to hold merged results
WordList globalWordList;

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

// Initialize WordList with a given capacity
void initWordList(WordList* list, int capacity) {
    list->words = malloc(capacity * sizeof(WordCount));
    if (!list->words) {
        fprintf(stderr, "Memory allocation failed for WordList\n");
        exit(EXIT_FAILURE);
    }
    list->count = 0;
    list->capacity = capacity;
}

// Free WordList memory
void freeWordList(WordList* list) {
    if (list->words) {
        free(list->words);
        list->words = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

// Ensure capacity for one more WordCount entry in WordList
void ensureCapacity(WordList* list) {
    if (list->count >= list->capacity) {
        int newCapacity = list->capacity * 2;
        WordCount* newWords = realloc(list->words, newCapacity * sizeof(WordCount));
        if (!newWords) {
            fprintf(stderr, "Memory reallocation failed\n");
            freeWordList(list);
            exit(EXIT_FAILURE);
        }
        list->words = newWords;
        list->capacity = newCapacity;
    }
}

// Add or update word in a given WordList (linear search, could be improved)
void addWordToList(WordList* list, const char* word) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->words[i].word, word) == 0) {
            list->words[i].count++;
            return;
        }
    }
    // Add new word
    ensureCapacity(list);
    strcpy(list->words[list->count].word, word);
    list->words[list->count].count = 1;
    list->count++;
}

// Merge a thread-local WordList into the global WordList
void mergeWordLists(WordList* dest, WordList* src) {
    for (int i = 0; i < src->count; i++) {
        int found = 0;
        for (int j = 0; j < dest->count; j++) {
            if (strcmp(dest->words[j].word, src->words[i].word) == 0) {
                dest->words[j].count += src->words[i].count;
                found = 1;
                break;
            }
        }
        if (!found) {
            ensureCapacity(dest);
            strcpy(dest->words[dest->count].word, src->words[i].word);
            dest->words[dest->count].count = src->words[i].count;
            dest->count++;
        }
    }
}

int main() {
    // Allocate initial allWords dynamic array
    allWords = malloc(allWordsCapacity * sizeof(*allWords));
    if (!allWords) {
        fprintf(stderr, "Memory allocation failed for allWords\n");
        return 1;
    }

    FILE* file = fopen("input.txt", "r");
    if (!file) {
        perror("Error opening file");
        free(allWords);
        return 1;
    }

    char tempWord[MAX_WORD_LEN];

    // Read and clean words from input file, dynamically growing allWords array
    while (fscanf(file, "%99s", tempWord) == 1) {
        cleanWord(tempWord);
        if (strlen(tempWord) > 0) {
            if (totalWords >= allWordsCapacity) {
                int newCapacity = allWordsCapacity * 2;
                char (*newAllWords)[MAX_WORD_LEN] = realloc(allWords, newCapacity * sizeof(*allWords));
                if (!newAllWords) {
                    fprintf(stderr, "Memory reallocation failed for allWords\n");
                    free(allWords);
                    fclose(file);
                    return 1;
                }
                allWords = newAllWords;
                allWordsCapacity = newCapacity;
            }
            strcpy(allWords[totalWords++], tempWord);
        }
    }
    fclose(file);

    // Initialize thread local WordLists
    for (int i = 0; i < NUM_THREADS; i++) {
        initWordList(&threadWordLists[i], 1000);
    }

    // Initialize global WordList
    initWordList(&globalWordList, 2000);

    double start = omp_get_wtime();

    omp_set_num_threads(NUM_THREADS);

    // Parallel word counting
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        WordList* localList = &threadWordLists[tid];
        localList->count = 0;

        #pragma omp for schedule(static)
        for (int i = 0; i < totalWords; i++) {
            addWordToList(localList, allWords[i]);
        }
    }

    // Merge thread-local lists into the global list (sequentially)
    for (int i = 0; i < NUM_THREADS; i++) {
        mergeWordLists(&globalWordList, &threadWordLists[i]);
    }

    double end = omp_get_wtime();

    // Output results
    printf("Word Frequencies:\n");
    for (int i = 0; i < globalWordList.count; i++) {
        printf("%s: %d\n", globalWordList.words[i].word, globalWordList.words[i].count);
    }

    printf("Execution time: %f seconds\n", end - start);

    // Free allocated memory
    for (int i = 0; i < NUM_THREADS; i++) {
        freeWordList(&threadWordLists[i]);
    }
    freeWordList(&globalWordList);
    free(allWords);

    return 0;
}
