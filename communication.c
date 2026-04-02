#include <communication.h>
#include <stdlib.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

/*
 * fd: file descriptor to write into
 * buffer: a pointer, content of which is written
 * size: size bytes will be written
 * 
 * This function always tries to write size bytes to fd.
 * A timeout of 5s is set to prevent forever dead lock.
 * Return 1 if the socket is closed, return -1 if timeouts.
 * 
*/
int buffered_write(int fd, const void * buffer, int size){
	const void * buf = buffer;
	struct timeval timeout;
	timeout.tv_sec = 5;  // 5 seconds
	timeout.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt");
	}
	while (size > 0) {
		int ret = write(fd, buf, size);
		if (ret < 0) {
			perror("write");
			return -1;
		}
		else if (ret == 0) {
			return 1;
		}
		size -= ret;
		buf += ret;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
		perror("setsockopt failed");
	}
	return 0;
}

/*
 * fd: file descriptor to read from
 * buffer: a pointer, content of read is written to buffer
 * size: size bytes will be read
 * 
 * This function always tries to read size bytes to fd.
 * A timeout of 5s is set to prevent forever dead lock.
 * Return 1 if the socket is closed, return -1 if timeouts.
 * 
 */
int buffered_read(int fd, void * buffer, int size){
	char * buf = (char *) buffer;
	while (size > 0) {
		int ret = read(fd, buf, size);
		if (ret < 0) {
			perror("read");
			exit(1);
		}
		else if(ret == 0) {
			return 1;
		}
		size -= ret;
		buf += ret;
	}
	return 0;
}



/*
 * fd: file descriptor to write into
 * var args: null terminated pointers, contents of which are written to fd
 * size: size bytes will be written
 * 
 * This function writes var args to fd according to our protocol communication.h.
 * 
 */
int write_in_chunks(int fd, const char *arg, ...){
	int len = 0;
	const char * arg_ptr = arg;
	fprintf(stderr, "DEBUG write_in_chunks: starting\n");

	va_list ap;
	va_start(ap, arg);
	while (arg_ptr != NULL) {
		len++;
		arg_ptr = va_arg(ap, char *); 
	}
	va_end(ap);
	
	int len_n = htonl(len);
	fprintf(stderr, "DEBUG write_in_chunks: num_chunks=%d\n", len);
	if (buffered_write(fd, &len_n, sizeof(int)) == 1) {
		return 1;
	}
	fprintf(stderr, "DEBUG write_in_chunks: wrote num_chunks, starting chunk loop\n");
	va_start(ap, arg);
	arg_ptr = arg;
	for (int i = 0; i < len; i++) {
		if (i != 0) arg_ptr = va_arg(ap, char *);
		int chunk_len = strlen(arg_ptr) + 1;
		fprintf(stderr, "DEBUG write_in_chunks: chunk %d, len=%d, data=%s\n", i, chunk_len, arg_ptr);
		int chunk_len_n = htonl(chunk_len);
		fprintf(stderr, "DEBUG child: before buffered_write chunk_len_n\n");
		if (buffered_write(fd, &chunk_len_n, sizeof(int)) == 1) {
			va_end(ap);
			fprintf(stderr, "DEBUG child: buffered_write chunk_len returned 1\n");
			return 1;
		}
		fprintf(stderr, "DEBUG child: buffered_write chunk_len succeeded\n");
		if (buffered_write(fd, arg_ptr, chunk_len) == 1) {
			va_end(ap);
			fprintf(stderr, "DEBUG child: buffered_write chunk_data returned 1\n");
			return 1;
		}
		fprintf(stderr, "DEBUG child: buffered_write chunk_data succeeded\n");
	}
	va_end(ap);
	return 0;
}


/*
 * fd: file descriptor to read from.
 * data: pointer to read_data, to which read data is written.
 * 
 * This function reads args into data according to our protocol expalined in communication.h.
 * 
 */
int read_in_chunks(int fd, read_data* data) {
	int len;
	fprintf(stderr, "DEBUG read_in_chunks: starting\n");
	if (buffered_read(fd, &len, sizeof(int)) == 1) {
		fprintf(stderr, "DEBUG read_in_chunks: buffered_read returned 1 (EOF or error) on first read\n");
		return 1;
	}
	int len_h = ntohl(len);
	fprintf(stderr, "DEBUG read_in_chunks: len=%d (network order), len_h=%d (host order)\n", len, len_h);
	data->num_chunks = len_h;
	data->data = malloc(sizeof(char *) * len_h);
	for (int i = 0; i < len_h; i++) {
		int chunk_len;
		if (buffered_read(fd, &chunk_len, sizeof(int)) == 1) {
			for (int j = 0; j < i; j++) {
                		free(data->data[j]);
            		}
            		free(data->data);
			fprintf(stderr, "DEBUG read_in_chunks: chunk %d buffered_read returned 1\n", i);
			return 1;
		}
		int chunk_len_h = ntohl(chunk_len);
		fprintf(stderr, "DEBUG read_in_chunks: chunk %d len=%d\n", i, chunk_len_h);
		char * chunk = malloc(sizeof(char) * chunk_len_h);
		if (buffered_read(fd, chunk, chunk_len_h) == 1) {
			free(chunk);
            		for (int j = 0; j < i; j++) {
                		free(data->data[j]);
            		}
            		free(data->data);
			return 1;
		}
		data->data[i] = chunk;
	}
	return 0;
}

/**
 * @brief Free the memory allocated for read_data
 * 
 * @param data 
 */
void free_read_data(read_data* data) {
  if (data == NULL || data->data == NULL) {
    return;
  }

  for (int i = 0; i < data->num_chunks; i++) {
    free(data->data[i]);
  }
  free(data->data);
  data->data = NULL;
  data->num_chunks = 0;
}