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

	Tru11
	-----

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
	
	This command line program requires the tru11 talker program (talker firmware)
	to be downloaded into the MCU RAM first.

	Developer   : Truong Hy
	Date        : 03 Aug 2024
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
#define TALKER_MAX_BYTE_COUNT     256
#define TALKER_READ_CMD           0x01
#define TALKER_WRITE_CMD          0x02
#define TALKER_WRITE_EE_CMD       0x03
#define TALKER_WRITE_E_CMD        0x04
#define TALKER_WRITE_E20_CMD      0x05
#define SREC_ADDR_CHECKSUM_COUNT  3
#define HC11_CONFIG_ADDR          0x103f

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

// Generic transmit and receive in blocks
void txrx_chunk(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len, bool arg_verify_echo){
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
		if(arg_verify_echo){
			verify_echo(txbuf_p, rxbuf_p, chunklen);
		}

		txbuf_p += chunklen;
		rxbuf_p += chunklen;
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
				// A workaround for some adapters, since last byte is missing we read one less normally
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

// Transmit and receive in blocks specifically for writing memory
void txrx_chunk_write(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t *arg_txbuf, uint8_t *arg_rxbuf, uint32_t arg_len, bool arg_is_prog){
	uint8_t *txbuf_p = arg_txbuf;
	uint8_t *rxbuf_p = arg_rxbuf;
	uint32_t chunklen = 0;
	uint32_t xferredlen;
	uint32_t remaining;

	remaining = arg_len;
	while(remaining){
		if(arg_is_prog){
			chunklen = (remaining > arg_params->serial_prog_txbuf_size) ? arg_params->serial_prog_txbuf_size : remaining;
		}else{
			chunklen = (remaining > arg_params->serial_txbuf_size) ? arg_params->serial_txbuf_size : remaining;
		}
		remaining -= chunklen;

		xferredlen = arg_serial_com->write_port(txbuf_p, chunklen);
		if(xferredlen != chunklen){
			throw tru_exception(__func__, TRU_EXCEPT_SRC_VEN, APP_ERROR_TX_FAIL_ID, app_error_string::messages[APP_ERROR_TX_FAIL_ID], std::format(app_error_string::messages[APP_ERROR_XFER_INFO_ID], chunklen, xferredlen));
		}

		rx_chunk(arg_params, arg_serial_com, rxbuf_p, chunklen);

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
	uint32_t len;

	len = (arg_params->serial_txbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_txbuf_size : BOOTLOADER_MAX_BYTE_COUNT;
	txbuf.alloc_buf(len);
	txbuf_p = txbuf.get_buf();

	len = (arg_params->serial_rxbuf_size > BOOTLOADER_MAX_BYTE_COUNT) ? arg_params->serial_rxbuf_size : BOOTLOADER_MAX_BYTE_COUNT;
	rxbuf.alloc_buf(len);
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
}

void readmem(cl_my_params *arg_params, serial_com *arg_serial_com){
	uint32_t i;
	uint16_t addr;
	cl_my_file out_file;
	size_t bytes_written;
	std::string srec_line;
	std::string srec_addr_str;
	uint8_t datacount = 0;
	uint8_t srec_bytecount = SREC_ADDR_CHECKSUM_COUNT + arg_params->srec_datalen;
	uint8_t checksum = 0;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;
	uint32_t chunklen = 0;
	uint32_t remaining;

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

	while(remaining){
		chunklen = (remaining > TALKER_MAX_BYTE_COUNT) ? TALKER_MAX_BYTE_COUNT : remaining;

		rxbuf_p = rxbuf.get_buf();

		// Transmit command
		txbuf_p[0] = TALKER_READ_CMD;
		txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, true);

		// Transmit parameters
		txbuf_p[0] = (uint8_t)chunklen;
		txbuf_p[1] = (uint8_t)(addr >> 8 & 0xff);
		txbuf_p[2] = (uint8_t)(addr & 0xff);
		tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

		// Read a chunk of memory
		rx_chunk(arg_params, arg_serial_com, rxbuf_p, chunklen);

		remaining -= chunklen;

		for(i = 0; i < chunklen; i++){
			if(datacount == 0){
				std::cout << string_utils_ns::to_string_right_hex_up(addr, 4, '0') << ":";
			}

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

					std::cout << std::endl;
				}
			}

			rxbuf_p++;
			addr++;
		}
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

			std::cout << std::endl;
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
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;
	uint32_t line_mismatch_count;
	uint32_t mismatch_count = 0;
	uint32_t line_ignore_count;
	uint32_t ignore_count = 0;

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
				std::cout << "File: " << line_str << std::endl;
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count

				ic_line_str.clear();
				line_mismatch_count = 0;
				line_ignore_count = 0;
				rxbuf_p = rxbuf.get_buf();

				// Transmit command
				txbuf_p[0] = TALKER_READ_CMD;
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, true);

				// Transmit parameters
				txbuf_p[0] = (uint8_t)srec_datacount;
				txbuf_p[1] = (uint8_t)(srec_addr >> 8 & 0xff);
				txbuf_p[2] = (uint8_t)(srec_addr & 0xff);
				tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

				// Read a chunk of memory
				rx_chunk(arg_params, arg_serial_com, rxbuf_p, srec_datacount);

				// Loop each data byte
				for(i = 0; i < srec_datacount; i++){
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

				total_databytes += srec_datacount;
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

void writemem_hexstr(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t arg_write_cmd_code){
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
			txbuf_p = txbuf.get_buf();
			rxbuf_p = rxbuf.get_buf();
			txbuf_p[0] = arg_write_cmd_code;
			txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, true);

			// Transmit parameters
			txbuf_p[0] = (uint8_t)chunklen;
			txbuf_p[1] = (uint8_t)(addr >> 8 & 0xff);
			txbuf_p[2] = (uint8_t)(addr & 0xff);
			tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

			// Write and receive a chunk of memory
			txbuf_p = txbuf.get_buf();
			rxbuf_p = rxbuf.get_buf();
			for(uint16_t i = 0; i < chunklen; i++){
				txbuf_p[i] = (uint8_t)strtoul(arg_params->data.substr(2 * (addr - arg_params->from_addr + i), 2).c_str(), NULL, 16);
			}
			txrx_chunk_write(arg_params, arg_serial_com, txbuf_p, rxbuf_p, chunklen, false);

			addr += chunklen;
			remaining -= chunklen;
		}
	}
}

