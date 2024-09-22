#include "cmd_line.h"

bool parse_param_exist(std::string param, std::string key){
	// Len of param is correct or longer?
	if(param.size() == key.size()){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			return true;
		}
	}

	return false;
}

bool parse_param_str(std::string param, std::string key, std::string &value){
	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			value = param.substr(key.size(), param.size());

			return true;
		}
	}

	return false;
}

bool parse_param_yn(std::string param, std::string key, bool &value){
	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			if(param.substr(key.size(), 1) == "y"){
				value = true;
			}else{
				value = false;
			}

			return true;
		}
	}

	return false;
}

bool parse_param_hex_str(std::string param, std::string key, std::string &value){
	char ch;
	std::string::size_type i;

	value.clear();
	// Len of param is correct or longer?
	if(param.size() >= (key.size() + 1)){
		// Compares param to key word
		if(param.compare(0, key.size(), key) == 0){
			for(i = 0; i < (param.size() - key.size()); i += 2){
				ch = (char)strtoul(param.substr(key.size() + i, 2).c_str(), NULL, 16);
				value += ch;
			}
			return true;
		}
	}
	return false;
}

void usage(char *arg_0){
	printf("%s ver 20240730. Truong Hy\n", arg_0);
	printf("Usage:\n");
	printf(" %s <devparams> <cmdparams>\n", arg_0);
	printf("devparams:\n");
	printf("  path=<s>       : serial port path\n");
	printf("  [timeout=<n>]  : timeout ms\n");
	printf("\n");
	printf("cmdparams:\n");
	printf("uptalker        : upload talker\n");
	printf("  [fast=<y|n>]  : upload talker with 7812 baud\n");
	printf("  [talker=<s>]  : talker file\n");
	printf("read            : read memory to file\n");
	printf("  from_addr=<n>  : from address\n");
	printf("  to_addr=<n>    : to address\n");
	printf("  file=<s>       : file\n");
	printf("verify          : verify memory with file\n");
	printf("  file=<s>       : file\n");
	printf("write_hex       : write hex string to memory\n");
	printf("  from_addr=<n>  : from address\n");
	printf("  hex=<s>        : hex string\n");
	printf("write           : write file to memory\n");
	printf("  file=<s>       : file\n");
	printf("write_ee        : write file to EEPROM\n");
	printf("  file=<s>       : file\n");
	printf("write_e         : write file to EPROM (non E20)\n");
	printf("  file=<s>       : file\n");
	printf("write_e20       : write file to EPROM (E20, 12V)\n");
	printf("  file=<s>       : file\n");
}

bool parse_params_search(char *cmdl_param, cl_my_params *my_params){
	if(parse_param_exist(cmdl_param, "uptalker")){
		my_params->cmd = CMD_UPTALKER;
		return true;
	}
	if(parse_param_exist(cmdl_param, "read")){
		my_params->cmd = CMD_READ;
		return true;
	}
	if(parse_param_exist(cmdl_param, "verify")){
		my_params->cmd = CMD_READ_VERIFY;
		return true;
	}
	if(parse_param_exist(cmdl_param, "writehex")){
		my_params->cmd = CMD_WRITEHEXSTR;
		return true;
	}
	if(parse_param_exist(cmdl_param, "write")){
		my_params->cmd = CMD_WRITE;
		return true;
	}
	if(parse_param_exist(cmdl_param, "write_ee")){
		my_params->cmd = CMD_WRITE_EEPROM;
		return true;
	}
	if(parse_param_exist(cmdl_param, "write_e")){
		my_params->cmd = CMD_WRITE_EPROM;
		return true;
	}
	if(parse_param_exist(cmdl_param, "write_e20")){
		my_params->cmd = CMD_WRITE_EPROM_E20;
		return true;
	}
	if(parse_param_str(cmdl_param, "path=", my_params->dev_path)){
		return true;
	}
	if(parse_param_val_uint(cmdl_param, "timeout=", my_params->timeoutms)){
		return true;
	}
	if(parse_param_str(cmdl_param, "talker=", my_params->talker_filename)){
		return true;
	}
	if(parse_param_yn(cmdl_param, "fast=", my_params->use_fast)){
		return true;
	}
	if(parse_param_val_uint(cmdl_param, "from_addr=", my_params->from_addr)){
		return true;
	}
	if(parse_param_val_uint(cmdl_param, "to_addr=", my_params->to_addr)){
		return true;
	}
	if(parse_param_str(cmdl_param, "file=", my_params->full_file_name)){
		return true;
	}
	if(parse_param_str(cmdl_param, "hex=", my_params->data)){
		return true;
	}

	return false;
}

void parse_params(int arg_c, char *arg_v[], cl_my_params *my_params){
	// Iterate to search for parameters
	for(int i = 1; i < arg_c; i++){
		parse_params_search(arg_v[i], my_params);
	}
}
