/*  PSXjin - Pc Psx Emulator
 *  Copyright (C) 1999-2003  PSXjin Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(__DREAMCAST__)
#define st_size size
//struct stat {
//	uint32 st_size;
//};
#else
#include <sys/stat.h>
#endif

#include "PsxCommon.h"
#include "padwin.h"

#ifdef _MSC_VER_
#pragma warning(disable:4244)
#endif

// *** FOR WORKS ON PADS AND MEMORY CARDS *****

static unsigned char buf[256];
unsigned char cardh[4] = { 0x00, 0x00, 0x5a, 0x5d };

//static unsigned short StatReg = 0x002b;
// Transfer Ready and the Buffer is Empty
unsigned short StatReg = TX_RDY | TX_EMPTY;
unsigned short ModeReg;
unsigned short CtrlReg;
unsigned short BaudReg;

static unsigned long bufcount;
static unsigned long parp;
static unsigned long mcdst,rdwr;
static unsigned char adrH,adrL;
static unsigned long padst;

PadDataS pad;

char Mcd1Data[MCD_SIZE], Mcd2Data[MCD_SIZE];

// clk cycle byte
// 4us * 8bits = ((PSXCLK / 1000000) * 32) / BIAS; (linuzappz)
#define SIO_INT() { \
	if (!Config.Sio) { \
		psxRegs.interrupt|= 0x80; \
		psxRegs.intCycle[7+1] = 200; /*270;*/ \
		psxRegs.intCycle[7] = psxRegs.cycle; \
	} \
}

unsigned char sioRead8() {
	unsigned char ret = 0;

	if ((StatReg & RX_RDY)/* && (CtrlReg & RX_PERM)*/) {
//		StatReg &= ~RX_OVERRUN;
		ret = buf[parp];
		if (parp == bufcount) {
			StatReg &= ~RX_RDY;		// Receive is not Ready now
			if (mcdst == 5) {
				mcdst = 0;
				if (rdwr == 2) {
					switch (CtrlReg&0x2002) {
						case 0x0002:
							memcpy(Mcd1Data + (adrL | (adrH << 8)) * 128, &buf[1], 128);
							SaveMcd(Config.Mcd1, Mcd1Data, (adrL | (adrH << 8)) * 128, 128);
							break;
						case 0x2002:
							memcpy(Mcd2Data + (adrL | (adrH << 8)) * 128, &buf[1], 128);
							SaveMcd(Config.Mcd2, Mcd2Data, (adrL | (adrH << 8)) * 128, 128);
							break;
					}
				}
			}
			if (padst == 2) padst = 0;
			if (mcdst == 1) {
				mcdst = 2;
				StatReg|= RX_RDY;
			}
		}
	}

#ifdef PAD_LOG
	PAD_LOG("sio read8 ;ret = %x\n", ret);
#endif
	return ret;
}

void netError() {
	ClosePlugins();
	SysMessage(_("Connection closed\n"));
	SysRunGui();
}

