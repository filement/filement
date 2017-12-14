#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(OS_WINDOWS)
# include <poll.h>
#else
# include <windows.h>
# include <sys/stat.h>
# include "../../windows/mingw.h"
# define ETIMEDOUT 110
# undef read
# define read READ
# undef write
# define write WRITE
#endif

#include "types.h"
#include "stream.h"
#include "log.h"

#if !defined(OS_WINDOWS)
# include "io.h"
#else
# include "../io.h"
#endif

// TODO future improvements
//  buffering more data can speed things up (e.g. larger TLS_RECORD)
//  check how different sizes were chosen (e.g. WRITE_MAX)
//  more friendly API handling for when reading huge amounts of data is requested

// TODO: read about TLS packet size (negotiated maximum record size) // TODO I no longer seem to find the document describing such size limit is a performance optimization
#define TLS_RECORD		1024

#define WRITE_MAX 8192 /* rename this */

// When a flush operation is performed, if the corresponding buffer (input or output) is empty, its size is shrinked to the minimum allowed size.

// Priority strings:
// http://gnutls.org/manual/html_node/Priority-Strings.html
// http://gnutls.org/manual/html_node/Supported-ciphersuites.html#ciphersuites
// http://unhandledexpression.com/2013/01/25/5-easy-tips-to-accelerate-ssl/

// TODO consider using readv and circular read buffer http://stackoverflow.com/questions/3575424/buffering-data-from-sockets

// TODO ? don't allow operations on a terminated stream

// GNU TLS pitfalls
//  http://lists.gnu.org/archive/html/help-gnutls/2009-12/msg00011.html
//  http://lists.gnutls.org/pipermail/gnutls-help/2009-December/001901.html

#if defined(FILEMENT_TLS)
// TLS implementation based on X.509

# include <gnutls/gnutls.h>

// certificate authorities
#if defined(OS_MAC)
# define TLS_CA "/Applications/Filement.app/Contents/Resources/ca.crt"
#elif defined(OS_ANDROID)
# define TLS_CA ""
#elif !defined(OS_WINDOWS)
# define TLS_CA (PREFIX "/share/filement/ca.crt")
#else
extern char *tls_location;
# define TLS_CA tls_location
#endif

// certificate revocation lists
//# define CRLFILE "/etc/filement/crl.pem"

// certificate
# if !TEST
#  define TLS_CERT_FILE "/etc/filement/test.p12"
#  define TLS_CERT_PASSWORD ""
# else
#  define TLS_CERT_FILE "/etc/filement/filement.crt"
#  define TLS_KEY_FILE "/etc/filement/filement.key"
# endif

static gnutls_certificate_credentials_t x509;
static gnutls_dh_params_t dh_params;

int tls_init(void)
{
	int status;

	if (gnutls_global_init() != GNUTLS_E_SUCCESS)
	{
		error(logs("TLS error: cannot initialize"));
		return -1;
	}

	if (gnutls_certificate_allocate_credentials(&x509) != GNUTLS_E_SUCCESS)
	{
		error(logs("TLS error: cannot allocate credentials"));
		gnutls_global_deinit();
		return -1;
	}

	if (gnutls_certificate_set_x509_trust_file(x509, TLS_CA, GNUTLS_X509_FMT_PEM) < 0)
	{
		error(logs("TLS error: cannot set trust file"));
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}
	//gnutls_certificate_set_x509_crl_file(x509, CRLFILE, GNUTLS_X509_FMT_PEM); // TODO: certificate revokation list

# if !defined(DEVICE) /* devices don't have certificates */
#  if !TEST
	status = gnutls_certificate_set_x509_simple_pkcs12_file(x509, TLS_CERT_FILE, GNUTLS_X509_FMT_PEM, TLS_CERT_PASSWORD);
#  else
	status = gnutls_certificate_set_x509_key_file(x509, TLS_CERT_FILE, TLS_KEY_FILE, GNUTLS_X509_FMT_PEM);
#  endif
	if (status != GNUTLS_E_SUCCESS)
	{
		fprintf(stderr, "TLS error: %s\n", (char *)gnutls_strerror(status));
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}

	// Generate and set prime numbers for key exchange.
	// TODO These should be discarded and regenerated once a day, once a week or once a month, depending on the security requirements.
	if (gnutls_dh_params_init(&dh_params) != GNUTLS_E_SUCCESS)
	{
		error(logs("TLS error: cannot allocate prime numbers"));
		gnutls_certificate_free_credentials(x509);
		gnutls_global_deinit();
		return -1;
	}
	if (gnutls_dh_params_generate2(dh_params, gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, GNUTLS_SEC_PARAM_LOW)) != GNUTLS_E_SUCCESS)
	{
		error(logs("TLS error: cannot generate prime numbers"));
		// TODO: change GNUTLS_SEC_PARAM
		tls_term();
		return -1;
	}
	gnutls_certificate_set_dh_params(x509, dh_params);
