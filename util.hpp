
// You may only use fgets() to pull input from stdin
// You may use any print function to stdout to print 
// out chat messages
#include <stdio.h>

// You may use memory allocators and helper functions 
// (e.g., rand()).  You may not use system().
#include <stdlib.h>

#include <inttypes.h>
#include <time.h>

#include <ostream>
#include <iostream>
#include <vector>
#include <cstring>
#include <ctime>

#ifndef UTIL_H_
#define UTIL_H_

#define ADDR_PTR uint64_t 
#define CYCLES uint32_t
#define CACHE_SIZE 32768
#define CACHE_ASSOC 8
#define CACHE_LINESIZE 64
#define CACHE_SETS (CACHE_SIZE / CACHE_LINESIZE) / CACHE_ASSOC

#define CLOCK_TICKS 1
#define SYNC_GIVE_UP 50000
#define MALLOC_GIVE_UP 10 // DO NOT TURN UP
#define MASTER 0
#define SLAVE 1
#define SYNC_DEBUG 0
#define SYNC 4000

#define ACK 0xA7
#define SYNACK 0x7A
#define NACK 0x9B
#define SUCCESS 0x07

CYCLES measure_one_block_access_time(ADDR_PTR addr);


class CovertChannel{
	std::vector <ADDR_PTR> channels;
	uint32_t cache_set;
	uint32_t role;
	uint64_t sync_period;
	uint64_t threshold;
	char* buffer;

public:
	CovertChannel(uint32_t mode, 
				  uint32_t buffer_len, 
				  uint32_t chan, 
			      uint64_t period, 
				  uint64_t threshold){
		this->role = mode;
		this->cache_set = chan;
		this->sync_period = period;
		this->threshold = threshold;
		setChannel(buffer_len);
	}

	bool setChannel(uint32_t buffer_len){
		if(buffer_len != CACHE_SIZE)
			std::cout << "Warning: malloc'd buffers len differs from L1 cache size" << std::endl;

		int attempts = 1;
		do{
			this->buffer = (char*) malloc(buffer_len*attempts);
			
			int i = (this->role == MASTER)?0:1;
			for(i; i < CACHE_SIZE; i+=2){
				ADDR_PTR addr = (ADDR_PTR) (buffer+i);
				if (cache_set_index(addr) == this->cache_set){
					this->channels.push_back(addr); 
				}
			}

			if(this->channels.empty()){
				if(attempts == MALLOC_GIVE_UP){
					std::cerr << "Could not get channels, giving up" << std::endl;
					return false;
				}
				else{
					std::cerr << "Could not get channels, trying again" << std::endl;
					free(this->buffer);
					attempts++;
				}
			}
		} while(this->channels.empty());

		std::cout << "Got " << this->channels.size() << " cache lines" << std::endl;
		return true;
	}

	bool synchronize(){
		// The goal will be to maximize miss rate on both sides
		// This is a lazy approach that lets the slave sync to the masters byte "epoch" 
		// Confimation that both sides are synced through backwards handshaking 
		uint32_t itter = 0;
		uint8_t arr[] = {0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00}; // square wave
		if(this->role == MASTER){
			uint8_t recv_byte = 0x00;
			while(!this->rx_ack()){ // rx syn
				this->tx_buffer((unsigned char*)arr, 8);
				if(itter >= SYNC_GIVE_UP){
					std::cout << "Master giving up on sync" << std::endl;
					return false;
				}
				itter++;
			}
			this->tx_ack(); // tx ack
			if(this->rx_synack()) // rx synack
				return true;
			return false;
		}

		if(this->role == SLAVE){
			uint32_t sync_word = 0xAAAAAAAA;

			// If you are lost:
			// The last byte in the sync string (arr above) is 0x00
			// When the last byte is tx'd, the master will read a byte
			// This rx will look like a second 0x00 to me
			// These two 0x00 in a row are the lower 8 bits of my sync word
			// This pattern only happens once per sync_buffer
			while(sync_word != 0x00ff0000){
				if(itter >= SYNC_GIVE_UP){
					std::cout << "Slave giving up on sync" << std::endl;
					return false;
				}

				if(itter%18 == 0){
					if(SYNC_DEBUG)
						std::cout << "bitsliding agian" << std::endl;
					this->rx_bit_slide(0xff);
				}

				sync_word = (sync_word<<8) | this->rx_byte();
				itter++;
				if(SYNC_DEBUG)
					printf("loop: %x, %08x)\n", itter, sync_word);
			}

			// I believe I am sync'd, let master dump another buffer on me
			for(int i = 0; i < 8; i++){
				uint8_t b = this->rx_byte();
				if(arr[i] != b){
					if(SYNC_DEBUG){
						std::cout << "Thought I was synced -- wrongggg" << std::endl;
						printf("%x: %x != %x\n", i, arr[i], b);
					}
					return false;
				}
			}

			this->tx_ack(); //tx syn
			if(!this->rx_ack()){ // rx ack
				if(SYNC_DEBUG)
					std::cout << "No ack recieved" << std::endl;
				return false;
			}

			tx_synack(); //tx syn ack
			// Blindly accept that master got the synack
			return true;
		}
		return false;
	}

