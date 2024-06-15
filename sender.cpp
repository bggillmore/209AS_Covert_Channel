
#include"util.hpp"

//https://stackoverflow.com/questions/3501338/c-read-file-line-by-line
void deaddrop_manifesto(CovertChannel chan){
	char * line = NULL;
    size_t len = 0;
    ssize_t llen;
	FILE* fp = fopen("./2010-Dead-Drops.txt", "r");
	if(fp == NULL)
		std::cerr << "Cant find dead drops manifesto" << std::endl;
	
	while ((llen = getline(&line, &len, fp)) != -1) {
        printf("%s", line);
		while(false == chan.tx_buffer_crc((unsigned char*)line, llen)){}
    }

    fclose(fp);

	return;
}

int main(int argc, char **argv)
{
	// Put your covert channel setup code here
	uint64_t thresh = 150;
	uint64_t sync_period = SYNC;
	CovertChannel chan(MASTER, CACHE_SIZE, 0, sync_period, thresh);


	bool sending = true;
	while (sending) {
		char text_buf[128];
		printf("Please type a message. (Enter \"DEADDROP\" ;p) (EXIT to exit)\"\n");
		fgets(text_buf, sizeof(text_buf), stdin);
		int buf_len = strlen(text_buf);
		// Put your covert channel code here
		if(strcmp(text_buf, "EXIT\n") == 0)
			return 0;
		
		if(strcmp(text_buf, "DEADDROP\n") == 0){
			deaddrop_manifesto(chan);
			continue;
		}
		
		printf("sending %x bytes: %s\n", buf_len, text_buf);
		int fails = 0;
		while(!chan.tx_buffer_crc((unsigned char*)text_buf, buf_len)){}
		printf("Send success.\n");
	}

	printf("Sender finished.\n");

	return 0;
}