# endif

	gnutls_global_set_log_level(1); // TODO change this

	return 0;
}

void tls_term(void)
{
# if !defined(DEVICE) /* devices don't have this */
	gnutls_dh_params_deinit(dh_params);
# endif
	gnutls_certificate_free_credentials(x509);
	gnutls_global_deinit();
}

static inline int stream_init_tls(struct stream *restrict stream, int fd, void *tls)
{
	// TODO: use gnutls_record_get_max_size()

	stream->_input = malloc(BUFFER_SIZE_MIN);
	if (!stream->_input) return ERROR_MEMORY;
	stream->_input_size = BUFFER_SIZE_MIN;
	stream->_input_index = 0;
	stream->_input_length = 0;

	stream->_output = malloc(BUFFER_SIZE_MIN);
	if (!stream->_output)
	{
		free(stream->_input);
		stream->_input = 0;
		return ERROR_MEMORY;
	}
	stream->_output_size = BUFFER_SIZE_MIN;
	stream->_output_index = 0;
	stream->_output_length = 0;

# if !defined(OS_WINDOWS)
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
# endif
	stream->fd = fd;

	stream->_tls = tls;
	stream->_tls_retry = 0;

	return 0;
}
#include <stdio.h>
int stream_init_tls_connect(struct stream *restrict stream, int fd, const char *restrict domain)
{
	int status;

	gnutls_session_t session;
	if (gnutls_init(&session, GNUTLS_NONBLOCK | GNUTLS_CLIENT) != GNUTLS_E_SUCCESS) return -1;

	if (gnutls_priority_set_direct(session, "NORMAL", 0) != GNUTLS_E_SUCCESS) goto error;
	if (gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509) != GNUTLS_E_SUCCESS) goto error;

	// Perform TLS handshake on the socket.
	gnutls_transport_set_ptr(session, (gnutls_transport_ptr_t)(ptrdiff_t)fd);
	while ((status = gnutls_handshake(session)) < 0)
		if (gnutls_error_is_fatal(status))
			goto error;

	// TODO: stream_term on the errors below?
	// Validate server certificate.
	unsigned errors = UINT_MAX; // set all bits
#if !TEST
	if (gnutls_certificate_verify_peers3(session, domain, &errors) != GNUTLS_E_SUCCESS) goto error;
#else
	if (gnutls_certificate_verify_peers3(session, 0, &errors) != GNUTLS_E_SUCCESS) goto error;
#endif
	gnutls_datum_t out;
	gnutls_certificate_verification_status_print(errors, gnutls_certificate_type_get(session), &out, 0);
	printf("%s\n", out.data);
	gnutls_free(out.data);
	if (errors) goto error;

	// TODO: check revokation lists

	return stream_init_tls(stream, fd, session);

error:
	gnutls_deinit(session);
	return -1;
}
int stream_init_tls_accept(struct stream *restrict stream, int fd)
{
	int status;

	gnutls_session_t session;
	if (gnutls_init(&session, GNUTLS_NONBLOCK | GNUTLS_SERVER) != GNUTLS_E_SUCCESS) return -1;

	if (gnutls_priority_set_direct(session, "PERFORMANCE:-CIPHER-ALL:+ARCFOUR-128:+AES-128-CBC:+AES-128-GCM", 0) != GNUTLS_E_SUCCESS) goto error;
	//if (gnutls_priority_set_direct(session, "PERFORMANCE:-CIPHER-ALL:+ARCFOUR-128", 0) != GNUTLS_E_SUCCESS) goto error;
	if (gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, x509) != GNUTLS_E_SUCCESS) goto error;

	// We request no certificate from the client. Otherwise we would need to verify it.
	// gnutls_certificate_server_set_request(session, GNUTLS_CERT_REQUEST);

	// Perform TLS handshake on the socket.
	gnutls_transport_set_ptr(session, (gnutls_transport_ptr_t)(ptrdiff_t)fd);
	while ((status = gnutls_handshake(session)) < 0)
		if (gnutls_error_is_fatal(status))
			goto error;

	//printf("CIPHER: %s\n", gnutls_cipher_get_name(gnutls_cipher_get(session)));

	return stream_init_tls(stream, fd, session);

