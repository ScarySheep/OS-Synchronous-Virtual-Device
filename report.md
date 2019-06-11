# [OS] Synchronous Virtual Device
## Design
### Device Drivers

#### Master Device
master_device.c 是Master side 的Kernel program負責接收master傳來的資料以及與ksocket建立溝通管道。
```cpp=43
extern ksocket_t ksocket(int domain, int type, int protocol);
extern int kconnect(ksocket_t socket, struct sockaddr *address, int address_len);
extern ssize_t krecv(ksocket_t socket, void *buffer, size_t length, int flags);
extern int kclose(ksocket_t socket);
extern unsigned int inet_addr(char* ip);
extern char *inet_ntoa(struct in_addr *in);
```
首先先定義一些Ksocket相關函式。因為Ksocket的函式會被exported所以我們這邊使用extern。
```cpp=59
static ksocket_t sockfd_cli;
static struct sockaddr_in addr_srv;
```
以上兩行，第一行表示連結master的socket，第二行則是master的address。
```cpp=85
static const struct vm_operations_struct my_vm_ops = {
	.open = mmap_open,
	.close = mmap_close,
	.fault = mmap_fault
};
```
提供VM的相關操作。
緊接定義一些file operations的函式，紀錄Device information還有完成master端的open/close函式與mmap各式函式還有錯誤處理。

