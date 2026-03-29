#ifndef _COMM
#define _COMM

typedef struct read_format {
	int num_chunks;
	char** data; // similar to argv
} read_data;
// the var list must be null terminated
int write_in_chunks(int fd, const char *arg, ...);
int read_in_chunks(int fd, read_data* data);

#endif
