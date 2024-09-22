#ifndef MY_BUF_H
#define MY_BUF_H

#include "tru_exception.h"
#include <stdlib.h>
#include <cstdint>

class cl_my_buf{
protected:
	unsigned char *_buf;
	size_t _len;

public:
	cl_my_buf() :
		_buf(NULL),
		_len(0){
	}
	~cl_my_buf(){
		if(_buf != NULL){
			free(_buf);
			_buf = NULL;
			_len = 0;
		}
	}
	unsigned char* get_buf(){
		return _buf;
	}
	void alloc_buf(uint32_t m_arg_len){
		if(_buf != NULL){
			free(_buf);
			_buf = NULL;
			_len = 0;
		}

		_buf = (unsigned char*)malloc(m_arg_len);
		if(_buf == NULL){
			errno = ENOMEM;
			throw tru_exception::get_clib_last_error(__func__, "");
		}
		_len = m_arg_len;
	}
	size_t len(){
		return _len;
	}
};

#endif
