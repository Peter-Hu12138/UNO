#include <communication.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>

#define MAX_CHUNKS 10

void test_single_chunk() {
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        write_in_chunks(pipefd[1], "hello", NULL);
        fflush(stderr);
        close(pipefd[1]);
        exit(0);
    }

    close(pipefd[1]);
    // Wait for child to write
    struct timespec ts = {0, 100000000}; // 100ms
    nanosleep(&ts, NULL);

    read_data input_data = {0};
    fprintf(stderr, "DEBUG: before read_in_chunks\n");
    int result = read_in_chunks(pipefd[0], &input_data);
    fprintf(stderr, "DEBUG: after read_in_chunks, result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    close(pipefd[0]);
    wait(NULL);

    printf("test_single_chunk: result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    if (input_data.data && input_data.data[0]) {
        fprintf(stderr, "DEBUG: data[0] = \"%s\"\n", input_data.data[0]);
    }
    assert(result == 0);
    assert(input_data.num_chunks == 1);
    assert(strcmp(input_data.data[0], "hello") == 0);
    printf("test_single_chunk: PASSED\n");

    free(input_data.data[0]);
    free(input_data.data);
}

void test_multiple_chunks() {
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        write_in_chunks(pipefd[1], "chunk1", "chunk2", "chunk3", NULL);
        fflush(stderr);
        close(pipefd[1]);
        exit(0);
    }

    close(pipefd[1]);
    struct timespec ts = {0, 100000000};
    nanosleep(&ts, NULL);

    read_data input_data = {0};
    fprintf(stderr, "DEBUG: before read_in_chunks\n");
    int result = read_in_chunks(pipefd[0], &input_data);
    fprintf(stderr, "DEBUG: after read_in_chunks, result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    close(pipefd[0]);
    wait(NULL);

    printf("test_multiple_chunks: result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    for (int i = 0; i < input_data.num_chunks && input_data.data; i++) {
        fprintf(stderr, "DEBUG: data[%d] = \"%s\"\n", i, input_data.data[i] ? input_data.data[i] : "(null)");
    }
    assert(result == 0);
    assert(input_data.num_chunks == 3);
    assert(strcmp(input_data.data[0], "chunk1") == 0);
    assert(strcmp(input_data.data[1], "chunk2") == 0);
    assert(strcmp(input_data.data[2], "chunk3") == 0);
    printf("test_multiple_chunks: PASSED\n");

    for (int i = 0; i < 3; i++) {
        free(input_data.data[i]);
    }
    free(input_data.data);
}

void test_empty_string_chunk() {
    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        write_in_chunks(pipefd[1], "", NULL);
        fflush(stderr);
        close(pipefd[1]);
        exit(0);
    }

    close(pipefd[1]);
    struct timespec ts = {0, 100000000};
    nanosleep(&ts, NULL);

    read_data input_data = {0};
    fprintf(stderr, "DEBUG: before read_in_chunks\n");
    int result = read_in_chunks(pipefd[0], &input_data);
    fprintf(stderr, "DEBUG: after read_in_chunks, result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    close(pipefd[0]);
    wait(NULL);

    printf("test_empty_string_chunk: result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    assert(result == 0);
    assert(input_data.num_chunks == 1);
    assert(strcmp(input_data.data[0], "") == 0);
    printf("test_empty_string_chunk: PASSED\n");

    free(input_data.data[0]);
    free(input_data.data);
}

void test_binary_data() {
    int pipefd[2];
    pipe(pipefd);

    char binary[] = {'\x01', '\x02', '\x00', '\xff', '\x80'};

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        write_in_chunks(pipefd[1], binary, NULL);
        fflush(stderr);
        close(pipefd[1]);
        exit(0);
    }

    close(pipefd[1]);
    struct timespec ts = {0, 100000000};
    nanosleep(&ts, NULL);

    read_data input_data = {0};
    fprintf(stderr, "DEBUG: before read_in_chunks\n");
    int result = read_in_chunks(pipefd[0], &input_data);
    fprintf(stderr, "DEBUG: after read_in_chunks, result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    close(pipefd[0]);
    wait(NULL);

    printf("test_binary_data: result=%d, num_chunks=%d\n", result, input_data.num_chunks);
    assert(result == 0);
    assert(input_data.num_chunks == 1);
    assert(memcmp(input_data.data[0], binary, 5) == 0);
    printf("test_binary_data: PASSED\n");

    free(input_data.data[0]);
    free(input_data.data);
}

int main() {
    printf("Running communication protocol tests...\n\n");

    test_single_chunk();
    test_multiple_chunks();
    test_empty_string_chunk();
    test_binary_data();

    printf("\nAll tests passed!\n");
    return 0;
}
