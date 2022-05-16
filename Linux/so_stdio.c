#include "so_stdio.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

#define FILE_BUFF_LEN 4096

typedef struct _so_file {
	unsigned char was_written;
	int fd;
	int ferror;
	int cursor_fd;
	int cursor_buf_read;
	int cursor_buf_write;
	int buffer_size;
	int feof;
	pid_t pid;
	char buffer[FILE_BUFF_LEN];
} _so_file;

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *so_file;
	int fd = -1;

	if (!strcmp(mode, "r")) {
		// Read
		fd = open(pathname, O_RDONLY);
	} else if (!strcmp(mode, "r+")) {
		// Read & Write
		fd = open(pathname, O_RDWR);
	} else if (!strcmp(mode, "w")) {
		// Write
		fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC);
	} else if (!strcmp(mode, "w+")) {
		// Write & Read
		fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC);
	} else if (!strcmp(mode, "a")) {
		// Append
		fd = open(pathname, O_WRONLY | O_CREAT | O_APPEND);
	} else if (!strcmp(mode, "a+")) {
		// Append & Read
		fd = open(pathname, O_RDWR | O_CREAT | O_APPEND);
	} else {
		return NULL;
	}

	// Error opening file
	if (fd == -1)
		return NULL;

	so_file = calloc(1, sizeof(*so_file));
	if (!so_file)
		return NULL;

	// Assign the file descriptor to the struct
	so_file->fd = fd;

	return so_file;
}

int so_fclose(SO_FILE *stream)
{
	int ret;

	if (stream->cursor_buf_write > 0) {
		ret = so_fflush(stream);
		if (ret == SO_EOF) {
			close(stream->fd);
			free(stream);
			return SO_EOF;
		}
	}

	ret = close(stream->fd);
	if (ret != 0) {
		free(stream);
		return SO_EOF;
	}

	free(stream);

	return ret;
}

int so_fileno(SO_FILE *stream) { return stream->fd; }

int so_fflush(SO_FILE *stream)
{
	int ret, written = 0;

	while (written < stream->cursor_buf_write) {
		ret = write(stream->fd, stream->buffer + written,
			    stream->cursor_buf_write - written);
		if (ret == -1)
			return SO_EOF;

		written += ret;
	}

	memset(stream->buffer, 0, FILE_BUFF_LEN);
	stream->cursor_buf_write = 0;

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int ret;

	if (stream->was_written) {
		so_fflush(stream);
	} else {
		memset(stream->buffer, 0, FILE_BUFF_LEN);
		stream->cursor_buf_read = 0;
		stream->buffer_size = 0;
	}

	ret = lseek(stream->fd, offset, whence);
	if (ret == -1) {
		stream->ferror = 1;
		return SO_EOF;
	}

	stream->cursor_fd = ret;

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	if (stream->feof)
		return -1;

	return stream->cursor_fd;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_to_read, bytes_read = 0;

	stream->was_written = 0;

	if (stream->feof) {
		return 0;
	}

	bytes_to_read = nmemb * size;

	while (bytes_read < bytes_to_read) {
		int ret = so_fgetc(stream);

		if (ret == SO_EOF)
			break;

		memcpy(ptr + bytes_read, &ret, 1);
		++bytes_read;
	}

	return bytes_read / size;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_to_write, bytes_written = 0;

	stream->was_written = 1;

	if (stream->feof)
		return 0;

	bytes_to_write = nmemb * size;

	while (bytes_written < bytes_to_write) {
		int ret = so_fputc(*((int *)(ptr + bytes_written)), stream);

		if (ret == SO_EOF)
			break;

		++bytes_written;
	}

	return bytes_written / size;
}

int so_fgetc(SO_FILE *stream)
{
	int ret;

	stream->was_written = 0;

	if (stream->feof)
		return SO_EOF;

	if (stream->cursor_buf_read == 0 ||
	    stream->cursor_buf_read == stream->buffer_size) {
		ret = read(stream->fd, stream->buffer, FILE_BUFF_LEN);
		if (ret < 0) {
			stream->ferror = 1;
			return SO_EOF;
		} else if (ret == 0) {
			stream->feof = 1;
			return SO_EOF;
		}

		stream->cursor_buf_read = 0;
		stream->buffer_size = ret;
	}

	stream->cursor_fd++;
	return (unsigned char)stream->buffer[stream->cursor_buf_read++];
}

int so_fputc(int c, SO_FILE *stream)
{
	int ret;

	stream->was_written = 1;

	if (stream->cursor_buf_write == FILE_BUFF_LEN) {
		ret = so_fflush(stream);
		if (ret == SO_EOF) {
			stream->ferror = 1;
			return SO_EOF;
		}
	}

	memcpy(stream->buffer + stream->cursor_buf_write, &c, 1);
	stream->cursor_buf_write++;
	stream->cursor_fd++;

	return c;
}

int so_feof(SO_FILE *stream) { return stream->feof; }

int so_ferror(SO_FILE *stream) { return stream->ferror; }

SO_FILE *so_popen(const char *command, const char *type)
{
	pid_t pid;
	SO_FILE *file;
	int pdes[2];

	if (pipe(pdes) < 0)
		return NULL;

	pid = fork();
	switch (pid) {
	case -1:
		close(pdes[0]);
		close(pdes[1]);
		/* error forking */
		return NULL;
	case 0:
		/* child process */
		if (*type == 'r') {
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				close(pdes[1]);
			}
			close(pdes[0]);
		} else {
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				close(pdes[0]);
			}
			close(pdes[1]);
		}

		execl("/bin/sh", "sh", "-c", command, NULL);

		/* only if exec failed */
		exit(EXIT_FAILURE);
	default:
		/* parent process */
		break;
	}

	file = calloc(1, sizeof(*file));

	if (!file)
		return NULL;

	if (*type == 'r') {
		file->fd = pdes[0];
		close(pdes[1]);
	} else {
		file->fd = pdes[1];
		close(pdes[0]);
	}

	file->pid = pid;

	return file;
}

int so_pclose(SO_FILE *stream)
{
	/* only parent process gets here */
	int pstat;
	pid_t curr_pid = stream->pid;

	int ret = so_fclose(stream);

	if (ret == SO_EOF)
		return SO_EOF;

	pid_t pid = waitpid(curr_pid, &pstat, 0);

	if (pid < 0)
		return -1;

	return pstat;
}