	char rx_bit_slide(uint8_t target){
		
		uint8_t arrow = ~target;
		uint32_t i = 0;
		do{
			i++;
			arrow = (arrow<<1) | this->rx_bit();
			if(i%256== 0){
				clock_t start = std::clock();
				while((std::clock() - start) < 25){}
			}
		} while(arrow != target || i < 8);
		if(SYNC_DEBUG)
			std::cout << "rx_bit_slide success" << std::endl;
		return arrow;
	}

	int rx_buffer_crc(unsigned char* buf, int buf_len){
		int rx_len = rx_buffer(buf, buf_len);
		if(rx_len <= 2)
			return -1;
		uint16_t crc = this->crc16(buf, rx_len-2);
		uint16_t recv_crc = buf[rx_len-2] << 8 | buf[rx_len-1];
		if(crc == recv_crc){
			while(!this->synchronize()) {}
			this->tx_success();
			return rx_len-2;
		}
		if(SYNC_DEBUG){
			std::cerr << "CRC check failed" << std::endl;
			std::cerr << "crc: " << crc << " != recvd crc " << recv_crc << std::endl;
		}
		while(!this->synchronize()) {}
		this->tx_nack();
		return -1;
	}

	int rx_buffer(unsigned char* buf, int buf_len){
		while(!this->synchronize()) {}
		for(int j = 0; j < buf_len; j++){
			uint8_t rcv = this->rx_byte();
			buf[j] = rcv;
			if(rcv == 0x00)
				return j;
		}
		return -1;
	}

	bool rx_ack(){
		return (ACK == this->rx_byte());
	}

	bool rx_synack(){
		return (SYNACK == this->rx_byte());
	}

	bool rx_success(){
		return (SUCCESS == this->rx_byte());
	}

	uint8_t rx_byte(){
		uint8_t item = 0;
		for(int j = 0; j < 8; j++){
			item = (this->rx_bit() << 7) | (item>>1);
		}
		return item;
	}

	uint8_t rx_bit(){ // assume good syncronization
		int access= 0;
		int hit = 0;
		double hit_rate = -1; 

		clock_t start = std::clock();
		
		while((std::clock() - start) < this->sync_period){

			for(auto addr : this->channels){
				CYCLES x = measure_one_block_access_time(addr);
				
				access++; 
				if(x <= this->threshold)
					hit++;
			}
		}

		hit_rate = (double) hit / (double) access; 
		if(hit_rate < 0.8)
			return 0x01;
		else
			return 0x00; 
	}

	bool tx_buffer_crc(unsigned char* buf, uint32_t buf_len){
		uint16_t crc = this->crc16(buf, buf_len);
		uint8_t first_word = (crc&0xff00) >> 8;
		uint8_t second_word = (crc&0x00ff);
		while(!this->synchronize()) {}
		tx_buffer(buf, buf_len);
		tx_byte(first_word);
		tx_byte(second_word);
		while(!this->synchronize()) {}
		return (this->rx_success());
	}

	void tx_buffer(unsigned char* buf, uint32_t buf_len){
		for(int i = 0; i < buf_len; i++){
			tx_byte(buf[i]);
		}
	}

	void tx_byte(char item){
		for(int j = 0; j < 8; j++){
			this->tx_bit(item&0x1);
			item >>= 1;
		}
	}
	
	void tx_nack(){
		tx_byte(NACK);
	}

	void tx_ack(){
		tx_byte(ACK);
	}

	void tx_synack(){
		tx_byte(SYNACK);
	}

	void tx_success(){
		tx_byte(SUCCESS);
	}

	void tx_bit(bool bit){
		uint64_t start = std::clock();

		if(bit == 1){
			while(std::clock() - start < this->sync_period){
				for(auto it : channels){
					ADDR_PTR address = (ADDR_PTR) it; 
					this->CLFLUSH(address);
				}
			}
		}
		else{
			if(bit!=0)
				std::cerr << "Requested non binary transmit, sending zero in place" << std::endl;
			
			while(std::clock() - start < this->sync_period){}
		}
	}

	private:
	
	//https://barrgroup.com/blog/crc-series-part-3-crc-implementation-code-cc
	uint16_t crc16(unsigned char* buf, uint32_t buf_len){
		uint16_t crc = 0;
		for(int i = 0; i < buf_len; i++){
			crc ^= (buf[i] << 8);
			for(uint8_t j = 8; j>0; j--){
				if(crc &0x8000){
					crc = (crc<<1)^0x9b;
				}
				else{
					crc = (crc<<1);
				}
			}
		}
		return crc;
	}

	// Source is from original uiuc utils
	void CLFLUSH(ADDR_PTR addr){
		asm volatile ("clflush (%0)"::"r"(addr));
	}

	uint64_t cache_set_index(ADDR_PTR addr){
		uint64_t mask = ((uint64_t) 1 << 16) - 1;
		return (addr & mask) >> 6;
	}
	
	uint64_t cache_line_item_index(ADDR_PTR addr){
		uint64_t mask = ((uint64_t) 1 << 6) - 1;
		return (addr & mask);
	}
};
#endif
