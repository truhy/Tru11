/*
	MIT License

	Copyright (c) 2024 Truong Hy

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
	
	Version: 20240803
	
	Portable support of OS specific serial UART functions.
	Currently, supporting Windows and Linux.
	
	Note, under Linux, custom baud rate is currently unsupported - I have not
	found a universal way to make it work.  Custom baud rate with the struct
	termios2 doesn't even work on some Linux distributions, e.g. Ubuntu.
	
	Todo:
	Perhaps, convert TCHAR path to char ASCII string for the error exception.
	On Windows port the UART path is a TCHAR string which cannot be displayed by
	my ASCII based exception handling.
*/

#ifndef SERIAL_COM_H
#define SERIAL_COM_H

#if defined(WIN32) || defined(WIN64)

// =======
// Windows
// =======

#include "tru_macro.h"
#include "tc_string.h"
#include <windows.h>
#include <cstdint>
#include <string>

// The Windows ASYNC option below is useful for a multithreaded/event program type, e.g. GUI based.  For single threaded console it doesn't provide any advantage
//#define USE_ASYNC_READ_WRITE
#define SERIAL_COM_LEN_T DWORD

class serial_com{
protected:
	HANDLE fd;  // Handle to a device
	DCB dcb;  // Win32 serial com parameters
	COMMTIMEOUTS timeouts;
	bool is_rd_timed_out;

public:
	serial_com();
	~serial_com();
	void close_handle();
	void open_handle(const TCHAR *path);
	void open_handle(std::string path);
	void clear_comm_error();
	void set_params(uint32_t baud_rate, uint8_t byte_size, uint8_t parity, uint8_t stop_bits, bool rtscts_en);
	void set_timeout(uint32_t timeout_ms);
	DWORD read_port(void *buf, uint32_t len);
	DWORD write_port(void *buf, uint32_t len);
	void purge();
};

#else

// =====
// Linux
// =====

#include "tru_macro.h"
#include <cstdint>
#include <string>
#include <fcntl.h> // Contains file controls like O_RDWR
#include <errno.h> // Error integer and strerror() function
#include <unistd.h> // write(), read(), close()
#include <termios.h>  // Contains POSIX terminal control definitions

#define SERIAL_LEN_T ssize_t

#define NOPARITY    0
#define ODDPARITY   1
#define EVENPARITY  2
#define MARKPARITY  3
#define SPACEPARITY 4

#define ONESTOPBIT   0  // 1 stop bit
#define ONE5STOPBITS 1  // 1.5 stop bits
#define TWOSTOPBITS  2  // 2 stop bits

class serial_com{
protected:
	int fd;  // Handle to a device
	uint8_t buf[255];

public:
	serial_com();
	~serial_com();
	void close_handle();
	void open_handle(std::string path);
	void clear_comm_error();
	speed_t baud_rate_to_code(uint32_t baud_rate);
	uint8_t byte_size_to_code(uint8_t byte_size);
	void set_params(uint32_t baud_rate, uint8_t byte_size, uint8_t parity, uint8_t stop_bits, bool rtscts_en);
	void set_timeout(uint32_t timeout_ms);
	void set_wait(uint32_t min_chars);
	void purge();
	ssize_t read_port(void *buf, uint32_t len);
	ssize_t write_port(void *buf, uint32_t len);
};

#endif

// =======
// Generic
// =======

// Serial comm custom error message list
#define SERIALCOMM_ERROR_LIST(item) \
	item(SERIALCOMM_ERROR_WAITABANDONED_ID, "Wait abandoned") \
	item(SERIALCOMM_ERROR_TIMEDOUT_ID, "Timed out")

// Create enum from error message list
CREATE_ENUM(serialcomm_error_e, SERIALCOMM_ERROR_LIST)

class serialcomm_error_string{
public:
	INIT_INLINE_CLASS_ARRAY_ENUM(static constexpr char const *, messages, SERIALCOMM_ERROR_LIST)
};

#endif