// Note, when programming the CONFIG register 0x103f the new value cannot be read until a reset
void writemem_file(cl_my_params *arg_params, serial_com *arg_serial_com, uint8_t arg_write_cmd_code){
	uint32_t i;
	uint32_t bytecount = 0;
	cl_my_file in_file;
	std::string line_str;
	std::string srec_type;
	uint8_t srec_datacount;
	uint32_t total_databytes = 0;
	uint16_t srec_addr;
	cl_my_buf txbuf;
	uint8_t *txbuf_p;
	cl_my_buf rxbuf;
	uint8_t *rxbuf_p;
	uint32_t line_mismatch_count;
	uint32_t mismatch_count = 0;
	uint32_t line_ignore_count;
	uint32_t ignore_count = 0;

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
				//std::cout << line_str << std::endl;
				srec_datacount = (uint8_t)strtoul(line_str.substr(2, 2).c_str(), NULL, 16) - SREC_ADDR_CHECKSUM_COUNT;  // Extract srecord data byte count
				srec_addr = (uint16_t)strtoul(line_str.substr(4, 4).c_str(), NULL, 16);  // Extract srecord address

				std::cout << string_utils_ns::to_string_right_hex_up(srec_addr, 4, '0') << ":";

				// Transmit command
				txbuf_p = txbuf.get_buf();
				rxbuf_p = rxbuf.get_buf();
				txbuf_p[0] = arg_write_cmd_code;
				txrx_chunk(arg_params, arg_serial_com, txbuf_p, rxbuf_p, 1, true);

				// Transmit parameters
				txbuf_p[0] = (uint8_t)srec_datacount;
				txbuf_p[1] = (uint8_t)(srec_addr >> 8 & 0xff);
				txbuf_p[2] = (uint8_t)(srec_addr & 0xff);
				tx_chunk(arg_params, arg_serial_com, txbuf_p, 3);

				// Loop each data byte appending them into a buffer
				for(i = 0; i < srec_datacount; i++){
					*txbuf_p = (uint8_t)strtoul(line_str.substr(2 * i + 8, 2).c_str(), NULL, 16);
					std::cout << string_utils_ns::to_string_right_hex_up((uint16_t)(*txbuf_p), 2, '0');

					txbuf_p++;
					bytecount++;
				}

				// Write and receive a chunk of memory
				txbuf_p = txbuf.get_buf();
				rxbuf_p = rxbuf.get_buf();
				txrx_chunk_write(arg_params, arg_serial_com, txbuf_p, rxbuf_p, srec_datacount, arg_write_cmd_code != TALKER_WRITE_CMD);

				line_mismatch_count = 0;
				line_ignore_count = 0;
				for(i = 0; i < srec_datacount; i++){
					if(!arg_params->verify_config && srec_addr == HC11_CONFIG_ADDR){  // We cannot read the new config value until after a reset so we will not verify it
						line_ignore_count++;
						ignore_count++;
					}else{
						if(txbuf_p[i] != rxbuf_p[i]){
							line_mismatch_count++;
							mismatch_count++;

						}
					}
					srec_addr++;
				}

				if(line_mismatch_count && line_ignore_count){
					std::cout << " = " << line_mismatch_count << " mismatched, " << line_ignore_count << " ignored" << std::endl;
				}else if(line_mismatch_count){
					std::cout << " = " << line_mismatch_count << " mismatched" << std::endl;
				}else if(line_ignore_count == srec_datacount){
					std::cout << " = " << line_ignore_count << " ignored" << std::endl;
				}else if(line_ignore_count){
					std::cout << " = " << (uint16_t)srec_datacount << " matched, " << line_ignore_count << " ignored" << std::endl;
				}else{
					std::cout << " = " << (uint16_t)srec_datacount << " matched" << std::endl;
				}

				total_databytes += srec_datacount;
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

bool prog_prompt_write(uint8_t arg_write_cmd_code){
	switch(arg_write_cmd_code){
		case TALKER_WRITE_EE_CMD:
			std::cout << "EEPROM PROGRAMMING CONFIRMATION:" << std::endl;
			std::cout << "Note, current content will be lost, are you sure you want to write (y/[n])? ";
			break;
		case TALKER_WRITE_E_CMD:
		case TALKER_WRITE_E20_CMD:
			std::cout << "EPROM PROGRAMMING CONFIRMATION:" << std::endl;
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

#if defined(WIN32) || defined(WIN64)
			Sleep(75);  // We need to wait a bit for the downloaded program to become ready
#else
			usleep(75000);  // We need to wait a bit for the downloaded program to become ready
#endif

			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings

			break;
		case CMD_READ:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
			std::cout << "Reading memory" << std::endl;
			readmem(arg_params, &serial);

			break;
		case CMD_READ_VERIFY:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
			std::cout << "Reading & verifying memory" << std::endl;
			readmem_verify(arg_params, &serial);

			break;
		case CMD_WRITE_NORMAL_HEXSTR:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
			std::cout << "Writing normal memory" << std::endl;
			writemem_hexstr(arg_params, &serial, TALKER_WRITE_CMD);

			break;
		case CMD_WRITE_EE_HEXSTR:
			if(prog_prompt_write(TALKER_WRITE_EE_CMD)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
				std::cout << "Writing EEPROM" << std::endl;
				writemem_hexstr(arg_params, &serial, TALKER_WRITE_EE_CMD);
			}

			break;
		case CMD_WRITE_NORMAL:
			serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
			std::cout << "Writing & verifying normal memory" << std::endl;
			writemem_file(arg_params, &serial, TALKER_WRITE_CMD);

			break;
		case CMD_WRITE_EE:
			if(prog_prompt_write(TALKER_WRITE_EE_CMD)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
				std::cout << "Writing & verifying EEPROM" << std::endl;
				writemem_file(arg_params, &serial, TALKER_WRITE_EE_CMD);
			}

			break;
		case CMD_WRITE_E:
			if(prog_prompt_write(TALKER_WRITE_E_CMD)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
				std::cout << "Writing & verifying EPROM (non E20)" << std::endl;
				writemem_file(arg_params, &serial, TALKER_WRITE_E_CMD);
				std::cout << "Please remove programming voltage (12V) now before powering of the MCU" << std::endl;
			}

			break;
		case CMD_WRITE_E20:
			if(prog_prompt_write(TALKER_WRITE_E20_CMD)){
				serial.set_params(9600, 8, NOPARITY, ONESTOPBIT, false);  // Set to talker port settings
				std::cout << "Writing & verifying EPROM (E20, 12V)" << std::endl;
				writemem_file(arg_params, &serial, TALKER_WRITE_E20_CMD);
				std::cout << "Please remove programming voltage (12V) now before powering of the MCU" << std::endl;
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
