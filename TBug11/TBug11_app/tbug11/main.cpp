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

	TBug11
	------

	A command line program for reading and writing the 68HC11 series
	microcontroller (MCU).
	
	Example devices:
		MC68HC11E1
		MC68HC11E9
		MC68HC711E9
		MC68HC11E20
		MC68HC711E20
		MC68HC811E2

	The MCU must start in the bootstrap mode with an 8MHz crystal.
	The mode can be selected by connecting MODA + MODB pins to ground.

	In bootstrap mode, the built-in bootloader program in the ROM will execute,
	which then waits for the host to send it a user program to place into RAM,
	and then executes it by jumping to RAM address 0x0000.
	
	This command line program requires the JBug11 talker program (talker firmware)
	to be downloaded into the MCU RAM first.

	Developer   : Truong Hy
	Date        : 30 Jul 2024
	Language    : C++
	Program type: Console program
*/

#include "app_error_string.h"
#include "tru_exception.h"
#include "cmd_line.h"
#include "to_string.h"
#include "serial_com.h"
#include "my_buf.h"
#include "my_file.h"
#include <stdio.h>
#include <iostream>
#include <format>

// For the Sleep/sleep function
#if defined(WIN32) || defined(WIN64)
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define BOOTLOADER_MAX_BYTE_COUNT 256
#define TALKER_MAX_BYTE_COUNT 256
// The talker is too slow so we don't need a delay (typically 10ms)
#define TALKER_ERASE_PROG_DELAY 0
#define TALKER_READ_CMD 0x01
#define TALKER_WRITE_CMD 0x41
#define HC11_CONFIG_ADDR 0X103f
#define SREC_ADDR_CHECKSUM_COUNT 3

enum class talker_echo_e{
	TALKER_ECHO_IGNORE,
	TALKER_ECHO_VERIFY_COM,
	TALKER_ECHO_VERIFY
};

// Generic transmit in blocks
void tx_chunk(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_txbuf, uint32_t arg_len){
	uint8_t *txbuf_p = arg_txbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		chunklen = (remaining > arg_params->serial_txbuf_size) ? arg_params->serial_txbuf_size : remaining;
		xferredlen = arg_serial_com->write_port(txbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_TX_FAIL_ID, app_error_string::messages[APP_ERROR_TX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}
		txbuf_p += chunklen;
		remaining -= chunklen;
	}
}

// Generic receive in blocks
void rx_chunk(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_rxbuf, uint32_t arg_len){
	uint8_t *rxbuf_p = arg_rxbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		chunklen = (remaining > arg_params->serial_rxbuf_size) ? arg_params->serial_rxbuf_size : remaining;
		xferredlen = arg_serial_com->read_port(rxbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_RX_FAIL_ID, app_error_string::messages[APP_ERROR_RX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}

		rxbuf_p += chunklen;
		remaining -= chunklen;
	}
}

void verify_echo(uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len){
	for(uint32_t i = 0; i < arg_len; i++){
		if(arg_txbuf[i] != arg_rxbuf[i]){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_ECHO_ID, app_error_string::messages[APP_ERROR_ECHO_ID], std::format(app_error_string::messages[APP_ERROR_ECHO_INFO_ID], arg_txbuf[i], arg_rxbuf[i]));
		}
	}
}

void verify_echo_com(uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len){
	for(uint32_t i = 0; i < arg_len; i++){
		if(arg_txbuf[i] != (uint8_t)~arg_rxbuf[i]){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_ECHO_ID, app_error_string::messages[APP_ERROR_ECHO_ID], std::format(app_error_string::messages[APP_ERROR_ECHO_INFO_ID], arg_txbuf[i], (uint8_t)~arg_rxbuf[i]));
		}
	}
}

// Generic transmit and receive in blocks
void txrx_chunk(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len, talker_echo_e arg_echo_check){
	uint8_t *txbuf_p = arg_txbuf;
	uint8_t *rxbuf_p = arg_rxbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		chunklen = (remaining > arg_params->serial_txbuf_size) ? arg_params->serial_txbuf_size : remaining;

		xferredlen = arg_serial_com->write_port(txbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_TX_FAIL_ID, app_error_string::messages[APP_ERROR_TX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}

		rx_chunk(arg_params, arg_serial_com, rxbuf_p, chunklen);
		switch(arg_echo_check){
			case talker_echo_e::TALKER_ECHO_VERIFY: verify_echo(txbuf_p, rxbuf_p, chunklen); break;
			case talker_echo_e::TALKER_ECHO_VERIFY_COM: verify_echo_com(txbuf_p, rxbuf_p, chunklen); break;
			case talker_echo_e::TALKER_ECHO_IGNORE: break;
		}

		txbuf_p += chunklen;
		rxbuf_p += chunklen;
		remaining -= chunklen;
	}
}

