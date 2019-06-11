#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define BUFF 512
#define PAGE_SZIE 4096 //use $getconf PAGE_SZIE to get the page_size of OS

int main( int argc, char* argv[]){
	char buff[BUFF];
	char file_name[100], io_method[10], ip[20];
	int  device_fd, file_fd;
	size_t file_size;
	struct timeval t_start, t_end;
	double trans_time;

	strcpy(file_name, argv[1]);
	strcpy(io_method, argv[2]);
	strcpy(ip,argv[3]);

	if( (device_fd = open("/dev/slave_device", O_RDWR ) ) < 0 ){ //open device
		fprintf(stderr, "slave_device failed to open\n");
	}

	gettimeofday(&t_start, NULL); //get start time

	if( (file_fd = open(file_name, O_RDWR | O_CREAT | O_TRUNC ) ) < 0 ){ //open file 
			fprintf(stderr, "file failed to open\n");
	}

	if( ioctl(device_fd, 0x12345677, ip) == -1 ){ // connect to master
		fprintf(stderr, "connection failed\n" ); // 0x12345677 is definrf in ioctl-number.txt
	}

	if( strcmp(io_method,"fcntl") == 0 ){
		size_t ret;
		do{
			ret = read(device_fd, buff, sizeof(buff));
			write(file_fd, buff, ret); //write to file
			file_size += ret;
		}while( ret > 0 );
	}
	else if( strcmp(io_method,"mmap") == 0 ){
		size_t ret, offset = 0;
		char *file_address, *device_address;
		do{
			ret = ioctl(device_fd, 0x12345678);
			if( ret == 0 ){ // transmit complete
				file_size = offset;
				break;
			}
			posix_fallocate(file_fd, offset, ret); // locate space for device
			device_address = mmap(NULL, ret, PROT_WRITE, MAP_SHARED, file_fd, offset);//set memory map address
			file_address = mmap(NULL, ret, PROT_READ, MAP_SHARED, device_fd, offset);
			memcpy(file_address, device_address, ret); //copy data from device_address to file_address
			offset += ret;
		}while( ret > 0 );
	}

	ioctl(device_fd, 7122);

	if( ioctl(device_fd, 0x123456789 ) == -1 ){ // close connection
		fprintf(stderr, "close connection failed\n" );
	}
	gettimeofday( &t_end, NULL);
		trans_time = ( t_end.tv_sec - t_start.tv_sec ) * 1000 + ( t_end.tv_usec - t_start.tv_usec ) * 0.0001;
	//calculate transmit time
	printf("Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size / 8 );

	close(device_fd);
	close(file_fd);
	return 0;
}
