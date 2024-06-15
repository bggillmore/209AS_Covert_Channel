
#include"util.hpp"

int main(int argc, char **argv)
{
	// Put your covert channel setup code here
	uint64_t sync_period = SYNC; // * CLOCK_TICKS;
	uint64_t thresh = 150 * CLOCK_TICKS;

	CovertChannel chan(SLAVE, CACHE_SIZE, 0, sync_period, thresh);
	
	printf("Please press enter.\n");

	char text_buf[2];
	unsigned char rx_buf[256] = {0};
	int buf_len = 256;
	fgets(text_buf, sizeof(text_buf), stdin);

	printf("Receiver now listening.\n");

	bool listening = true;
	while (listening) {
		int rx_len = chan.rx_buffer_crc(rx_buf, buf_len);
		int fails = 0;
		while(rx_len <= 0){
			rx_len = chan.rx_buffer_crc(rx_buf, buf_len);
		}
		
		printf("%.*s", rx_len, rx_buf);
	}

	printf("Receiver finished.\n");

	return 0;
}