// Generic receive and transmit in blocks
void rxtx_chunk(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_rxbuf, uint8_t *arg_txbuf, uint32_t arg_len){
	uint8_t *rxbuf_p = arg_rxbuf;
	uint8_t *txbuf_p = arg_txbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		chunklen = (remaining > arg_params->serial_rxbuf_size) ? arg_params->serial_rxbuf_size : remaining;

		xferredlen = arg_serial_com->read_port(rxbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_RX_FAIL_ID, app_error_string::messages[APP_ERROR_RX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}

		tx_chunk(arg_params, arg_serial_com, txbuf_p, chunklen);

		rxbuf_p += chunklen;
		txbuf_p += chunklen;
		remaining -= chunklen;
	}
}

// Transmit and receive in blocks specifically for downloading the control program
void txrx_chunk_control_program(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len){
	uint8_t *txbuf_p = arg_txbuf;
	uint8_t *rxbuf_p = arg_rxbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		chunklen = (remaining > arg_params->serial_txbuf_size) ? arg_params->serial_txbuf_size : remaining;
		remaining -= chunklen;

		xferredlen = arg_serial_com->write_port(txbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_TX_FAIL_ID, app_error_string::messages[APP_ERROR_TX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}

		// Note this problem is to do with the use serial no control flow and the bootloader's baud rate changing scheme
		// For the last byte: some USB to TTL serial adapters will not receive the last echoed byte
		if(remaining == 0){
			if(chunklen == 1){
				// Note: some adapters will not receive the last echoed byte, if so we will ignore the error
				// Read the last byte
				try{
					xferredlen = arg_serial_com->read_port(rxbuf_p, chunklen);
					verify_echo(txbuf_p, rxbuf_p, chunklen);
				}catch(tru_exception &ex){
					(void)ex;  // Suppress unreferenced warning
				}
			}else{
				// A workaround for some adapters, since last byte is missing we read one less
				rx_chunk(arg_params, arg_serial_com, rxbuf_p, chunklen - 1);
				verify_echo(txbuf_p, rxbuf_p, chunklen - 1);

				// Note: some adapters will not receive the last echoed byte, if so we will ignore the error
				// Read the last byte
				try{
					xferredlen = arg_serial_com->read_port(rxbuf_p + chunklen - 1, 1);
					verify_echo(txbuf_p, rxbuf_p + chunklen - 1, 1);
				}catch(tru_exception &ex){
					(void)ex;  // Suppress unreferenced warning
				}
			}
		}else{
			rx_chunk(arg_params, arg_serial_com, rxbuf_p, chunklen);
			verify_echo(txbuf_p, rxbuf_p, chunklen);
		}

		txbuf_p += chunklen;
		rxbuf_p += chunklen;
	}
}

