
// https://wiki.osdev.org/FAT

#include "tools/fat32.h"

#define FAT32_PTBL_OFF 446
#define FAT32_START_LBA 8
#define FAT32_LEN_LBA 12

static char fat32_buf[512];
char *fat32_error;

void fat32_set16(char *blk, int byoff, int v)
{
	blk[byoff] = v & 0xFF;
	blk[byoff + 1] = (v >> 8) & 0xFF;
}

void fat32_set32(char *blk, int byoff, int v)
{
	fat32_set16(blk, byoff, v & 0xFFFFF);	
	fat32_set16(blk, byoff + 2, (v >> 16) & 0xFFFFF);	
}

int fat32_get16(char *blk, int byoff)
{
	int v;
	v = blk[byoff];
	v += blk[byoff] << 8;
	return v;
}

int fat32_get32(char *blk, int byoff)
{
	int v;
	v = blk[byoff];
	v += blk[byoff + 1] << 8;
	v += blk[byoff + 2] << 16;
	v += blk[byoff + 3] << 24;
	return v;
}

int fat32_read_block(char *file, char *blk, int start)
{
	FILE *fd;
        fd = fopen(file, "r+b");
	if (!fd) exit(-1);
        fseek(fd, start * 512, SEEK_SET);
        fread(blk, 512, 1, fd);
        return fclose(fd);
}

int fat32_write_block(char *file, char *blk, int start)
{
	FILE *fd;
        fd = fopen(file, "r+b");
	if (!fd) exit(-1);
        fseek(fd, start * 512, SEEK_SET);
        fwrite(blk, 512, 1, fd);
        return fclose(fd);
}


int fat32_set_mbr(char *mbr, int start, int length) {
	char *p;
	int i;
	p = mbr + FAT32_PTBL_OFF; // partition table first volume 
	*p = 0x80; // status ACTIVE
	p++; 
	for (i = 0; i < 24; i += 8) {
		*p = 0;// start chs
		p++;
	}
	*p = 0x0C; // type FAT32 with LBA
	p++; 
	for (i = 0; i < 24; i += 8) {
		*p = 0;// end chs
		p++;
	}
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (start >> i);//  start lba
	 	p++;
	}
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (length >> i);//  length lba
	 	p++;
	}
	return 0;
}

int fat32_set_vbr_sect(char *blk, int start, int length) {
	char *p;
	int i;
	int n;
	p = blk + 0x1C;
	n = start;	// number of hidden sector
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (n >> i);
	 	p++;
	}
	
	p = blk + 0x13;
	n = length;	// sector count
	if (n > 0xFFFF) {
		n = 0;
	} else {
		length = 0;
	}
	for (i = 0; i < 16; i += 8) {
		*p = 0xFF & (n >> i);
	 	p++;
	}
	p = blk + 0x20;
	n = length;	// large sector count
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (n >> i);
	 	p++;
	}
	return 0;
}

int fat32_set_fsinfo(char *blk) {
	char *p;
	int i;
	int v;
	p = blk;
	v = 0x41615252; // lead signature 
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (v >> i);
	 	p++;
	}
	p = blk + 0x1E4;
	v = 0x61417272; // signature
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (v >> i);
	 	p++;
	}
	v = 0xFFFFFFFF; // free clusters 
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (v >> i);
	 	p++;
	}
	v = 0xFFFFFFFF; // start available cluster 
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (v >> i);
	 	p++;
	}

	p = blk + 0x1FC;
	v = 0xAA550000;  // trail signature
	for (i = 0; i < 32; i += 8) {
		*p = 0xFF & (v >> i);
	 	p++;
	}
	return 0;
}

int fat32_start_lba(char *blk, int partid)
{
	return fat32_get32(blk, FAT32_PTBL_OFF + FAT32_START_LBA + (16 * partid));
}

int fat32_len_lba(char *blk, int partid)
{
	return fat32_get32(blk, FAT32_PTBL_OFF + FAT32_LEN_LBA + (16 * partid));
}

int fat32_format(char *file, int partid, char *name)
{
	char *blk;
	int s;
	int l;
	int o;
	blk = fat32_buf;
	
	fat32_read_block(file, blk, 0);
	s = fat32_start_lba(blk, partid);
	l = fat32_len_lba(blk, partid);

	printf("partition start: %d length: %d \n", s, l);
	
	fat32_read_block(file, blk, s);

	return 0;
}

Buf *fat32_list(char *pa) 
{
	int s;
	int l;
	char *file;
	char *name;
	int partid;
	char blk[512];
	Buf *data;
	
	partid = 0;
	fat32_error = "";
	if ((*pa != '/' && *pa != '\\') || strncmp(pa+1, "dev/", 4)) {
		puts(pa);
		fat32_error = "unsupported device path";
		return NULL;
	}
		
	file = pa + 5;
	name = file;
	while (*name && *name != ':') {
		name++;
	}
	if (*name != ':') {
		fat32_error = "missing :";
		return NULL;
	}
	*name = 0;
	data = buf_new(256);
	
	fat32_read_block(file, blk, 0);
	s = fat32_start_lba(blk, partid);
	l = fat32_len_lba(blk, partid);

	buf_addstr(data, "hello\nWorld: ");
	buf_addint(data, s);
	buf_addstr(data, " : ");
	buf_addint(data, l);

	// buf_dispose(data);
	return data;
}
