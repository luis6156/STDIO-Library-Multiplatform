#include "so_stdio.h"
#include <fcntl.h>
#include <string.h>

#include <stdio.h>

#define FILE_BUFF_LEN 4096

typedef struct _so_file {
	char buffer[FILE_BUFF_LEN]; // buffer to store data
	long cursor_fd;		    // FD cursor
	int fd;			    // HANDLE/FD
	int ferror;		    // error flag
	int cursor_buf_read;	    // buffer cursor (read operation)
	int cursor_buf_write;	    // buffer cursor (write operation)
	int buffer_size;	    // number of bytes in buffer
	int feof;		    // end of file flag
	unsigned char was_written;  // last operation
} _so_file;

/**
 * @brief Open a file and create a stream for it.
 *
 * @param pathname	path of the file
 * @param mode 		operation type (read, write, append)
 * @return SO_FILE* new stream
 */
SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *so_file;
	HANDLE fd;

	if (!strcmp(mode, "r")) {
		// Read
		fd = CreateFile(pathname, GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
	} else if (!strcmp(mode, "r+")) {
		// Read & Write
		fd = CreateFile(pathname, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (!strcmp(mode, "w")) {
		// Write
		fd = CreateFile(pathname, GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (!strcmp(mode, "w+")) {
		// Write & Read
		fd = CreateFile(pathname, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (!strcmp(mode, "a")) {
		// Append
		fd = CreateFile(pathname, FILE_APPEND_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (!strcmp(mode, "a+")) {
		// Append & Read
		fd = CreateFile(pathname, FILE_APPEND_DATA | GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else {
		return NULL;
	}

	// Error opening file
	if (fd == INVALID_HANDLE_VALUE)
		return NULL;

	so_file = calloc(1, sizeof(*so_file));
	if (!so_file)
		return NULL;

	// Assign the file descriptor to the struct
	so_file->fd = fd;

	return so_file;
}

/**
 * @brief Close a stream and flush it if necessary.
 *
 * @param stream structure that contains the buffer and FD/HANDLE
 * @return int SO_EOF on failure, otherwise "0"
 */
int so_fclose(SO_FILE *stream)
{
	int ret;

	// If bytes were written to the buffer -> flush it
	if (stream->cursor_buf_write > 0) {
		ret = so_fflush(stream);
		if (ret == SO_EOF) {
			CloseHandle(stream->fd);
			free(stream);
			return SO_EOF;
		}
	}

	// Close the file
	ret = CloseHandle(stream->fd);
	if (ret == 0) {
		free(stream);
		return SO_EOF;
	}

	free(stream);

	return 0;
}

/**
 * @brief Returns the file descriptor/HANDLE associated with the stream.
 *
 * @param stream structure that stores the FD/HANDLE
 * @return int file descriptor (Linux)
 */
HANDLE so_fileno(SO_FILE *stream) { return stream->fd; }

/**
 * @brief Writes the data from the buffer to the output file.
 *
 * @param stream structure that stores the buffer
 * @return int "0" on success or SO_EOF on failure
 */
int so_fflush(SO_FILE *stream)
{
	int ret, total_written = 0, bytes_written;

	// Loop while the number of bytes written is not equal to the buffer's
	// size
	while (total_written < stream->cursor_buf_write) {
		ret = WriteFile(stream->fd, stream->buffer + total_written,
				stream->cursor_buf_write - total_written,
				&bytes_written, NULL);
		if (ret == 0) {
			stream->ferror = 1;
			return SO_EOF;
		}

		total_written += bytes_written;
	}

	// Reset buffer and its properties
	memset(stream->buffer, 0, FILE_BUFF_LEN);
	stream->cursor_buf_write = 0;
	stream->buffer_size = 0;

	return 0;
}

/**
 * @brief Puts the cursor of the file to the desired position.
 *
 * @param stream structure that stores the FD/HANDLE
 * @param offset from the current position
 * @param whence starting point for the cursor
 * @return int "0" on success or SO_EOF on failure
 */
int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int ret;

	if (stream->was_written) {
		// Last operation was a write -> flush the buffer
		if (so_fflush(stream) == SO_EOF) {
			stream->ferror = 1;
			return SO_EOF;
		}
	} else {
		// Last operation was a read -> invalidate the buffer
		memset(stream->buffer, 0, FILE_BUFF_LEN);
		stream->cursor_buf_read = 0;
		stream->buffer_size = 0;
	}

	// Put the cursor to the desired position
	ret = SetFilePointer(stream->fd, offset, NULL, whence);
	if (ret == INVALID_SET_FILE_POINTER) {
		stream->ferror = 1;
		return SO_EOF;
	}

	// Set cursor's current position in the SO_FILE struct
	stream->cursor_fd = ret;

	return 0;
}

/**
 * @brief Returns the position of the cursor in the file opened.
 *
 * @param stream structure that stores the FD/HANDLE
 * @return long position of the cursor in the file
 */
long so_ftell(SO_FILE *stream)
{
	if (stream->feof)
		return -1;

	return stream->cursor_fd;
}

/**
 * @brief Puts inside a pointer size x nmemb bytes from the file starting
 * from the cursor's position.
 *
 * @param ptr pointer for the data to be stored in
 * @param size of one element
 * @param nmemb number of elements
 * @param stream structure that stores the stream's properties
 * @return size_t number of elements read into the pointer
 */
size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_to_read, bytes_read = 0;

	// Set last operation
	stream->was_written = 0;

	// End of file reached -> nothing else can be done
	if (stream->feof)
		return 0;

	// Compute total number of bytes to be read
	bytes_to_read = nmemb * size;

	// Call "so_fgetc" to read all the bytes required
	while (bytes_read < bytes_to_read) {
		int ret = so_fgetc(stream);

		if (ret == SO_EOF)
			break;

		// Copy the byte read into the pointer
		memcpy(((char *)ptr) + bytes_read, &ret, 1);
		++bytes_read;
	}

	// Return the number of elements read into the pointer
	return bytes_read / size;
}

/**
 * @brief Puts inside the file, starting from the cursor's position,
 * size x nmemb bytes.
 *
 * @param ptr pointer for the data to be read from
 * @param size of one element
 * @param nmemb number of elements
 * @param stream structure that stores the stream's properties
 * @return size_t number of elements written into the file
 */
size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	size_t bytes_to_write, bytes_written = 0;

	// Set last operation
	stream->was_written = 1;

	// Compute total number of bytes to be written
	bytes_to_write = nmemb * size;

	// Call "so_fputc" to write all the bytes required
	while (bytes_written < bytes_to_write) {
		int ret =
		    so_fputc(*(char *)(((char *)ptr) + bytes_written), stream);

		if (ret == SO_EOF)
			break;

		++bytes_written;
	}

	// Return the number of elements written into the file
	return bytes_written / size;
}

/**
 * @brief Read one byte from the opened file into the internal buffer.
 *
 * @param stream structure that stores the stream's properties
 * @return int byte read from the file (casted as unsigned char)
 */
int so_fgetc(SO_FILE *stream)
{
	int ret;
	int dwBytesRead;
	unsigned char tmp;

	// Set last operation
	stream->was_written = 0;

	// End of file reached -> nothing else can be done
	if (stream->feof)
		return SO_EOF;

	// If the buffer is empty or full -> perform a read operation
	if (stream->cursor_buf_read == 0 ||
	    stream->cursor_buf_read == stream->buffer_size) {
		ret = ReadFile(stream->fd, stream->buffer, FILE_BUFF_LEN,
			       &dwBytesRead, NULL);
		if (ret == 0) {
			// An error was returned
			stream->ferror = 1;
			return SO_EOF;
		} else if (dwBytesRead == 0) {
			// End of file has been reached
			stream->feof = 1;
			return SO_EOF;
		}

		// Reset the buffer's counters
		stream->cursor_buf_read = 0;
		stream->buffer_size = dwBytesRead;
	}

	// Increment the position of the file's cursor
	stream->cursor_fd++;

	return (unsigned char)stream->buffer[stream->cursor_buf_read++];
}

/**
 * @brief Write one byte from the internal buffer into the opened file.
 *
 * @param c byte to be written
 * @param stream structure that stores the stream's properties
 * @return int byte written from the internal buffer (casted as unsigned char)
 */
int so_fputc(int c, SO_FILE *stream)
{
	int ret;

	// Set last operation
	stream->was_written = 1;

	// If the buffer is full -> flush it
	if (stream->cursor_buf_write == FILE_BUFF_LEN) {
		ret = so_fflush(stream);
		if (ret == SO_EOF) {
			stream->ferror = 1;
			return SO_EOF;
		}
	}

	// Write the byte into the buffer
	memcpy(stream->buffer + stream->cursor_buf_write, &c, 1);
	stream->cursor_buf_write++;
	stream->cursor_fd++;

	return (unsigned char)c;
}

/**
 * @brief Check if the end of file has been reached.
 *
 * @param stream structure that stores the end of file flag
 * @return int "0" if the end has not been reached, otherwise "1"
 */
int so_feof(SO_FILE *stream) { return stream->feof; }

/**
 * @brief Check if an error has occurred durring any operation.
 *
 * @param stream structure that stores the error flag
 * @return int "0" if an error has not occurred, otherwise "1"
 */
int so_ferror(SO_FILE *stream) { return stream->ferror; }

/**
 * @brief Create a new stream connected to a pipe running the given command.
 *
 * @param command to be executed by the child process
 * @param type read or write operation
 * @return SO_FILE*
 */
SO_FILE *so_popen(const char *command, const char *type) { return NULL; }

/**
 * @brief Close a stream opened by popen and return the status of its child.
 *
 * @param stream structure that stores the pid
 * @return int "SO_EOF" on failure, otherwise process exit code
 */
int so_pclose(SO_FILE *stream) { return -1; }