// Note: all MCU types have minimum of 256 bytes RAM (some have more)
void send_control_program(cl_my_params *arg_params, serial_com *arg_serial_com){
	cl_my_file talker_file;
	std::string line_str;
	uint8_t srec_bytecount;
	uint16_t byte_index = 0;
	uint16_t pad_index;
	uint8_t txbyte;
	cl_my_buf txbuf;
	cl_my_buf rxbuf;
	uint8_t *txbuf_p;
	uint8_t *rxbuf_p;

	txbuf.alloc_buf((arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	txbuf_p = txbuf.get_buf();
	rxbuf.alloc_buf((arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	rxbuf_p = rxbuf.get_buf();

	std::cout << "Loading " << arg_params->talker_filename << std::endl;
	talker_file.open_file(arg_params->talker_filename, "rb");

	// =====================
	// Read file into buffer
	// =====================

	do{
		talker_file.read_file_line(line_str);

		//std::cout << "Line: " << line_str << std::endl;

		// Check for valid S1 record
		if(line_str.size() > 8){
			// S1 record type?
			if(line_str.compare(0, 2, "S1") == 0){
				srec_bytecount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16);  // Extract srecord data byte count
				// Byte count is valid?
				if((srec_bytecount > 0) && (srec_bytecount >= ((line_str.size() - 4) / 2))){
					if(srec_bytecount > SREC_ADDR_CHECKSUM_COUNT){
						std::cout << line_str << std::endl;
						// Loop through record data (exclude the 16 bit address and 8 bit checksum)
						for(pad_index = 0; pad_index < (uint8_t)(srec_bytecount - SREC_ADDR_CHECKSUM_COUNT); pad_index++){
							// Not the 257th byte?
							if(byte_index == BOOTLOADER_MAX_BYTE_COUNT){
								throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_TALKER_TOO_BIG_ID, std::format(app_error_string::messages[APP_ERROR_TALKER_TOO_BIG_ID], BOOTLOADER_MAX_BYTE_COUNT), "");
							}

							*txbuf_p = (uint8_t)strtoul(line_str.substr(2 * pad_index + 8, 2).c_str(), NULL, 16);
							txbuf_p++;
							byte_index++;
						}
					}
				}
			}
		}
	}while(!talker_file.eof());

	// If control program is small, pad with 0x00 bytes
	for(pad_index = byte_index; pad_index < BOOTLOADER_MAX_BYTE_COUNT; pad_index++){
		*txbuf_p = 0x00;
		txbuf_p++;
		byte_index++;
	}

	// ========
	// Download
	// ========

	// Transmit leading 0xff byte and no echo expected
	std::cout << "Transmitting sync char 0xff" << std::endl;
	txbyte = 0xff;
	tx_chunk(arg_params, arg_serial_com, &txbyte, 1);

	// Transmit talker bytes
	std::cout << "Transmitting talker bytes" << std::endl;
	txbuf_p = txbuf.get_buf();
	txrx_chunk_control_program(arg_params, arg_serial_com, txbuf_p, rxbuf_p, byte_index);

	// Wait a bit to ensure the boot ROM program has timed-out
#if defined(WIN32) || defined(WIN64)
	Sleep(75);
#else
	usleep(75000);
#endif
}

void writemem_byte(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t arg_addr, uint8_t value){
	uint8_t txbuf[3];
	uint8_t rxbuf[1];

	// Transmit command
	txbuf[0] = TALKER_WRITE_CMD;
	txrx_chunk(arg_params, arg_serial_com, txbuf, rxbuf, 1, talker_echo_e::TALKER_ECHO_VERIFY_COM);

	// Transmit parameters
	txbuf[0] = 1;     // Byte count
	txbuf[1] = (uint8_t)(arg_addr >> 8 & 0xff);  // High byte of address
	txbuf[2] = (uint8_t)(arg_addr & 0xff);  // Low byte of address
	tx_chunk(arg_params, arg_serial_com, txbuf, 3);

	// Transmit memory value
	txbuf[0] = value;
	txrx_chunk(arg_params, arg_serial_com, txbuf, rxbuf, 1, talker_echo_e::TALKER_ECHO_VERIFY);
}

// Switch to Special Test mode, RBOOT = 0, IRV = 0.  This enables config register programming and also access to external memory areas
void test_mode(cl_my_params *arg_params, serial_com *arg_serial_com){
	writemem_byte(arg_params, arg_serial_com, 0x103c, 0x66);  // Write HPRIO ($103c) with 0x66
}

// Clear the block protect register (BPROT), which allows EEPROM programming for MC68HC811E2
void bprot_off(cl_my_params *arg_params, serial_com *arg_serial_com){
	writemem_byte(arg_params, arg_serial_com, 0x1035, 0x00);  // Write BPROT ($1035) with 0x00
}

// Enable EEPROM erase + write protection
void bprot_on(cl_my_params *arg_params, serial_com *arg_serial_com){
	writemem_byte(arg_params, arg_serial_com, 0x1035, 0x1f);  // Write BPROT ($1035) with 0x1f
}

// Write EEPROM byte.  Assumes address was already erased
void eeprom_prog_byte(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x02);   // Enable EEPROM latch: EELAT = 1
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store data byte at EEPROM address.  Writing this byte latches (toggles) the circuit for programming, but not actually started yet
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x03);   // Enable programming.  For EEPROM, this turns on programming voltage: EELAT = 1 (Enable EEPROM latch) and EPGM = 1 (Enable programming)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage: EELAT = 0 (Disable EEPROM latch) and EPGM = 0 (Disable programming)
}

// Bulk erase (erase all).  Note, the passed in byte is a dummy and not actually programmed
void eeprom_bulk_erase(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x06);   // Enable EEPROM latch and bulk erase mode: EELAT = 1 (enable EEPROM latch) and ERASE = 1 (bulk erase mode)
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store data byte at EEPROM address.  Writing this byte latches (toggles) the circuit for programming, but not actually started yet
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x07);   // Enable programming.  For EEPROM, this turns on programming voltage: EELAT = 1 (Enable EEPROM latch), EPGM = 1 (Enable programming) and ERASE = 1 (bulk erase mode)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage
}