error:
	gnutls_deinit(session);
	return -1;
}
#endif /* FILEMENT_TLS */

int stream_init(struct stream *restrict stream, int fd)
{
	stream->_input = malloc(BUFFER_SIZE_MIN);
	if (!stream->_input) return ERROR_MEMORY;
	stream->_input_size = BUFFER_SIZE_MIN;
	stream->_input_index = 0;
	stream->_input_length = 0;

	stream->_output = malloc(BUFFER_SIZE_MIN);
	if (!stream->_output)
	{
		free(stream->_input);
		stream->_input = 0;
		return ERROR_MEMORY;
	}
	stream->_output_size = BUFFER_SIZE_MIN;
	stream->_output_index = 0;
	stream->_output_length = 0;

#if !defined(OS_WINDOWS)
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
	stream->fd = fd;

#if defined(FILEMENT_TLS)
	stream->_tls = 0;
	stream->_tls_retry = 0;
#endif

	return 0;
}

int stream_term(struct stream *restrict stream)
{
	if (!stream->_input) return true;

	free(stream->_input);
	stream->_input = 0;

	free(stream->_output);
	stream->_output = 0;

#if defined(FILEMENT_TLS)
	if (stream->_tls)
	{
		// TODO: check gnutls_bye return status
		int status = gnutls_bye(stream->_tls, GNUTLS_SHUT_RDWR); // TODO: can this modify errno ?
		gnutls_deinit(stream->_tls);
		return (status == GNUTLS_E_SUCCESS);
	}
#endif

	return true;
}

size_t stream_cached(const struct stream *stream)
{
#if defined(FILEMENT_TLS)
	return ((stream->_input_length - stream->_input_index) + (stream->_tls ? gnutls_record_check_pending(stream->_tls) : 0));
#else
	return (stream->_input_length - stream->_input_index);
#endif
}

static int timeout(int fd, short event, unsigned long traffic)
{
	struct pollfd wait = {
		.fd = fd,
		.events = event,
		.revents = 0
	};
	int status;

	int time;
	if (traffic < 64 * 1024)
		time = 1250; // 1.25s
	else if (traffic < 1024 * 1024)
		time = 5000; // 5s
	else
		time = 20000; // 20s

	while (1)
	{
		status = poll(&wait, 1, time);
		if (status > 0)
		{
			if (wait.revents & event) return 0;
			else return ERROR_NETWORK;
		}
		else if ((status < 0) && ((errno == EINTR) || (errno == EAGAIN))) continue;
		else return ERROR_AGAIN;
	}
}