void sioWrite8(unsigned char value) {
#ifdef PAD_LOG
	PAD_LOG("sio write8 %x\n", value);
#endif
	switch (padst) {
		case 1: SIO_INT();
			if ((value&0x40) == 0x40) {
				padst = 2; parp = 1;
				
					switch (CtrlReg&0x2002) {
						case 0x0002:
							buf[parp] = PAD1_poll(value);
							break;
						case 0x2002:
							buf[parp] = PAD2_poll(value);
							break;
					}
				

				if (!(buf[parp] & 0x0f)) {
					bufcount = 2 + 32;
				} else {
					bufcount = 2 + (buf[parp] & 0x0f) * 2;
				}
				if (buf[parp] == 0x41) {
					switch (value) {
						case 0x43:
							buf[1] = 0x43;
							break;
						case 0x45:
							buf[1] = 0xf3;
							break;
					}
				}
			}
			else padst = 0;
			return;
		case 2:
			parp++;
/*			if (buf[1] == 0x45) {
				buf[parp] = 0;
				SIO_INT();
				return;
			}*/
			
				switch (CtrlReg&0x2002) {
					case 0x0002: buf[parp] = PAD1_poll(value); break;
					case 0x2002: buf[parp] = PAD2_poll(value); break;
				}		

			if (parp == bufcount) { padst = 0; return; }
			SIO_INT();
			return;
	}

	switch (mcdst) {
		case 1:
			SIO_INT();
			if (rdwr) { parp++; return; }
			parp = 1;
			switch (value) {
				case 0x52: rdwr = 1; break;
				case 0x57: rdwr = 2; break;
				default: mcdst = 0;
			}
			return;
		case 2: // address H
			SIO_INT();
			adrH = value;
			*buf = 0;
			parp = 0;
			bufcount = 1;
			mcdst = 3;
			return;
		case 3: // address L
			SIO_INT();
			adrL = value;
			*buf = adrH;
			parp = 0;
			bufcount = 1;
			mcdst = 4;
			return;
		case 4:
			SIO_INT();
			parp = 0;
			switch (rdwr) {
				case 1: // read
					buf[0] = 0x5c;
					buf[1] = 0x5d;
					buf[2] = adrH;
					buf[3] = adrL;
					switch (CtrlReg&0x2002) {
						case 0x0002:
							memcpy(&buf[4], Mcd1Data + (adrL | (adrH << 8)) * 128, 128);
							break;
						case 0x2002:
							memcpy(&buf[4], Mcd2Data + (adrL | (adrH << 8)) * 128, 128);
							break;
					}
					{
					char xor = 0;
					int i;
					for (i=2;i<128+4;i++)
						xor^=buf[i];
					buf[132] = xor;
					}
					buf[133] = 0x47;
					bufcount = 133;
					break;
				case 2: // write
					buf[0] = adrL;
					buf[1] = value;
					buf[129] = 0x5c;
					buf[130] = 0x5d;
					buf[131] = 0x47;
					bufcount = 131;
					break;
			}
			mcdst = 5;
			return;
		case 5:	
			parp++;
			if (rdwr == 2) {
				if (parp < 128) buf[parp+1] = value;
			}
			SIO_INT();
			return;
	}

	switch (value) {
		case 0x01: // start pad
			StatReg |= RX_RDY;		// Transfer is Ready
			
				switch (CtrlReg&0x2002) {
					case 0x0002: buf[0] = PAD1_startPoll(1); break;
					case 0x2002: buf[0] = PAD2_startPoll(2); break;
				}			
			

			bufcount = 2;
			parp = 0;
			padst = 1;
			SIO_INT();
			return;
		case 0x81: // start memcard
			StatReg |= RX_RDY;
			memcpy(buf, cardh, 4);
			parp = 0;
			bufcount = 3;
			mcdst = 1;
			rdwr = 0;
			SIO_INT();
			return;
	}
}

void sioWriteCtrl16(unsigned short value) {
	CtrlReg = value & ~RESET_ERR;
	if (value & RESET_ERR) StatReg &= ~IRQ;
	if ((CtrlReg & SIO_RESET) || (!CtrlReg)) {
		padst = 0; mcdst = 0; parp = 0;
		StatReg = TX_RDY | TX_EMPTY;
		psxRegs.interrupt&=~0x80;
	}
}

void sioInterrupt() {
#ifdef PAD_LOG
	PAD_LOG("Sio Interrupt (CP0.Status = %x)\n", psxRegs.CP0.n.Status);
#endif
//	SysPrintf("Sio Interrupt\n");
	StatReg|= IRQ;
	psxHu32ref(0x1070)|= SWAPu32(0x80);
}
char str[256];
char* MakeMemCardPath(const char* filename)
{
	strcpy(str,Config.MemCardsDir);
	strcat(str, filename);
	return str;
}


void LoadMcd(int mcd, char *str) {
	FILE *f;
	char *data = NULL;

	if (mcd == 1) data = Mcd1Data;
	if (mcd == 2) data = Mcd2Data;
	
	char z[32] = "";
	sprintf(z, "Mcd00%d.mcr", mcd);	
	if (*str == 0) str = MakeMemCardPath(z);

	f = fopen(str, "rb");
	if (f == NULL) {
		CreateMcd(str);
		f = fopen(str, "rb");
		if (f != NULL) {
			struct stat buf;

			if (stat(str, &buf) != -1) {
				if (buf.st_size == MCD_SIZE + 64) 
					fseek(f, 64, SEEK_SET);
				else if(buf.st_size == MCD_SIZE + 3904)
					fseek(f, 3904, SEEK_SET);
			}			
			fread(data, 1, MCD_SIZE, f);
			fclose(f);
		}
		//else SysMessage(_("Failed loading MemCard %s\n"), str);
	}
	else {
		struct stat buf;

		if (stat(str, &buf) != -1) {
			if (buf.st_size == MCD_SIZE + 64) 
				fseek(f, 64, SEEK_SET);
			else if(buf.st_size == MCD_SIZE + 3904)
				fseek(f, 3904, SEEK_SET);
		}
		fread(data, 1, MCD_SIZE, f);
		fclose(f);
	}
}

