#ifndef CMD_LINE_H
#define CMD_LINE_H

#include "my_buf.h"
#include <string>

// Command line commands
typedef enum{
	CMD_NONE,
	CMD_UPTALKER,
	CMD_READ,
	CMD_READ_VERIFY,
	CMD_WRITE_NORMAL_HEXSTR,
	CMD_WRITE_EE_HEXSTR,
	CMD_WRITE_NORMAL,
	CMD_WRITE_EE,
	CMD_WRITE_E,
	CMD_WRITE_E20
}cmd_type;

// Note, because the 68HC11 has a 1 byte SCI (UART) receive buffer, the code (if fast enough) can read out one and receive another,
// this means we are able to set our application UART buffer size to 2 even if the OS UART driver does not support buffering.
// Programming EEPROM/EPROM require a delay in the 68HC11 firmware, and due to how Windows UART driver implement buffering - it
// seems to be affected by timing, this prevents the read/write ahead buffering on the host side (Windows) from working so we can
// only set that to 2 or 1
class cl_my_params{
public:
	unsigned char cmd;
	std::string dev_path;
	bool use_fast;
	uint32_t serial_rxbuf_size;
	uint32_t serial_txbuf_size;
	uint32_t serial_prog_txbuf_size;
	uint32_t timeoutms;
	uint8_t srec_datalen;
	bool verify_config;
	std::string talker_filename;
	std::string full_file_name;
	std::string data;
	uint32_t from_addr;
	uint32_t to_addr;

	cl_my_params() :
		cmd(CMD_NONE),
		use_fast(false),
		serial_rxbuf_size(256),  // If the serial driver doesn't buffer, set this to 2 or 1
		serial_txbuf_size(256),  // If the serial driver doesn't buffer, set this to 2 or 1
		serial_prog_txbuf_size(2),
		timeoutms(1000),
		srec_datalen(16),
		verify_config(false),
		talker_filename("talker.s19"),
		from_addr(0),
		to_addr(0){
	}
};

template<class T>
bool parse_param_val(std::string param, std::string key, T &value){
	std::string param_substr;

	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			param_substr = param.substr(key.size(), param.size());
			if(!param_substr.empty()){
				//value = str_to_num<T>(param_substr);
				//value = (T)strtol(param_substr.c_str(), NULL, 0);
				value = (T)atof(param_substr.c_str());
			}else{
				value = 0;
			}

			return true;
		}
	}

	return false;
}

template<class T>
bool parse_param_val_int(std::string param, std::string key, T &value){
	std::string param_substr;

	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			param_substr = param.substr(key.size(), param.size());
			if(!param_substr.empty()){
				//value = str_to_num<T>(param_substr);
				value = (T)strtol(param_substr.c_str(), NULL, 0);
				//value = (T)atof(param_substr.c_str());
			}else{
				value = 0;
			}

			return true;
		}
	}

	return false;
}

template<class T>
bool parse_param_val_uint(std::string param, std::string key, T &value){
	std::string param_substr;

	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			param_substr = param.substr(key.size(), param.size());
			if(!param_substr.empty()){
				//value = str_to_num<T>(param_substr);
				value = (T)strtoul(param_substr.c_str(), NULL, 0);
				//value = (T)atof(param_substr.c_str());
			}else{
				value = 0;
			}

			return true;
		}
	}

	return false;
}

bool parse_param_exist(std::string param, std::string key);
bool parse_param_str(std::string param, std::string key, std::string &value);
bool parse_param_yn(std::string param, std::string key, bool &value);
bool parse_param_hex_str(std::string param, std::string key, std::string &value);
void usage(char *arg_0);
bool parse_params_search(char *cmdl_param, cl_my_params *my_params);
void parse_params(int arg_c, char *arg_v[], cl_my_params *my_params);

#endif