// Row erase (16 bytes).  Note, the passed in byte is a dummy and not actually programmed
void eeprom_row_erase(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x0e);   // Enable EEPROM latch and row erase mode: EELAT = 1 (enable EEPROM latch), ERASE =1 (erase mode) and ROW = 1 (row erase mode)
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store data byte to EEPROM address in row.  Writing this byte latches (toggles) the circuit for programming, but not actually started yet
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x0f);   // Enable programming.  For EEPROM, this turns on programming voltage: EELAT = 1 (Enable EEPROM latch), EPGM = 1 (Enable programming), ERASE =1 (erase mode) and ROW = 1 (row erase mode)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage
}

// Byte erase.  Note, the passed in byte is a dummy and not actually programmed
void eeprom_byte_erase(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x16);   // Enable EEPROM latch and byte erase mode: EELAT = 1 (enable EEPROM latch), ERASE =1 (erase mode) and BYTE = 1 (byte erase mode)
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store dummy data byte to EEPROM address
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x17);   // Enable programming.  For EEPROM, this turns on programming voltage: EELAT = 1 (Enable EEPROM latch), EPGM = 1 (Enable programming), ERASE =1 (erase mode) and BYTE = 1 (byte erase mode)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage
}

// Write EPROM byte using PPROG register (0x103b).  Assumes address is FFs (unwritten)
void eprom_prog_byte(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x20);   // Enable EPROM latch (ELAT = 1)
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store data byte to EEPROM address in row.  Writing this byte latches (toggles) the circuit for programming, but not actually started yet
	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x21);   // Enable programming.  For EPROM (excluding MC68HC711E20), this turns on programming voltage: ELAT = 1 (Enable EPROM latch) and EPGM = 1 (Enable programming)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x103b, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage
}

// Write EPROM byte for MC68HC711E20 using EPROG register (0x1036).  Assumes address is FFs (unwritten).  Requires 12V on VPPE pin
void eprom_prog_e20_byte(cl_my_params *arg_params, serial_com *arg_serial_com, uint16_t address,  uint8_t byte){
	writemem_byte(arg_params, arg_serial_com, 0x1036, 0x20);   // Enable EPROM latch (ELAT = 1)
	writemem_byte(arg_params, arg_serial_com, address, byte);  // Store data byte to EEPROM address in row.  Writing this byte latches (toggles) the circuit for programming, but not actually started yet
	writemem_byte(arg_params, arg_serial_com, 0x1036, 0x21);   // Enable programming: ELAT = 1 (Enable EPROM latch) and EPGM = 1 (Enable programming)

	// Delay
#if defined(WIN32) || defined(WIN64)
	if(TALKER_ERASE_PROG_DELAY != 0) Sleep(TALKER_ERASE_PROG_DELAY);
#else
	if(TALKER_ERASE_PROG_DELAY != 0) sleep(TALKER_ERASE_PROG_DELAY / 1000);
#endif

	writemem_byte(arg_params, arg_serial_com, 0x1036, 0x00);  // Disable EEPROM latch and programming.  For EEPROM, this turns off programming voltage
}