void LoadMcds(char *mcd1, char *mcd2) {
	LoadMcd(1, mcd1);
	LoadMcd(2, mcd2);
}

void SaveMcd(char *mcd, char *data, unsigned long adr, int size) {
	FILE *f;
	
	f = fopen(mcd, "r+b");
	if (f != NULL) {
		struct stat buf;

		if (stat(mcd, &buf) != -1) {
			if (buf.st_size == MCD_SIZE + 64)
				fseek(f, adr + 64, SEEK_SET);
			else if (buf.st_size == MCD_SIZE + 3904)
				fseek(f, adr + 3904, SEEK_SET);
			else
				fseek(f, adr, SEEK_SET);
		} else 	fseek(f, adr, SEEK_SET);

		fwrite(data + adr, 1, size, f);
		fclose(f);
		return;
	}

	// try to create it again if we can't open it
	/*f = fopen(mcd, "wb");
	if (f != NULL) {
		fwrite(data, 1, MCD_SIZE, f);
		fclose(f);
	}*/
	ConvertMcd(mcd, data);
}

void CreateMcd(char *mcd) {
	FILE *f;	
	struct stat buf;
	int s = MCD_SIZE;
	int i=0, j;

	f = fopen(mcd, "wb");
	if (f == NULL) return;

	if(stat(mcd, &buf)!=-1) {		
		if ((buf.st_size == MCD_SIZE + 3904) || strstr(mcd, ".gme")) {			
			s = s + 3904;
			fputc('1', f); s--;
			fputc('2', f); s--;
			fputc('3', f); s--;
			fputc('-', f); s--;
			fputc('4', f); s--;
			fputc('5', f); s--;
			fputc('6', f); s--;
			fputc('-', f); s--;
			fputc('S', f); s--;
			fputc('T', f); s--;
			fputc('D', f); s--;
			for(i=0;i<7;i++) {
				fputc(0, f); s--;
			}
			fputc(1, f); s--;
			fputc(0, f); s--;
			fputc(1, f); s--;
			fputc('M', f); s--; 
			fputc('Q', f); s--; 
			for(i=0;i<14;i++) {
				fputc(0xa0, f); s--;
			}
			fputc(0, f); s--;
			fputc(0xff, f);
			while (s-- > (MCD_SIZE+1)) fputc(0, f);
		} else if ((buf.st_size == MCD_SIZE + 64) || strstr(mcd, ".mem") || strstr(mcd, ".vgs")) {
			s = s + 64;				
			fputc('V', f); s--;
			fputc('g', f); s--;
			fputc('s', f); s--;
			fputc('M', f); s--;
			for(i=0;i<3;i++) {
				fputc(1, f); s--;
				fputc(0, f); s--;
				fputc(0, f); s--;
				fputc(0, f); s--;
			}
			fputc(0, f); s--;
			fputc(2, f);
			while (s-- > (MCD_SIZE+1)) fputc(0, f);
		}
	}
	fputc('M', f); s--;
	fputc('C', f); s--;
	while (s-- > (MCD_SIZE-127)) fputc(0, f);
	fputc(0xe, f); s--;

	for(i=0;i<15;i++) { // 15 blocks
		fputc(0xa0, f); s--;
		for(j=0;j<126;j++) {
			fputc(0x00, f); s--;
		}
		fputc(0xa0, f); s--;
	}

	while ((s--)>=0) fputc(0, f);		
	fclose(f);
}