master_ioctl 函式接受三個參數:file,ioctl_num 與ioctl_param。
根據不同層級的page目錄定義:
```cpp=208
pgd_t *pgd;
pud_t *pud;
pmd_t *pmd;
pte_t *ptep, pte;
```
最後根據 ioctl_num做不同的處理。
```cpp=213
case master_IOCTL_CREATESOCK:
			sockfd_cli = kaccept(sockfd_srv, (struct sockaddr *)&addr_cli, &addr_len);
			if (sockfd_cli == NULL)
			{
				printk("accept failed\n");
				return -1;
			}
			else
				printk("aceept sockfd_cli = 0x%p\n", sockfd_cli);

			tmp = inet_ntoa(&addr_cli.sin_addr);
			printk("got connected from : %s %d\n", tmp, ntohs(addr_cli.sin_port));
			kfree(tmp);
			ret = 0;
			break;
```
master_IOCTL_CREATESOCK 會製造一個socket並產生連結。
```cpp=228
case master_IOCTL_MMAP:
    ksend(sockfd_cli, file->private_data, ioctl_param, 0);
	break;
```
```cpp=240

default:
	pgd = pgd_offset(current->mm, ioctl_param);
	pud = pud_offset(pgd, ioctl_param);
	pmd = pmd_offset(pud, ioctl_param);
	ptep = pte_offset_kernel(pmd , ioctl_param);
	pte = *ptep;
	printk("master: %lX\n", pte);
	ret = 0;
	break;
```
以上兩段分別處理memory mapped IO與file IO。
```cpp=232
case master_IOCTL_EXIT:
	if(kclose(sockfd_cli) == -1)
	{
		printk("kclose cli error\n");
		return -1;
	}
	ret = 0;
	break;
```
master_IOCTL_EXIT結束master_ioctl函式。
```cpp=253
static ssize_t send_msg(struct file *file, const char __user *buf, size_t count, loff_t *data)
{
	char msg[BUF_SIZE];
	if(copy_from_user(msg, buf, count))
		return -ENOMEM;
	ksend(sockfd_cli, msg, count, 0);

	return count;

}
```
User 寫東西到這個device都會呼叫這個函式
#### Slave Device
功能基本上就是slave方的 Kernel program，接收來自Ksocket的資料並傳輸到slave手上。
與master端差異比較大的是slave_ioctl與receive_message兩個函式。
slave_ioctl 根據ioctl_num參數有四個不同處理:
```cpp=168
case slave_IOCTL_CREATESOCK:

	if(copy_from_user(ip, (char*)ioctl_param, sizeof(ip)))
		return -ENOMEM;

	sprintf(current->comm, "ksktcli");

	memset(&addr_srv, 0, sizeof(addr_srv));
	addr_srv.sin_family = AF_INET;
	addr_srv.sin_port = htons(2325);
	addr_srv.sin_addr.s_addr = inet_addr(ip);
	addr_len = sizeof(struct sockaddr_in);

	sockfd_cli = ksocket(AF_INET, SOCK_STREAM, 0);
	printk("sockfd_cli = 0x%p  socket is created\n", sockfd_cli);
	if (sockfd_cli == NULL)
	{
		printk("socket failed\n");
		return -1;
	}
	if (kconnect(sockfd_cli, (struct sockaddr*)&addr_srv, addr_len) < 0)
	{
		printk("connect failed\n");
				return -1;
	}
	tmp = inet_ntoa(&addr_srv.sin_addr);
	printk("connected to : %s %d\n", tmp, ntohs(addr_srv.sin_port));
	kfree(tmp);
	ret = 0;
	break;
```
slave_IOCTL_CREATESOCK 會產生與master端的Ksocket連結。
```cpp=198
case slave_IOCTL_MMAP:
	while (1) {
		rec_n = krecv(sockfd_cli, buf, sizeof(buf), 0);接收訊息
		if (rec_n == 0) {
			break;
		}
		memcpy(file->private_data + offset, buf, rec_n);
		offset += rec_n;
	}
	ret = offset;
	break;

```
slave_IOCTL_MMAP 處理slave 端memory map IO 的部分。以一個while迴圈不斷接收訊息直到結束，並以memcpy將訊息寫至file。
```cpp=210
case slave_IOCTL_EXIT:
	if(kclose(sockfd_cli) == -1)
	{
		printk("kclose cli error\n");
		return -1;
	}
	set_fs(old_fs);
	ret = 0;
	break;
```
slave_IOCTL_EXIT 結束slave端ioctl函式
```cpp=219
default:
	pgd = pgd_offset(current->mm, ioctl_param);		pud = pud_offset(pgd, ioctl_param);
	pmd = pmd_offset(pud, ioctl_param);
	ptep = pte_offset_kernel(pmd , ioctl_param);
	pte = *ptep;
	printk("slave: %lX\n", pte);
	ret = 0;
	break;

```
其餘的處理file IO的部分，一些簡單的read write operations。
### Ksocket
ksocket.c 提供master-slave之間兩個device的資料傳輸的各個函式，與sample code一樣，沒有做修改的動作。
### User Programs
從standard input讀取master slave之間資料傳輸的參數。Master 會接受 file location 與IO 的模式。Slave則收到file locatiom, IO模式與master 的ip。
#### Master
```cpp=26
if( (device_fd = open("/dev/master_device", O_RDWR ) ) < 0 )
    {
	fprintf(stderr, "master_device failed to open\n");
    }
gettimeofday(&t_start, NULL);
```
首先程式會嘗試啟動master device，並且得到Start time。緊接著打開檔案並且創造要使用的Socket。
```cpp=46
if( strcmp(io_method,"fcntl") == 0 ){
		size_t ret;
		do{
			ret = read(file_fd, buff, sizeof(buff));
			write(device_fd, buff, ret);to ddevice
		}while( ret > 0 );
	}
else if( strcmp(io_method,"mmap") == 0 ){
		size_t offset = 0;
		size_t map_size = PAGE_SZIE * 128;
		while( offset < file_size ){
			size_t map_length = map_size;
			if( ( file_size - offset ) < map_length ){
				map_length = file_size - offset;
			}
			char *file_address = mmap(NULL, map_length, PROT_READ, MAP_SHARED, file_fd, offset);
			char *device_address = mmap(NULL, map_length, PROT_WRITE, MAP_SHARED, device_fd, offset);
			memcpy(device_address,file_address,map_length);
			offset += map_length;
			ioctl(device_fd,0x12345678, map_length);
		}
	}
```
接下來程式依照所指定的IO模式進行個別處理，file IO 執行fnctl； memory mapped IO 執行 mmap。fnctl做的就是將從file讀的東西寫到target location。mmap則會先設定檔案與device的memory map address，然後用memcpy函式處理資料的傳輸。
```cpp=71
if( ioctl(device_fd, 0x123456789) == -1 ){
		fprintf(stderr, "disconnect error\n" );
	}

gettimeofday(&t_end, NULL);

trans_time = ( t_end.tv_sec - t_start.tv_sec ) * 1000 + ( t_end.tv_usec - t_start.tv_usec ) * 0.0001;
printf("Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size / 8 );

close(device_fd);
close(file_fd);
```
最後程式關閉連結，讀取結束時間並以它與先前的start time計算整體的transmit time還有執行關閉檔案與device的動作。
#### Slave
slave.c與master.c基本上差不多，所有的函式都相當雷同
。
```cpp=37
if( ioctl(device_fd, 0x12345677, ip) == -1 ){
	fprintf(stderr, "connection failed\n" ); 
}
```
slave 多了與master連結的動作。
## 執行範例測資
Master/Slave: mmap/fnctl
![](https://i.imgur.com/HbMczIG.jpg)

Master/Slave:fnctl/fnctl
![](https://i.imgur.com/0wfap53.jpg)

Master/Slave:fnctl/mmap
![](https://i.imgur.com/JDFQbwX.jpg)

Master/Slave:mmap/mmap
![](https://i.imgur.com/g9Lpg6B.jpg)

## Results
![](https://i.imgur.com/kprBhZm.png)

Master端各個IO模式之間的差異並不大，但普遍來說 Master/Slave: functl/functl的傳輸時間都比其他組的長。Mmap/mmap的傳輸時間比其他組短。程式執行需要開兩台終端機同步進行；先開好的是Master所以她必須等Slave成功啟動方能開始傳輸資料，固時間普遍較長。
![](https://i.imgur.com/ZOmGqLc.png)
Slave端的各組間差異較明顯。在檔案大小比較小的兩組中，Transmission Time並沒有明顯的差異，但到後面兩個檔案都可以明顯看出functl/functl的傳輸時間最長，mmap/mmap最短。各個IO模式間的差異也隨著檔案大小增加出現明顯差異。

### 差異原因
當系統遇到 file IO的時候會先驗證 page cache裡面有沒無存取資料，沒有找到將會複製一份buffer。當遇到mmap的時候，則會先造成 page fault，然後系統會將 VMM 對應的 page 存取到記憶體中。
當處理大檔案時，重複傳輸檔案附近的block，隨機存取檔案中的資料，可以有效降低 page fault的發生率，mmap的效率會相對提高。而更因為少了一次複製buffer的動作，又比file IO更加快速。也因此mmap/mmap的傳輸時間比functl/functl在較大的檔案下有效率許多。
在小檔案處理方面，則因為總時間不長，造成各模式間的傳輸時間差異不明顯，較沒有討論控間。


## 組員的貢獻

* 楊晨弘 徐浩翔 - Ksocket
* 李正己 廖晨皓 - Device Drivers: Master/Slave Devices
* 許定為 - User Programs, MakeFile + Debug
* 吳綺緯 - Report + 結果分析
