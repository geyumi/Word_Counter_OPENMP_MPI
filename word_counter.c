#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define MAX_WORD_LEN 100

typedef struct {
    char word[MAX_WORD_LEN];
    int count;
} WordCount;

WordCount* wordList = NULL;  // pointer to dynamically allocated array
int wordCount = 0;
int capacity = 1000; // initial capacity (can start with a smaller or bigger number)

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

void addWord(char* word) {
    // Search if the word already exists
    for (int i = 0; i < wordCount; i++) {
        if (strcmp(wordList[i].word, word) == 0) {
            wordList[i].count++;
            return;
        }
    }

    // If new word and array full, grow the array
    if (wordCount == capacity) {
        capacity *= 2; // double the capacity
        WordCount* temp = realloc(wordList, capacity * sizeof(WordCount));
        if (temp == NULL) {
            printf("Memory allocation failed!\n");
            free(wordList);
            exit(1);
        }
        wordList = temp;
    }

    // Add new word
    strcpy(wordList[wordCount].word, word);
    wordList[wordCount].count = 1;
    wordCount++;
}

int main() {
    wordList = malloc(capacity * sizeof(WordCount));
    if (wordList == NULL) {
        printf("Memory allocation failed!\n");
        return 1;
    }

    FILE* file = fopen("input.txt", "r");
    if (!file) {
        perror("Error opening file");
        free(wordList);
        return 1;
    }

    clock_t start = clock();

    char word[MAX_WORD_LEN];
    while (fscanf(file, "%99s", word) == 1) {
        cleanWord(word);
        if (strlen(word) > 0) {
            addWord(word);
        }
    }

    fclose(file);
    clock_t end = clock();

    printf("Word Frequencies:\n");
    for (int i = 0; i < wordCount; i++) {
        printf("%s: %d\n", wordList[i].word, wordList[i].count);
    }

    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    printf("\nExecution time: %.6f seconds\n", time_spent);

    free(wordList);
    return 0;
}