int stream_read(struct stream *restrict stream, struct string *restrict buffer, size_t length)
{
	size_t available = stream->_input_length - stream->_input_index;

	// If input buffer is not big enough, resize it. Realign buffer data if necessary
	if (length > stream->_input_size)
	{
		// Round up buffer size to a multiple of 256 to avoid multiple +1B resizing and 1B reading.
		size_t size = (length + 0xff) & ~0xff;

		char *buffer;

		if (length > BUFFER_SIZE_MAX) return ERROR_MEMORY; // TODO: is this okay?

		if (available) // the buffer has data that should be kept after resizing
		{
			buffer = realloc(stream->_input, sizeof(char) * size);
			if (!buffer)
			{
				free(stream->_input);
				stream->_input = 0;
				return ERROR_MEMORY;
			}

			// Move the available data to the beginning of the buffer if one of these holds:
			//  size is not enough to fit the requested data with the current buffer data layout
			//  at least half of the buffer is wasted
			if (((length - stream->_input_index) < length) || (stream->_input_size <= stream->_input_index * 2))
			{
				// Move byte by byte because the source and the destination overlap.
				if (stream->_input_index) memmove(buffer, buffer + stream->_input_index, available);

				stream->_input_index = 0;
				stream->_input_length = available;
			}
		}
		else // The buffer contains no useful data
		{
			// Free the old buffer and allocate a new one
			free(stream->_input);
			buffer = malloc(sizeof(char) * size);
			if (!buffer)
			{
				stream->_input = 0;
				return ERROR_MEMORY;
			}
		}

		// Remember the new buffer and its size.
		stream->_input = buffer;
		stream->_input_size = length;

		goto read; // we have to read additional data - no need to check for it
	}

	if (length > available) // If the available data in the buffer is not enough to satisfy the request
read:
	{
		ssize_t size;

		// Realign buffer data if necessary
		if ((stream->_input_index + length) > stream->_input_size)
		{
			// Move byte by byte because the source and the destination overlap
			size_t i;
			for(i = 0; i < available; ++i)
				stream->_input[i] = stream->_input[i + stream->_input_index];

			stream->_input_index = 0;
			stream->_input_length = available;
		}

		// Read until the buffer contains enough data to satisfy the request
		while (1)
		{
#if defined(FILEMENT_TLS)
			if (stream->_tls) size = gnutls_read(stream->_tls, stream->_input + stream->_input_length, stream->_input_size - stream->_input_length);
			else
#endif
				size = read(stream->fd, stream->_input + stream->_input_length, stream->_input_size - stream->_input_length);
			if (size > 0)
			{
				stream->_input_length += size;
				available += size;
				stream->traffic_ += size;
				if (available < length) continue;
				else break;
			}
			else if (!size) return ERROR_NETWORK;

			assert(size < 0);
			// TODO handle all possible errors and handle them properly

#if defined(FILEMENT_TLS)
			if (stream->_tls)
			{
				switch (size)
				{
					int status;
				case GNUTLS_E_AGAIN: // TODO ?call timeout(, POLLOUT)
					// Check if there is more data waiting to be read.
					if (status = timeout(stream->fd, POLLIN, stream->traffic_)) return status;
				case GNUTLS_E_INTERRUPTED:
					continue;

				case GNUTLS_E_REHANDSHAKE:
					// TODO check non-fatal error codes: http://www.gnu.org/software/gnutls/reference/gnutls-gnutls.html#gnutls-handshake
					/*while ((status = gnutls_handshake(session)) < 0)
						if (gnutls_error_is_fatal(status))
							return ERROR; // TODO choose appropriate error*/
					continue;

				default:
					errno = 0; // make sure errno_error() below returns ERROR
				}
			}
#endif

			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				// Check if there is more data waiting to be read.
				int status = timeout(stream->fd, POLLIN, stream->traffic_);
				if (status) return status;
			}
			else if (errno != EINTR) return errno_error(errno);
		}
	}

	// Set buffer to point to the available data
	buffer->data = stream->_input + stream->_input_index;
	buffer->length = available;
	return 0;
}

void stream_read_flush(struct stream *restrict stream, size_t length)
{
	stream->_input_index += length;

	// Reset length and index position if the buffer holds no data
	if (stream->_input_index == stream->_input_length)
	{
		stream->_input_index = 0;
		stream->_input_length = 0;
		if (stream->_input_size > BUFFER_SIZE_MIN)
		{
			stream->_input = realloc(stream->_input, BUFFER_SIZE_MIN);
			stream->_input_size = BUFFER_SIZE_MIN;
		}
	}
}

// Tries to write data without blocking. Returns number of bytes written or error code on error.
static ssize_t stream_write_internal(struct stream *restrict stream, const char *restrict buffer, size_t size)
{
	ssize_t status;

#if defined(FILEMENT_TLS)
	if (stream->_tls)
	{
		status = gnutls_write(stream->_tls, buffer, (stream->_tls_retry ? stream->_tls_retry : ((size > TLS_RECORD) ? TLS_RECORD : size)));
		if (status > 0)
		{
			stream->_tls_retry = 0;
			stream->traffic_ += status;
			return status;
		}
		// TODO handle all possible errors and handle them properly
		switch (status)
		{
		case GNUTLS_E_INTERRUPTED:
		case GNUTLS_E_AGAIN:
			if (!stream->_tls_retry) stream->_tls_retry = size;
			return 0;
		default:
#if !defined(OS_WINDOWS)
			return ERROR;
#else
			return -32767;
#endif
		}
	}
#endif

	status = write(stream->fd, buffer, ((size > WRITE_MAX) ? WRITE_MAX : size));
	if (status < 0)
	{
		status = errno_error(errno);
		if (status == ERROR_AGAIN) return 0;
	}
	stream->traffic_ += status;
	return status;
}