void ConvertMcd(char *mcd, char *data) {
	FILE *f;
	int i=0;
	int s = MCD_SIZE;
	
	if (strstr(mcd, ".gme")) {		
		f = fopen(mcd, "wb");
		if (f != NULL) {		
			fwrite(data-3904, 1, MCD_SIZE+3904, f);
			fclose(f);
		}		
		f = fopen(mcd, "r+");		
		s = s + 3904;
		fputc('1', f); s--;
		fputc('2', f); s--;
		fputc('3', f); s--;
		fputc('-', f); s--;
		fputc('4', f); s--;
		fputc('5', f); s--;
		fputc('6', f); s--;
		fputc('-', f); s--;
		fputc('S', f); s--;
		fputc('T', f); s--;
		fputc('D', f); s--;
		for(i=0;i<7;i++) {
			fputc(0, f); s--;
		}		
		fputc(1, f); s--;
		fputc(0, f); s--;
		fputc(1, f); s--;
		fputc('M', f); s--;
		fputc('Q', f); s--;
		for(i=0;i<14;i++) {
			fputc(0xa0, f); s--;
		}
		fputc(0, f); s--;
		fputc(0xff, f);
		while (s-- > (MCD_SIZE+1)) fputc(0, f);
		fclose(f);
	} else if(strstr(mcd, ".mem") || strstr(mcd,".vgs")) {		
		f = fopen(mcd, "wb");
		if (f != NULL) {		
			fwrite(data-64, 1, MCD_SIZE+64, f);
			fclose(f);
		}		
		f = fopen(mcd, "r+");		
		s = s + 64;				
		fputc('V', f); s--;
		fputc('g', f); s--;
		fputc('s', f); s--;
		fputc('M', f); s--;
		for(i=0;i<3;i++) {
			fputc(1, f); s--;
			fputc(0, f); s--;
			fputc(0, f); s--;
			fputc(0, f); s--;
		}
		fputc(0, f); s--;
		fputc(2, f);
		while (s-- > (MCD_SIZE+1)) fputc(0, f);
		fclose(f);
	} else {
		f = fopen(mcd, "wb");
		if (f != NULL) {		
			fwrite(data, 1, MCD_SIZE, f);
			fclose(f);
		}
	}
}

void GetMcdBlockInfo(int mcd, int block, McdBlock *Info) {
	char *data = NULL, *ptr, *str;
	unsigned short clut[16];
	unsigned short c;
	int i, x;

	memset(Info, 0, sizeof(McdBlock));

	str = Info->Title;

	if (mcd == 1)
		data = Mcd1Data;
	if (mcd == 2)
		data = Mcd2Data;

	ptr = data + block * 8192 + 2;

	Info->IconCount = *ptr & 0x3;

	ptr+= 2;

	i=0;
	memcpy(Info->sTitle, ptr, 48*2);

	for (i=0; i < 48; i++) {
		c = *(ptr) << 8;
		c|= *(ptr+1);
		if (!c) break;

		if (c >= 0x8281 && c <= 0x8298)
			c = (c - 0x8281) + 'a';
		else if (c >= 0x824F && c <= 0x827A)
			c = (c - 0x824F) + '0';
		else if (c == 0x8144) c = '.';
		else if (c == 0x8146) c = ':';
		else if (c == 0x8168) c = '"';
		else if (c == 0x8169) c = '(';
		else if (c == 0x816A) c = ')';
		else if (c == 0x816D) c = '[';
		else if (c == 0x816E) c = ']';
		else if (c == 0x817C) c = '-';
		else {
			c = ' ';
		}

		str[i] = c;
		ptr+=2;
	}
	str[i] = 0;

	ptr = data + block * 8192 + 0x60; // icon palete data

	for (i=0; i<16; i++) {
		clut[i] = *((unsigned short*)ptr);
		ptr+=2;
	}

	for (i=0; i<Info->IconCount; i++) {
		short *icon = &Info->Icon[i*16*16];

		ptr = data + block * 8192 + 128 + 128 * i; // icon data

		for (x=0; x<16*16; x++) {
			icon[x++] = clut[*ptr & 0xf];
			icon[x]   = clut[*ptr >> 4];
			ptr++;
		}
	}

	ptr = data + block * 128;

	Info->Flags = *ptr;

	ptr+= 0xa;
	strncpy(Info->ID, ptr, 12);
	Info->ID[12] = 0;
	ptr+= 12;
	strncpy(Info->Name, ptr, 16);
}

int sioFreeze(EMUFILE *f, int Mode) {

	gzfreezelarr(buf);
	gzfreezel(&StatReg);
	gzfreezel(&ModeReg);
	gzfreezel(&CtrlReg);
	gzfreezel(&BaudReg);
	gzfreezel(&bufcount);
	gzfreezel(&parp);
	gzfreezel(&mcdst);
	gzfreezel(&rdwr);
	gzfreezel(&adrH);
	gzfreezel(&adrL);
	gzfreezel(&padst);

	return 0;
}

static void SetTempMemoryCard(char slot) {
	if (slot == 1) {
		strncpy(Config.OldMcd1, Config.Mcd1, 256);
		strncpy(Config.Mcd1, MakeMemCardPath("movie001.tmp"), 256);
	}
	else {
		strncpy(Config.OldMcd2, Config.Mcd2, 256);
		strncpy(Config.Mcd2, MakeMemCardPath("movie002.tmp"), 256);
	}
}

