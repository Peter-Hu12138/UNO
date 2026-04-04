#ifndef _COMM
#define _COMM

/* Message protocol
 *
 * Header
 * |   4 bytes   | 
 * | # of chunks |    # of bytes in the chunk payload     |
 *
 * Header is followed by # of chunks sent sequentially, for each chunk
 * Chunk header:
 * |             4 bytes             | 
 * | # of bytes in the chunk payload |
 * 
 * Chunk header is followed by # of bytes in the payload.
 */

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
