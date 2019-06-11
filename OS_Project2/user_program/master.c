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
	char file_name[100], io_method[10];
	int  device_fd, file_fd;
	size_t file_size;
	struct timeval t_start, t_end;
	double trans_time;
	
	strcpy(file_name, argv[1]);
	strcpy(io_method, argv[2]);

	if( (device_fd = open("/dev/master_device", O_RDWR ) ) < 0 ){ //open device
		fprintf(stderr, "master_device failed to open\n");
	}

	gettimeofday(&t_start, NULL); //get start time

	if( (file_fd = open(file_name, O_RDWR) ) < 0 ){ //open file 
		fprintf(stderr, "file failed to open\n");
	}

	struct stat sta;
	if( stat(file_name, &sta) < 0 ){ //use stat structure to get file info
		fprintf(stderr, "get stat of file failed\n" );
	}
	file_size = sta.st_size;

	if( ioctl(device_fd, 0x12345677) == -1 ){ //creat socket
		fprintf(stderr, "failed to create socket\n");
	}

	if( strcmp(io_method,"fcntl") == 0 ){
		size_t ret;
		do{
			ret = read(file_fd, buff, sizeof(buff)); //read from file
			write(device_fd, buff, ret); //write to ddevice
		}while( ret > 0 );
	}
	else if( strcmp(io_method,"mmap") == 0 ){
		size_t offset = 0;
		size_t map_size = PAGE_SZIE * 128;
		while( offset < file_size ){ //in the last mmap, the size may be less than map_size
			size_t map_length = map_size;
			if( ( file_size - offset ) < map_length ){
				map_length = file_size - offset;
			}
			char *file_address = mmap(NULL, map_length, PROT_READ, MAP_SHARED, file_fd, offset);//set memory map address of file
			char *device_address = mmap(NULL, map_length, PROT_WRITE, MAP_SHARED, device_fd, offset); //set memory map address of device
			memcpy(device_address,file_address,map_length); //copy data from file to device
			offset += map_length;
			ioctl(device_fd,0x12345678, map_length);
		}
	}

	ioctl(device_fd, 7122);

	if( ioctl(device_fd, 0x123456789) == -1 ){ //close the connection
		fprintf(stderr, "disconnect error\n" );
	}

	gettimeofday(&t_end, NULL);//get the end time

	trans_time = ( t_end.tv_sec - t_start.tv_sec ) * 1000 + ( t_end.tv_usec - t_start.tv_usec ) * 0.0001;
	//calculate transmit time
	printf("Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size / 8 );

	close(device_fd);
	close(file_fd);
	//close file and device

	return 0;
}
size_t get_filesize(const char* filename)
{
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}