void SIO_UnsetTempMemoryCards() {
	strncpy(Config.Mcd1, Config.OldMcd1, 256);
	strncpy(Config.Mcd2, Config.OldMcd2, 256);
	LoadMcds(Config.Mcd1, Config.Mcd2);
	remove("memcards\\movie001.tmp");
	remove("memcards\\movie002.tmp");
}

static unsigned long SaveMemoryCardEmbed(char *file,char *newfile,char *moviefile) {
	FILE *infile;
	gzFile f;
	char *buffer;
	unsigned long numbytes;

	//read old mcd file
	infile = fopen(file, "rb");
	if(infile == NULL)
		return 0;
	fseek(infile, 0L, SEEK_END);
	numbytes = ftell(infile);
	fseek(infile, 0L, SEEK_SET);
	buffer = (char*)calloc(numbytes, sizeof(char));
	if(buffer == NULL)
		return 0;
	fread(buffer, sizeof(char), numbytes, infile);
	fclose(infile);

	//write new mcd temp file
	infile = fopen(newfile, "wb");
	fwrite(buffer,sizeof(char),numbytes,infile);
	fclose(infile);

	//write uncompressed mcd size to movie file
	infile = fopen(moviefile, "ab");
	fwrite(&numbytes, 1, 4, infile);
	fclose(infile);

	//write compressed embed mcd to movie file
	f = gzopen(moviefile, "ab");
	if (f == NULL)
		return 0;
	gzwrite(f, (void*)buffer, numbytes);
	gzclose(f);

	free(buffer);
	return 1;
}

void SIO_SaveMemoryCardsEmbed(char *file,char slot) {
	if (slot == 1) {
		SaveMemoryCardEmbed(Config.Mcd1,MakeMemCardPath("movie001.tmp"),file);
		SetTempMemoryCard(1);
	}
	else {
		SaveMemoryCardEmbed(Config.Mcd2,MakeMemCardPath("movie001.tmp"),file);
		SetTempMemoryCard(2);
	}
}

static int LoadMemoryCardEmbed(char *moviefile,char *newmcdfile,
                               unsigned long fileOffsetBegin,unsigned long fileOffsetEnd) {
	unsigned long embMcdSize;
	FILE* fp;
	FILE* fp2;
	gzFile fs;
	uint8 * data;
	uint8 * embMcdTmp;
	size_t blockSize = fileOffsetEnd-fileOffsetBegin;

	embMcdTmp = (uint8*)malloc(blockSize);

	//read embedded mcd size and full compressed mcd file
	fp = fopen(moviefile,"rb");
	fseek(fp, fileOffsetBegin, SEEK_SET);
	fread(&embMcdSize, 1, 4, fp);
	fread(embMcdTmp, 1, blockSize-4, fp);
	fclose(fp);

	//write compressed mcd file to temp destination
	fp2 = fopen("movie_mcd.tmp","wb");
	fwrite(embMcdTmp, 1, blockSize-4, fp2);
	fclose(fp2);
	free(embMcdTmp);

	//open temp compressed mcd file and uncompress it
	fs = gzopen("movie_mcd.tmp", "rb");
	if (!fs)
		return 1;

	data=(uint8 *)malloc(embMcdSize);
	gzread(fs, (void *) data, embMcdSize);
	gzclose(fs);
	remove("movie_mcd.tmp");

	//write uncompressed mcd to new movie temp destination
	fp2 = fopen(newmcdfile,"wb");
	fwrite(data, 1, embMcdSize, fp2);
	fclose(fp2);
	free(data);

	return 0;
}

void SIO_LoadMemoryCardsEmbed(char *file) {
	LoadMemoryCardEmbed(file,MakeMemCardPath("movie001.tmp"),Movie.memoryCard1Offset,Movie.memoryCard2Offset);
	LoadMemoryCardEmbed(file,MakeMemCardPath("movie002.tmp"),Movie.memoryCard2Offset,Movie.cheatListOffset);
	SetTempMemoryCard(1);
	SetTempMemoryCard(2);
	LoadMcds(Config.Mcd1, Config.Mcd2);
}

void SIO_ClearMemoryCardsEmbed() {
	SetTempMemoryCard(1);
	SetTempMemoryCard(2);
	LoadMcds(Config.Mcd1, Config.Mcd2);
	remove(MakeMemCardPath("movie001.tmp"));
	remove(MakeMemCardPath("movie001.tmp"));
}