void readmem(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint16_t addr;
	cl_my_file out_file;
	size_t bytes_written;
	std::string srec_line;
	std::string srec_addr_str;
	uint8_t datacount = 0;
	uint8_t srec_bytecount = SREC_ADDR_CHECKSUM_COUNT + arg_params->srec_datalen;
	uint8_t checksum = 0;
	uint32_t chunklen = 0;
	uint32_t remaining;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;

	txbuf.alloc_buf((arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	txbuf_p = txbuf.get_buf();
	rxbuf.alloc_buf((arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	rxbuf_p = rxbuf.get_buf();

	if(arg_params->full_file_name.size() > 0){
		out_file.open_file(arg_params->full_file_name, "wb");
	}

	// Write Motorola file format header S0 record
	srec_line = "S0030000FC\r\n";
	out_file.write_file(srec_line.c_str(), srec_line.size(), bytes_written);

	checksum += (arg_params->from_addr >> 8) & 0xff;
	checksum += arg_params->from_addr & 0xff;
	srec_line.clear();
	srec_addr_str = string_utils_ns::to_string_right_hex_up((uint16_t)arg_params->from_addr, 4, '0');
	remaining = arg_params->to_addr - arg_params->from_addr + 1;
	addr = (uint16_t)arg_params->from_addr;
	for(uint32_t i = 0; i < arg_params->to_addr - arg_params->from_addr + 1; i++){
		if(datacount == 0){
			if(i != 0) std::cout << std::endl;
			std::cout << string_utils_ns::to_string_right_hex_up(addr, 4, '0') << ":";
		}

		if(chunklen == 0){
			chunklen = (remaining > TALKER_MAX_BYTE_COUNT) ? TALKER_MAX_BYTE_COUNT : remaining;

			rxbuf_p = rxbuf.get_buf();

			// Transmit command
			txbuf_p[0] = TALKER_READ_CMD;
			txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, talker_echo_e::TALKER_ECHO_VERIFY_COM);

			// Transmit parameters
			txbuf_p[0] = (uint8_t)chunklen;
			txbuf_p[1] = (uint8_t)(addr >> 8 & 0xff);
			txbuf_p[2] = (uint8_t)(addr & 0xff);
			tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

			// Optimised method: Making use of driver buffering, if we transmit first and then receive a chunk, we get a huge speed up!
			txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, chunklen, talker_echo_e::TALKER_ECHO_IGNORE);

			remaining -= chunklen;
		}

		// Original method: Read memory byte (receive then transmit)
		//rxtx_chunk(arg_params, arg_serial_com, rxbuf_p, txbuf_p, 1);

		std::cout << string_utils_ns::to_string_right_hex_up((uint16_t)*rxbuf_p, 2, '0');

		if(arg_params->full_file_name.size() > 0){
			srec_line += string_utils_ns::to_string_right_hex_up((uint16_t)*rxbuf_p, 2, '0');
			datacount++;
			checksum += *rxbuf_p;

			if(datacount == (srec_bytecount - SREC_ADDR_CHECKSUM_COUNT)){
				checksum += datacount + SREC_ADDR_CHECKSUM_COUNT;
				checksum = ~checksum;

				// Create S1 record: S1 + length + checksum
				srec_line =
					"S1" +
					string_utils_ns::to_string_right_hex_up((uint16_t)(datacount + SREC_ADDR_CHECKSUM_COUNT), 2, '0') +
					srec_addr_str +
					srec_line + string_utils_ns::to_string_right_hex_up((uint16_t)checksum, 2, '0') +
					"\r\n";

				// Write Motorola file format header S1 record
				out_file.write_file(srec_line.c_str(), srec_line.size(), bytes_written);

				datacount = 0;
				checksum = 0;
				checksum += ((addr + 1) >> 8) & 0xff;
				checksum += (addr + 1) & 0xff;
				srec_line.clear();
				srec_addr_str = string_utils_ns::to_string_right_hex_up((uint16_t)(addr + 1), 4, '0');
			}
		}

		rxbuf_p++;
		chunklen--;
		addr++;
	}

	if(arg_params->full_file_name.size() > 0){
		// Do we have remaining bytes?
		if(datacount > 0){
			checksum += datacount + SREC_ADDR_CHECKSUM_COUNT;
			checksum = ~checksum;

			// Create S1 record: S1 + length + checksum
			srec_line =
				"S1" +
				string_utils_ns::to_string_right_hex_up((uint16_t)(datacount + SREC_ADDR_CHECKSUM_COUNT), 2, '0') +
				srec_addr_str +
				srec_line + string_utils_ns::to_string_right_hex_up((uint16_t)checksum, 2, '0') +
				"\r\n";

			// Write Motorola file format header S1 record
			out_file.write_file(srec_line.c_str(), srec_line.size(), bytes_written);
		}

		// Create footer S9 record
		srec_line = "S9030000FC\r\n";

		// Write Motorola file format termination S9 record
		out_file.write_file(srec_line.c_str(), srec_line.size(), bytes_written);
	}

	std::cout << std::endl << "Read successfully completed" << std::endl;
}

void readmem_verify(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint32_t i;
	cl_my_file in_file;
	std::string line_str;
	std::string ic_line_str;
	uint16_t srec_addr;
	uint8_t srec_datacount;
	uint32_t total_databytes = 0;
	uint8_t file_byte;
	uint32_t line_mismatch_count;
	uint32_t mismatch_count = 0;
	uint32_t line_ignore_count;
	uint32_t ignore_count = 0;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;

	txbuf.alloc_buf((arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	txbuf_p = txbuf.get_buf();
	rxbuf.alloc_buf((arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	rxbuf_p = rxbuf.get_buf();

	in_file.open_file(arg_params->full_file_name, "rb");

	do{
		// Read line from file
		in_file.read_file_line(line_str);

		// Record string length must be atleast the minimum of 8
		if(line_str.size() >= 8){
			// We only want S1 records
			if(line_str.substr(0, 2) == "S1"){
				std::cout << "File: "<< line_str << std::endl;
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				total_databytes += srec_datacount;
				ic_line_str.clear();
				line_mismatch_count = 0;
				line_ignore_count = 0;
				rxbuf_p = rxbuf.get_buf();

				// Transmit command
				txbuf_p[0] = TALKER_READ_CMD;
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, talker_echo_e::TALKER_ECHO_VERIFY_COM);

				// Transmit parameters
				txbuf_p[0] = (uint8_t)srec_datacount;
				txbuf_p[1] = (uint8_t)(srec_addr >> 8 & 0xff);
				txbuf_p[2] = (uint8_t)(srec_addr & 0xff);
				tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

				// Optimised method: Making use of driver buffering, if we transmit first and then receive a chunk, we get a huge speed up!
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, srec_datacount, talker_echo_e::TALKER_ECHO_IGNORE);

				// Loop each data byte
				for(i = 0; i < srec_datacount; i++){
					// Original method: Read memory byte (receive then transmit)
					//rxtx_chunk(arg_params, arg_serial_com, rxbuf_p, txbuf_p, 1);

					if(!arg_params->verify_config && srec_addr == HC11_CONFIG_ADDR){
						line_ignore_count++;
						ignore_count++;
					}else{
						file_byte = (uint8_t)strtoul(line_str.substr(2 * i + 8, 2).c_str(), NULL, 16);
						if(*rxbuf_p != file_byte){
							line_mismatch_count++;
							mismatch_count++;
						}
					}

					ic_line_str += string_utils_ns::to_string_right_hex_up((uint16_t)*rxbuf_p, 2, '0');

					srec_addr++;
					rxbuf_p++;
				}

				if(line_mismatch_count && line_ignore_count){
					std::cout << "Rx  :         " << ic_line_str << " = " << line_mismatch_count << " mismatched, " << line_ignore_count << " ignored" << std::endl;
				}else if(line_mismatch_count){
					std::cout << "Rx  :         " << ic_line_str << " = " << line_mismatch_count << " mismatched" << std::endl;
				}else if(line_ignore_count == srec_datacount){
					std::cout << "Rx  :         " << ic_line_str << " = " << line_ignore_count << " ignored" << std::endl;
				}else if(line_ignore_count){
					std::cout << "Rx  :         " << ic_line_str << " = " << (uint16_t)srec_datacount << " matched, " << line_ignore_count << " ignored" << std::endl;
				}else{
					std::cout << "Rx  :         " << ic_line_str << " = " << (uint16_t)srec_datacount << " matched" << std::endl;
				}
			}
		}
	}while(!in_file.eof());

	if(mismatch_count){
		if(ignore_count){
			std::cout << "FAILED! " << total_databytes << " total bytes, " << mismatch_count << " mismatched, " << ignore_count << " ignored" << std::endl;
		}else{
			std::cout << "FAILED! " << total_databytes << " total bytes, " << mismatch_count << " mismatched" << std::endl;
		}
	}else{
		if(ignore_count){
			std::cout << "PASSED. " << total_databytes << " total bytes, " << total_databytes - ignore_count << " matched, " << ignore_count <<  " ignored" << std::endl;
		}else{
			std::cout << "PASSED. " << total_databytes << " total bytes, " << total_databytes - ignore_count << " matched" << std::endl;
		}
	}
}

void writemem_hexstr(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint16_t addr;
	uint8_t chunklen = 0;
	uint32_t remaining;
	uint32_t total_bytes;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;

	txbuf.alloc_buf((arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	txbuf_p = txbuf.get_buf();
	rxbuf.alloc_buf((arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	rxbuf_p = rxbuf.get_buf();

	if(arg_params->data.size() > 0){
		if(arg_params->data.size() % 2){
			arg_params->data = "0" + arg_params->data;
		}

		total_bytes = arg_params->data.size() / 2;
		addr = arg_params->from_addr;
		remaining = total_bytes;

		std::cout << string_utils_ns::to_string_right_hex_up(arg_params->from_addr, 4, '0') << ":" << arg_params->data << std::endl;

		// Loop each data byte
		while(remaining){
			chunklen = (remaining > TALKER_MAX_BYTE_COUNT) ? TALKER_MAX_BYTE_COUNT : remaining;

			// Transmit command
			txbuf_p[0] = TALKER_WRITE_CMD;
			txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, talker_echo_e::TALKER_ECHO_VERIFY_COM);

			// Transmit parameters
			txbuf_p[0] = (uint8_t)chunklen;
			txbuf_p[1] = (uint8_t)(addr >> 8 & 0xff);
			txbuf_p[2] = (uint8_t)(addr & 0xff);
			tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

			// Transmit memory values
			for(uint16_t i = 0; i < chunklen; i++){ txbuf_p[i] = (uint8_t)strtoul(arg_params->data.substr(2 * (addr - arg_params->from_addr + i), 2).c_str(), NULL, 16); }
			txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, chunklen, talker_echo_e::TALKER_ECHO_VERIFY);

			addr += chunklen;
			remaining -= chunklen;
		}
	}
}

void writemem_file(cl_my_params *arg_params, serial_com *arg_serial_com){
	cl_my_file in_file;
	std::string line_str;
	std::string srec_type;
	uint8_t srec_datacount;
	uint16_t srec_addr;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;

	txbuf.alloc_buf((arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	txbuf_p = txbuf.get_buf();
	rxbuf.alloc_buf((arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT);
	rxbuf_p = rxbuf.get_buf();

	in_file.open_file(arg_params->full_file_name, "rb");

	do{
		// Read line from file
		in_file.read_file_line(line_str);

		// Record string length must be atleast the minimum of 8
		if(line_str.size() >= 8){
			// We only want S1 records
			srec_type = line_str.substr(0, 2);
			if(srec_type == "S1"){
				std::cout << line_str << std::endl;
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);  // Extract srecord address

				// Transmit command
				txbuf_p[0] = TALKER_WRITE_CMD;
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, talker_echo_e::TALKER_ECHO_VERIFY_COM);

				// Transmit parameters
				txbuf_p[0] = (uint8_t)srec_datacount;
				txbuf_p[1] = (uint8_t)(srec_addr >> 8 & 0xff);
				txbuf_p[2] = (uint8_t)(srec_addr & 0xff);
				tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

				// Transmit memory values
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, srec_datacount, talker_echo_e::TALKER_ECHO_VERIFY);
			}
		}
	}while(!in_file.eof());
}

// Note, when programming the CONFIG register 0x103f the new value cannot be read until a reset
void write_ee(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint32_t i;
	uint32_t bytecount = 0;
	cl_my_file in_file;
	std::string line_str;
	std::string srec_type;
	uint8_t srec_datacount;
	uint16_t srec_addr;
	uint8_t txbyte;

	in_file.open_file(arg_params->full_file_name, "rb");

	do{
		// Read line from file
		in_file.read_file_line(line_str);

		// Record string length must be atleast the minimum of 8
		if(line_str.size() >= 8){
			// We only want S1 records
			srec_type = line_str.substr(0, 2);
			if(srec_type == "S1"){
				//std::cout << line_str << std::endl;
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);  // Extract srecord address

				std::cout << string_utils_ns::to_string_right_hex_up(srec_addr, 4, '0') << ":";
				// Loop each data byte
				for(i = 0; i < srec_datacount; i++){
					txbyte = (uint8_t)strtoul(line_str.substr(2 * i + 8, 2).c_str(), NULL, 16);
					std::cout << string_utils_ns::to_string_right_hex_up((uint16_t)(txbyte), 2, '0');

					if(srec_addr == HC11_CONFIG_ADDR){
						eeprom_bulk_erase(arg_params, arg_serial_com, srec_addr, txbyte);  // Bulk erase instead of byte erase for compatibility with  A1, A8 and A2 series
					}else{
						eeprom_byte_erase(arg_params, arg_serial_com, srec_addr, txbyte);
					}
					eeprom_prog_byte(arg_params, arg_serial_com, srec_addr, txbyte);

					srec_addr++;
					bytecount++;
				}
				std::cout << std::endl;
			}
		}
	}while(!in_file.eof());
}

void write_e(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint32_t i;
	uint32_t bytecount = 0;
	cl_my_file in_file;
	std::string line_str;
	std::string srec_type;
	uint8_t srec_datacount;
	uint16_t srec_addr;
	uint8_t txbyte;

	in_file.open_file(arg_params->full_file_name, "rb");

	do{
		// Read line from file
		in_file.read_file_line(line_str);

		// Record string length must be atleast the minimum of 8
		if(line_str.size() >= 8){
			// We only want S1 records
			srec_type = line_str.substr(0, 2);
			if(srec_type == "S1"){
				std::cout << line_str << std::endl;
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);  // Extract srecord address

				// Loop each data byte
				for(i = 0; i < srec_datacount; i++){
					txbyte = (uint8_t)strtoul(line_str.substr(2 * i + 8, 2).c_str(), NULL, 16);

					eprom_prog_byte(arg_params, arg_serial_com, srec_addr, txbyte);

					srec_addr++;
					bytecount++;
				}
			}
		}
	}while(!in_file.eof());
}

void write_e20(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint32_t i;
	uint32_t bytecount = 0;
	cl_my_file in_file;
	std::string line_str;
	std::string srec_type;
	uint8_t srec_datacount;
	uint16_t srec_addr;
	uint8_t txbyte;

	in_file.open_file(arg_params->full_file_name, "rb");

	do{
		// Read line from file
		in_file.read_file_line(line_str);

		// Record string length must be atleast the minimum of 8
		if(line_str.size() >= 8){
			// We only want S1 records
			srec_type = line_str.substr(0, 2);
			if(srec_type == "S1"){
				std::cout << line_str << std::endl;
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);  // Extract srecord address

				// Loop each data byte
				for(i = 0; i < srec_datacount; i++){
					txbyte = (uint8_t)strtoul(line_str.substr(2 * i + 8, 2).c_str(), NULL, 16);

					eprom_prog_e20_byte(arg_params, arg_serial_com, srec_addr, txbyte);

					srec_addr++;
					bytecount++;
				}
			}
		}
	}while(!in_file.eof());
}

bool prog_prompt_write(uint8_t arg_write_cmd_code){
	switch(arg_write_cmd_code){
	case CMD_WRITE_EEPROM:
		std::cout << "EEPROM PROGRAMMING CONFIRMATION:" << std::endl;
		std::cout << "Note, current content will be lost, are you sure you want to write (y/[n])? ";
		break;
	case CMD_WRITE_EPROM:
	case CMD_WRITE_EPROM_E20:
		std::cout << "E20 EPROM PROGRAMMING CONFIRMATION:" << std::endl;
		std::cout << "Note, programmed zero bits will become permanent, if yes, please apply the" << std::endl;
		std::cout << "programming voltage (12V) on VPPE pin now before continuing, are you sure " << std::endl;
		std::cout << "you want to write (y/[n])? ";
		break;
	}

	char buf[256];
	if(fgets(buf, sizeof(buf), stdin) != NULL){
		if(buf[0] == 'y'){
			return true;
		}
	}

	return false;
}

bool process_cmd_line(cl_my_params *arg_params){
	serial_com serial;

	serial.open_handle(arg_params->dev_path);  // Open serial COM port
	serial.set_timeout(arg_params->timeoutms);  // Set serial COM port timeout
	serial.purge();  // Clear buffer

	switch(arg_params->cmd){
		case CMD_UPTALKER:
			if(arg_params->use_fast){
				serial.set_params(7618, 8, NOPARITY, ONESTOPBIT, false);  // Set to bootloader ROM port settings
			}else{
				serial.set_params(1200, 8, NOPARITY, ONESTOPBIT, false);  // Set to bootloader ROM port settings
			}

			send_control_program(arg_params, &serial);  // Download custom EEPROM control program to MCU RAM
			std::cout << "Download completed successfully" << std::endl;

			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings

			std::cout << "Switching to special test mode" << std::endl;
			test_mode(arg_params, &serial);

			break;
		case CMD_READ:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
			std::cout << "Reading memory" << std::endl;
			readmem(arg_params, &serial);

			break;
		case CMD_READ_VERIFY:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
			std::cout << "Verifying file with memory" << std::endl;
			readmem_verify(arg_params, &serial);

			break;
		case CMD_WRITEHEXSTR:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
			std::cout << "Writing normal memory" << std::endl;
			writemem_hexstr(arg_params, &serial);

			break;
		case CMD_WRITE:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
			std::cout << "Writing normal memory" << std::endl;
			writemem_file(arg_params, &serial);
			bprot_on(arg_params, &serial);

			break;
		case CMD_WRITE_EEPROM:
			if(prog_prompt_write(CMD_WRITE_EEPROM)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
				std::cout << "Writing EEPROM" << std::endl;
				bprot_off(arg_params, &serial);
				write_ee(arg_params, &serial);
				bprot_on(arg_params, &serial);
			}

			break;
		case CMD_WRITE_EPROM:
			if(prog_prompt_write(CMD_WRITE_EPROM)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
				std::cout << "Writing EPROM (non E20)" << std::endl;
				write_e(arg_params, &serial);
			}

			break;
		case CMD_WRITE_EPROM_E20:
			if(prog_prompt_write(CMD_WRITE_EPROM_E20)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);
				std::cout << "Writing EPROM (E20, 12V)" << std::endl;
				write_e20(arg_params, &serial);
			}

			break;
	}

	return true;
}

int main(int arg_c, char *arg_v[]){
	cl_my_params my_params;

	try{
		if(arg_c > 1){
			parse_params(arg_c, arg_v, &my_params);
			process_cmd_line(&my_params);
		}else{
			usage(arg_v[0]);
		}
	}catch(tru_exception &ex){
		std::cout << "\nError: " << ex.get_error() << std::endl;
		return ex.get_code();
	}

	return 0;
}