int stream_write(struct stream *restrict stream, const struct string *buffer)
{
	ssize_t size;
	size_t available;
	size_t index = 0;
#if defined(FILEMENT_TLS)
	size_t rest;
#endif

	// If there is buffered data in stream->_output, send it first.
	while (available = stream->_output_length - stream->_output_index)
	{
#if defined(FILEMENT_TLS)
		// Prepare as much data as possible in a single TLS write, unless we must retry the last write attempt.
		// Add more data to the output buffer if the available data is less than the optimal amount.
		if (stream->_tls && !stream->_tls_retry && (available < TLS_RECORD))
		{
			rest = TLS_RECORD - available;

			// If the required amount of data won't fit in the output buffer, move buffer contents at the start of the buffer.
			if (rest < (stream->_output_size - stream->_output_length))
			{
				stream->_output_length -= stream->_output_index;
				memmove(stream->_output, stream->_output + stream->_output_index, stream->_output_length);
				stream->_output_index = 0;
			}

			// If the data to write is less than the expected packet size, buffer it without sending anything.
			if (rest > (buffer->length - index))
			{
				memcpy(stream->_output + stream->_output_length, buffer->data + index, buffer->length - index);
				stream->_output_length += buffer->length - index;
				return 0;
			}

			memcpy(stream->_output + stream->_output_length, buffer->data + index, rest);
			stream->_output_length += rest;
			index += rest;
		}
#endif

		size = stream_write_internal(stream, stream->_output + stream->_output_index, available);
		if (size > 0)
		{
			stream->_output_index += size;
			if (stream->_output_index == stream->_output_length)
			{
				stream->_output_index = 0;
				stream->_output_length = 0;
			}
			continue;
		}
		else if (size) return size;

		// The remaining data can not be written immediately.

		available += buffer->length;
		if (available > BUFFER_SIZE_MAX)
		{
			// The remaining data is too much to buffer it. Wait until more data can be written.
			if (size = timeout(stream->fd, POLLOUT, stream->traffic_)) return size;
		}
		else
		{
			// Move buffer data at the beginning of the buffer.
			if (stream->_output_index)
			{
				stream->_output_length -= stream->_output_index;
				memmove(stream->_output, stream->_output + stream->_output_index, stream->_output_length);
				stream->_output_index = 0;
			}

			// Buffer the remaining data. Expand the buffer if it's not big enough.
			if (available > stream->_output_size)
			{
				char *new = realloc(stream->_output, available);
				if (!new) return ERROR_MEMORY;
				stream->_output = new;
				stream->_output_size = available;
			}
			memcpy(stream->_output + stream->_output_length, buffer->data, buffer->length);
			stream->_output_length = available;

			return 0;
		}
	}

	// now stream->_output is empty and stream->_output_index == 0

	// Send the data in buffer.
	while (available = buffer->length - index)
	{
#if defined(FILEMENT_TLS)
		// Buffer the data instead of sending it if it is less than the optimal size for TLS record.
		if (stream->_tls && (available < TLS_RECORD))
		{
			memcpy(stream->_output, buffer->data + index, available);
			stream->_output_length = available;
			return 0;
		}
#endif

		size = stream_write_internal(stream, buffer->data + index, available);
		if (size > 0)
		{
			index += size;
			continue;
		}
		else if (size) return size;

		// The remaining data can not be written immediately.

		if (available > BUFFER_SIZE_MAX)
		{
			// The remaining data is too much to buffer it. Wait until more data can be written.
			if (size = timeout(stream->fd, POLLOUT, stream->traffic_)) return size;
		}
		else
		{
			// Buffer the remaining data. Expand the buffer if it's not big enough.
			if (available > stream->_output_size)
			{
				char *new = realloc(stream->_output, available);
				if (!new) return ERROR_MEMORY;
				stream->_output = new;
				stream->_output_size = available;
			}
			memcpy(stream->_output, buffer->data + index, available);
			stream->_output_length = available;
			return 0;
		}
	}

	return 0;
}

int stream_write_flush(struct stream *restrict stream)
{
	ssize_t size;
	size_t available;

	while (available = stream->_output_length - stream->_output_index)
	{
		size = stream_write_internal(stream, stream->_output + stream->_output_index, available);
		if (size > 0)
		{
			stream->_output_index += size;
			continue;
		}
		else if (size)
		{
			return size;
		}

		// Wait until more data can be written.
		if (size = timeout(stream->fd, POLLOUT, stream->traffic_))
			return size;
	}

	// Set output buffer as empty. Shrink it if necessary.
	stream->_output_index = 0;
	stream->_output_length = 0;
	if (stream->_output_size > BUFFER_SIZE_MIN)
	{
		stream->_output = realloc(stream->_output, BUFFER_SIZE_MIN);
		stream->_output_size = BUFFER_SIZE_MIN;
	}

	return 0;
}
