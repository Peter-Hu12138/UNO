#ifndef _COMM
#define _COMM

typedef struct read_format {
	int num_chunks;
	char** data; // similar to argv
} read_data;
// the var list must be null terminated
// return 1 on error
int write_in_chunks(int fd, const char *arg, ...);
// return 1 on error
int read_in_chunks(int fd, read_data* data);
void free_read_data(read_data* data);

#define DEFAULT_PORT 55555
#endif
