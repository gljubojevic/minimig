/*
Copyright 2005, 2006, 2007 Dennis van Weeren

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

ADF Routines for minimig

-- Goran Ljubojevic ---
2009-08-19	- First version extracted from main.c
			- Added separate debug mode variable to switch off printf
2009-08-28	- Removed cluster cache to free large amount of memory.
2009-10-15	- Removed extern string buffer defined in main no longer needed
2009-11-14	- HandleFDD moved from main
			- UpdateDriveStatus moved from main 
2009-11-30	- Cleaned up global floppy_drives variable, not needed
2009-12-13	- ReadTrack and WriteTrack modified to properly handle global file handle due to file seek optmisations
 			- floppy_drives commented out in HandleFDD, not used any more
2009-12-20	- WriteTrack fixed to do correct file seek before writing
2009-12-30	(Copy from Yaqube Arm release YQ091230)
			- updated sync word list 
			- adapted gap size
			- fixed sector header generation
			- removed DMA size check from ReadTrack
2010-08-26	- Added firmwareConfiguration.h
			- Removed Flopy insert on OSD, not visible anyway
2010-09-09	- Added definitions for standard floppy size
			- Added defines for MFM format
2010-10-26	- ReadTrack cleaned according to arm source
2010-11-07	- ReadTrack modified more to be like ARM version
			
*/

#include <pic18.h>
#include <stdio.h>
#include <string.h>
#include "firmwareConfiguration.h"
#include "hardware.h"
#include "fat16.h"
#include "osd.h"
#include "config.h"
#include "adf.h"

// Defined in main.c
extern void ErrorMessage(const char* message, unsigned char code);

unsigned char Error;

// Floppy drives
struct adfTYPE *pdfx;						// drive select pointer
struct adfTYPE df[MAX_FLOPPY_DRIVES];	// drives information structure



void HandleFDD(unsigned char c1, unsigned char c2)
{
	unsigned char sel;

	if (c1 & CMD_RDTRK)
	{
		DISKLED_ON;
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		ReadTrack( & df[sel]);
		DISKLED_OFF;
	}
	else if (c1 & CMD_WRTRK)
	{
		DISKLED_ON;
		sel = (c1 >> 6) & 0x03;
		df[sel].track = c2;
		WriteTrack( & df[sel]);
		DISKLED_OFF;
	}
}


void UpdateDriveStatus(void)
{
	unsigned char i;
	unsigned char status;
	
	status = 0;
	for(i=0; i < MAX_FLOPPY_DRIVES; i++)
	{	status |= (df[i].status << i);	}
	
	EnableFpga();
	SPI(CMD_DSKSTATUS);
	SPI(status);
	DisableFpga();
}


/*insert floppy image pointed to to by global <file> into <drive>*/
void InsertFloppy(struct adfTYPE *drive)
{
//	unsigned long tracks;

	// clear OSD and display message
	// REMOVED 2010-08-26 - Not visible anyway
//	OsdClear();
//	OsdWrite(1,"     Inserting",0);
//	OsdWrite(2,"     floppy...",0);

	// copy name
	strncpy(drive->name, file.name, 12);	// Smaller size
	
	// initialize rest of struct
	drive->status = DSK_INSERTED;
	
	//read-only attribute
	if (!(file.attributes & FAT_ATTRIB_READONLY))
	{	drive->status |= DSK_WRITABLE;		}

	drive->firstCluster = file.firstCluster;
	drive->clusteroffset=drive->firstCluster;
	drive->sectoroffset=0;
	drive->track=0;
	drive->trackprev=-1;

//	tracks = file.len / (SECTOR_BYTES * SECTOR_COUNT);
//	if(tracks > MAX_TRACKS)
//	{
//		tracks = MAX_TRACKS;
//		#ifdef DEBUG_ADF
//		printf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\r", tracks);
//		#endif
//	}
//	drive->tracks = tracks;

	#ifdef DEBUG_ADF
	printf("Inserting floppy: \"%s\"",file.name);
	printf(", attributes: %02X\r\n",file.attributes);
	printf("drive status: %02X\r\n",drive->status);
	#endif
}


// Prepares global file handle for reading and writing
void PrepareGlobalFileHandle(struct adfTYPE *drive)
{
	// Setup global file handle for reading
	// Restore previous file params
	file.firstCluster = drive->firstCluster;

	if(0 < drive->trackprev)
	{
		file.cluster = drive->clusteroffset;
		file.sector = (unsigned long)drive->trackprev;
		file.sector *= 11;
		file.sector += drive->sectoroffset;
	}
	else
	{
		file.cluster = file.firstCluster;
		file.sector = 0;
	}
}


//read a track from disk
//track number is updated in drive struct before calling this function
void ReadTrack(struct adfTYPE *drive)
{
	unsigned char sector;
	unsigned char status;
	unsigned char track;
	unsigned short dsksync;
	unsigned short dsklen;
	unsigned long seekSector;

	// display track number: cylinder & head
	#ifdef DEBUG_ADF
//	unsigned short timepassed;
//	timepassed = GetTimer(0);
	printf("\r\n*%0d:", drive->track);
	#endif

//	if (drive->track >= drive->tracks)
//	{  	drive->track = drive->tracks - 1;   }

    // Prepare Global File Handle
	PrepareGlobalFileHandle(drive);

	// track step or track 0, start at beginning of track
	if (drive->track != drive->trackprev)
	{
		drive->trackprev = drive->track;
		sector = 0;

		seekSector = (unsigned long)drive->track;
		seekSector *= SECTOR_COUNT;
		FileSeek(&file, seekSector);
	}
	else
	{
		// same track, start at next sector in track
		sector = drive->sectoroffset;
	}

	#ifdef DEBUG_ADF
	EnableFpga();
	status = SPI(0);				//read request signal
	track = SPI(0);					//track number (cylinder & head)
	dsksync  = SPI(0) << 8;			//disk sync high byte
	dsksync |= SPI(0);				//disk sync low byte
	dsklen = (SPI(0) & 0x3F) << 8;	//msb of mfm words to transfer
	dsklen |= SPI(0);				//lsb of mfm words to transfer
	DisableFpga();
	printf("\r\nS:%02X T:%02X [%04X] [%04X]:", status, track, dsksync, dsklen);
	#endif
	
	while (1)
	{
		FileRead(&file);

		EnableFpga();

		// check if FPGA is still asking for data
		status = SPI(0);				//read request signal
		track = SPI(0);					//track number (cylinder & head)
		dsksync  = SPI(0)<<8;			//disk sync high byte
		dsksync |= SPI(0);				//disk sync low byte
		dsklen = (SPI(0) & 0x3F) << 8;	//msb of mfm words to transfer
		dsklen |= SPI(0);				//lsb of mfm words to transfer

//		if (track >= drive->tracks)
//		{  	track = drive->tracks - 1;   }

	    #ifdef DEBUG_ADF
		printf("\r\n[%02X] %X:%04X", track, sector, dsklen);
		#endif

		if (dsksync == 0x0000 || dsksync == 0x8914 || dsksync == 0xA144) //work around for Copy Lock in Wiz'n'Liz
		{
			// KS 1.3 doesn't write dsksync register after reset, probably uses soft sync
			dsksync = 0x4489;
		}
		//North&South: $A144
		//Wiz'n'Liz (Copy Lock): $8914
		//Prince of Persia: $4891
		//Commando: $A245


		//some loaders stop dma if sector header isn't what they expect
		//we don't check dma transfer count after sending every word
		//so the track can be changed while we are sending the rest of the previous sector
		//in this case let's start transfer from the beginning
		if (track == drive->track)
		{
			// send sector if fpga is still asking for data
			if (status & CMD_RDTRK)
			{
				// Transfer sector and get remaining dma count
				dsklen = SectorToFpga(sector, drive->track, (unsigned char)(dsksync >> 8), (unsigned char)dsksync);
				
				// printing remaining dma count
				#ifdef DEBUG_ADF
				printf("-%04X", dsklen);
				#endif

				if (sector == LAST_SECTOR && dsklen > 0)
				{
					SectorGapToFpga();
					#ifdef DEBUG_ADF
					printf("+++");
					#endif
				}
			}
		}

		// we are done accessing FPGA
		DisableFpga();

		//track has changed
		if (track != drive->track)
		{	break;		}

		//read dma request
		if (!(status & CMD_RDTRK))
		{	break;	}

		sector++;
		if (sector < SECTOR_COUNT)
		{	FileNextSector(&file);	}
		else
		{
			//go to the start of current track
			sector = 0;
				
			seekSector = (unsigned long)drive->track;
			seekSector *= SECTOR_COUNT;
			FileSeek(&file, seekSector);
		}

		#ifdef DEBUG_ADF
		printf("->");
		#endif
	}

	//remember current sector and cluster
	drive->sectoroffset = sector;
	drive->clusteroffset = file.cluster;

	#ifdef DEBUG_ADF
//	timepassed = GetTimer(0) - timepassed;
//	printf("\r\n Track read time:%d", timepassed);
	printf(":OK\r\n");
	#endif
}


void WriteTrack(struct adfTYPE *drive)
{
	unsigned char sector;
	unsigned char writeTrack;
	unsigned char writeSector;
	unsigned long seekSector;

	// Prepare Global File Handle
	PrepareGlobalFileHandle(drive);
	
	//setting file pointer to begining of current track
	seekSector = (unsigned long)drive->track;
	seekSector *=SECTOR_COUNT;
	FileSeek(&file, seekSector);

	sector = 0;

	// Store Requested track to previous track
	drive->trackprev = drive->track;

	#ifdef DEBUG_ADF
	printf("*%d:\r", drive->track);
	#endif

	while (FindSync(drive))
	{
		if (GetHeader(&writeTrack, &writeSector))
		{
			// Position on floppy track
			if (writeTrack == drive->track)
			{
				while (sector != writeSector)
				{
					if (sector < writeSector)
					{
						FileNextSector(&file);
						sector++;
					}
					else
					{
						seekSector = (unsigned long)drive->track;
						seekSector *=SECTOR_COUNT;
						FileSeek(&file, seekSector);

						sector = 0;
					}
				}

				// Write Sector to floppy
				if (GetData())
				{
					if (drive->status & DSK_WRITABLE)
					{	FileWrite(&file);		}
					else
					{
						Error = 30;
						#ifdef DEBUG_ADF
						printf("Write attempt to protected disk!\r");
						#endif
					}
				}
			}
			else
			{
				//track number reported in sector header is not the same as current drive track
				Error = 27;
			}
		}
		
		if (Error)
		{
			#ifdef DEBUG_ADF
			printf("WriteTrack: error %d\r", Error);
			#endif
			ErrorMessage("  WriteTrack", Error);
		}
	}
	
	//remember current sector and cluster
	drive->sectoroffset = sector;
	drive->clusteroffset = file.cluster;
}


//this function reads data from fifo till it finds sync word
// or fifo is empty and dma inactive (so no more data is expected)
unsigned char FindSync(struct adfTYPE * drive)
{
	unsigned char  c1, c2, c3, c4;
	unsigned short n;

	while (1)
	{
		EnableFpga();
		c1 = SPI(0); //write request signal
		c2 = SPI(0); //track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
		{	break;	}
		if (c2 != drive->track)
		{	break;	}
		SPI(0); //disk sync high byte
		SPI(0); //disk sync low byte
		c3 = SPI(0) & 0xBF; //msb of mfm words to transfer
		c4 = SPI(0); //lsb of mfm words to transfer

		if (c3==0 && c4==0)
		{	break;	}

		n = ((c3 & 0x3F) << 8) + c4;

		while (n--)
		{
			c3 = SPI(0);
			c4 = SPI(0);
			if (c3==0x44 && c4==0x89)
			{
				DisableFpga();
				#ifdef DEBUG_ADF
				printf("#SYNC:");
				#endif
				return 1;
			}
		}
		DisableFpga();
	}
	
	DisableFpga();
	
	return 0;
}


//this function reads data from fifo till it finds sync word or dma is inactive
unsigned char GetHeader(unsigned char * pTrack, unsigned char * pSector)
{
	unsigned char c, c1, c2, c3, c4;
	unsigned char i;
	unsigned char checksum[4];

	Error = 0;
	while (1)
	{
		EnableFpga();
		c1 = SPI(0); //write request signal
		c2 = SPI(0); //track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
		{	break;	}
		SPI(0); //disk sync high byte
		SPI(0); //disk sync low byte
		c3 = SPI(0); //msb of mfm words to transfer
		c4 = SPI(0); //lsb of mfm words to transfer

		if ((c3 & 0x3F) != 0 || c4 > 24)//remaining header data is 25 mfm words
		{
			c1 = SPI(0); //second sync lsb
			c2 = SPI(0); //second sync msb
			if (c1 != 0x44 || c2 != 0x89)
			{
				Error = 21;
				#ifdef DEBUG_ADF
				printf("\rSecond sync word missing...\r");
				#endif
				break;
			}

			c = SPI(0);
			checksum[0] = c;
			c1 = (c & MFM_DATA_BITS_MASK)<<1;
			c = SPI(0);
			checksum[1] = c;
			c2 = (c & MFM_DATA_BITS_MASK)<<1;
			c = SPI(0);
			checksum[2] = c;
			c3 = (c & MFM_DATA_BITS_MASK)<<1;
			c = SPI(0);
			checksum[3] = c;
			c4 = (c & MFM_DATA_BITS_MASK)<<1;

			c = SPI(0);
			checksum[0] ^= c;
			c1 |= c & MFM_DATA_BITS_MASK;
			c = SPI(0);
			checksum[1] ^= c;
			c2 |= c & MFM_DATA_BITS_MASK;
			c = SPI(0);
			checksum[2] ^= c;
			c3 |= c & MFM_DATA_BITS_MASK;
			c = SPI(0);
			checksum[3] ^= c;
			c4 |= c & MFM_DATA_BITS_MASK;


			if (c1 != 0xFF)//always 0xFF
			{	Error = 22;	}
			else if (c2 > 159)//Track number (0-159)
			{	Error = 23;	}
			else if (c3 > 10)//Sector number (0-10)
			{	Error = 24;	}
			else if (c4 > 11 || c4==0)//Number of sectors to gap (1-11)
			{	Error = 25;	}

			if (Error)
			{
				#ifdef DEBUG_ADF
				printf("\rWrong header: %d.%d.%d.%d\r", c1, c2, c3, c4);
				#endif
				break;
			}

			#ifdef DEBUG_ADF
			printf("T%dS%d\r", c2, c3);
			#endif

			*pTrack = c2;
			*pSector = c3;

			for (i = 0; i < 8; i++)
			{
				checksum[0] ^= SPI(0);
				checksum[1] ^= SPI(0);
				checksum[2] ^= SPI(0);
				checksum[3] ^= SPI(0);
			}

			checksum[0] &= MFM_DATA_BITS_MASK;
			checksum[1] &= MFM_DATA_BITS_MASK;
			checksum[2] &= MFM_DATA_BITS_MASK;
			checksum[3] &= MFM_DATA_BITS_MASK;

			c1 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c2 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c3 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c4 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;

			c1 |= SPI(0) & MFM_DATA_BITS_MASK;
			c2 |= SPI(0) & MFM_DATA_BITS_MASK;
			c3 |= SPI(0) & MFM_DATA_BITS_MASK;
			c4 |= SPI(0) & MFM_DATA_BITS_MASK;

			if (c1!=checksum[0] || c2!=checksum[1] || c3!=checksum[2] || c4!=checksum[3])
			{
				Error = 26;
				break;
			}

			DisableFpga();
			return 1;
		}
		else //not enough data for header
			if ((c3&0x80)==0)//write dma is not active
			{
				Error = 20;
				break;
			}

			DisableFpga();
	}

	DisableFpga();
	return 0;
}


unsigned char GetData(void)
{
	unsigned char c, c1, c2, c3, c4;
	unsigned char i;
	unsigned char *p;
	unsigned short n;
	unsigned char checksum[4];

	Error = 0;
	while (1)
	{
		EnableFpga();
		c1 = SPI(0); //write request signal
		c2 = SPI(0); //track number (cylinder & head)
		if (!(c1 & CMD_WRTRK))
		{	break;	}
		SPI(0); //disk sync high byte
		SPI(0); //disk sync low byte
		c3 = SPI(0); //msb of mfm words to transfer
		c4 = SPI(0); //lsb of mfm words to transfer

		n = ((c3&0x3F)<<8) + c4;

		if (n >= 0x204)
		{
			c1 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c2 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c3 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;
			c4 = (SPI(0) & MFM_DATA_BITS_MASK)<<1;

			c1 |= SPI(0) & MFM_DATA_BITS_MASK;
			c2 |= SPI(0) & MFM_DATA_BITS_MASK;
			c3 |= SPI(0) & MFM_DATA_BITS_MASK;
			c4 |= SPI(0) & MFM_DATA_BITS_MASK;

			checksum[0] = 0;
			checksum[1] = 0;
			checksum[2] = 0;
			checksum[3] = 0;

			/*odd bits of data field*/
			i = SECTOR_BYTES / 4;
			p = secbuf;
			do
			{
				c = SPI(0);
				checksum[0] ^= c;
				*p++ = (c & MFM_DATA_BITS_MASK)<<1;
				c = SPI(0);
				checksum[1] ^= c;
				*p++ = (c & MFM_DATA_BITS_MASK)<<1;
				c = SPI(0);
				checksum[2] ^= c;
				*p++ = (c & MFM_DATA_BITS_MASK)<<1;
				c = SPI(0);
				checksum[3] ^= c;
				*p++ = (c & MFM_DATA_BITS_MASK)<<1;
			}
			while (--i);

			/*even bits of data field*/
			i = SECTOR_BYTES / 4;
			p = secbuf;
			do
			{
				c = SPI(0);
				checksum[0] ^= c;
				*p++ |= c & MFM_DATA_BITS_MASK;
				c = SPI(0);
				checksum[1] ^= c;
				*p++ |= c & MFM_DATA_BITS_MASK;
				c = SPI(0);
				checksum[2] ^= c;
				*p++ |= c & MFM_DATA_BITS_MASK;
				c = SPI(0);
				checksum[3] ^= c;
				*p++ |= c & MFM_DATA_BITS_MASK;
			}
			while (--i);

			checksum[0] &= MFM_DATA_BITS_MASK;
			checksum[1] &= MFM_DATA_BITS_MASK;
			checksum[2] &= MFM_DATA_BITS_MASK;
			checksum[3] &= MFM_DATA_BITS_MASK;

			if (c1 != checksum[0] || c2 != checksum[1] || c3 != checksum[2] || c4 != checksum[3])
			{
				Error = 29;
				break;
			}

			DisableFpga();
			return 1;
		}
		else //not enough data in fifo
			if ((c3 & 0x80)==0)//write dma is not active
			{
				Error = 28;
				break;
			}

			DisableFpga();
	}
	DisableFpga();
	return 0;
}


//this function sends the data in the sector buffer to the FPGA, translated
//into an Amiga floppy format sector
//sector is the sector number in the track
//track is the track number
//NOT TRUE:
//note that we do not insert clock bits because they will be stripped
//by the Amiga software anyway
unsigned short SectorToFpga(unsigned char sector, unsigned char track, unsigned char dsksynch, unsigned char dsksyncl)
{
	unsigned char c,i;
	unsigned char csum[4];
	unsigned char *p;
//	unsigned char c3, c4;
	unsigned short dsklen;

	// preamble
	SPI(MFM_CLOCK_BITS);
	SPI(MFM_CLOCK_BITS);
	SPI(MFM_CLOCK_BITS);
	SPI(MFM_CLOCK_BITS);

	// synchronization
	SPI(dsksynch);
	SPI(dsksyncl);
	SPI(dsksynch);
	SPI(dsksyncl);

	// odd bits of header
	// And calculate checksum
	c=MFM_DATA_BITS_MASK;
	csum[0]=c;
	SPI(c);
	c=(track>>1) & MFM_DATA_BITS_MASK;
	csum[1]=c;
	SPI(c);
	c=(sector>>1) & MFM_DATA_BITS_MASK;
	csum[2]=c;
	SPI(c);
	c=((SECTOR_COUNT-sector)>>1) & MFM_DATA_BITS_MASK;
	csum[3]=c;
	SPI(c);

	// even bits of header
	c=MFM_DATA_BITS_MASK;
	csum[0]^=c;
	SPI(c);
	c=track & MFM_DATA_BITS_MASK;
	csum[1]^=c;
	SPI(c);
	c=sector & MFM_DATA_BITS_MASK;
	csum[2]^=c;
	SPI(c);
	c=(SECTOR_COUNT-sector) & MFM_DATA_BITS_MASK;
	csum[3]^=c;
	SPI(c);

	// sector label and reserved area (changes nothing to checksum)
	i=32;
	do
	{	SPI(MFM_CLOCK_BITS);	}
	while(--i);

	//checksum over header
	SPI((csum[0]>>1)|MFM_CLOCK_BITS);
	SPI((csum[1]>>1)|MFM_CLOCK_BITS);
	SPI((csum[2]>>1)|MFM_CLOCK_BITS);
	SPI((csum[3]>>1)|MFM_CLOCK_BITS);
	SPI(csum[0]|MFM_CLOCK_BITS);
	SPI(csum[1]|MFM_CLOCK_BITS);
	SPI(csum[2]|MFM_CLOCK_BITS);
	SPI(csum[3]|MFM_CLOCK_BITS);

	//calculate data checksum
	csum[0]=0;
	csum[1]=0;
	csum[2]=0;
	csum[3]=0;
	i=SECTOR_BYTES/4;
	p=secbuf;
	do
	{
		c=*(p++);
		csum[0]^=c>>1;
		csum[0]^=c;
		
		c=*(p++);
		csum[1]^=c>>1;
		csum[1]^=c;
		
		c=*(p++);
		csum[2]^=c>>1;
		csum[2]^=c;

		c=*(p++);
		csum[3]^=c>>1;
		csum[3]^=c;
	}
	while (--i);
	csum[0]&=MFM_DATA_BITS_MASK;
	csum[1]&=MFM_DATA_BITS_MASK;
	csum[2]&=MFM_DATA_BITS_MASK;
	csum[3]&=MFM_DATA_BITS_MASK;


	//checksum over data
	SPI((csum[0]>>1)|MFM_CLOCK_BITS);
	SPI((csum[1]>>1)|MFM_CLOCK_BITS);
	SPI((csum[2]>>1)|MFM_CLOCK_BITS);
	SPI((csum[3]>>1)|MFM_CLOCK_BITS);
	SPI(csum[0]|MFM_CLOCK_BITS);
	SPI(csum[1]|MFM_CLOCK_BITS);
	SPI(csum[2]|MFM_CLOCK_BITS);
	SPI(csum[3]|MFM_CLOCK_BITS);

	//odd bits of data field
	i=SECTOR_BYTES/4;
	p=secbuf;
	do
	{
		c=*(p++);
		SSPBUF=(c >> 1) | MFM_CLOCK_BITS;
		while (!BF);

		c=*(p++);
		SSPBUF=(c >> 1) | MFM_CLOCK_BITS;
		while (!BF);

		c=*(p++);
		SSPBUF=(c >> 1) | MFM_CLOCK_BITS;
		while (!BF);
		dsklen = (SSPBUF & 0x3f) << 8;

		c=*(p++);
		SSPBUF=(c >> 1) | MFM_CLOCK_BITS;
		while (!BF);
		dsklen |= SSPBUF; 
	}
	while (--i && dsklen);
//	while (--i);
	
	if(0 == dsklen)
	{	return dsklen;	}

	//even bits of data field
	i=SECTOR_BYTES/4;
	p=secbuf;
	do
	{
		c=*(p++);
		SSPBUF=c | MFM_CLOCK_BITS;
		while (!BF);

		c=*(p++);
		SSPBUF=c | MFM_CLOCK_BITS;
		while (!BF);
		
		c=*(p++);
		SSPBUF=c | MFM_CLOCK_BITS;
		while (!BF);
//		c3 = SSPBUF;
		dsklen = (SSPBUF & 0x3f) << 8;
		
		c=*(p++);
		SSPBUF=c | MFM_CLOCK_BITS;
		while (!BF);
//		c4 = SSPBUF;
		dsklen |= SSPBUF; 
	}
	while (--i && dsklen);
//	while (--i);

//	return(((c3 &0x3F) << 8)|c4);
	return dsklen;
}

/*
// sends the data in the sector buffer to the FPGA, translated into an Amiga floppy format sector 
// note that we do not insert clock bits because they will be stripped by the Amiga software anyway 
unsigned short SectorToFpga(unsigned char sector, unsigned char track, unsigned char dsksynch, unsigned char dsksyncl) 
{
	unsigned char checksum[4];
	unsigned short i;
	unsigned char x;
	unsigned char *p;
	
	// preamble
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	
	// synchronization
	SPI(dsksynch);
	SPI(dsksyncl);
	SPI(dsksynch);
	SPI(dsksyncl);
	
	// odd bits of header
	x = 0x55;
	checksum[0] = x;
	SPI(x);
	x = track >> 1 & 0x55;
	checksum[1] = x;
	SPI(x);
	x = sector >> 1 & 0x55;
	checksum[2] = x;
	SPI(x);
	x = 11 - sector >> 1 & 0x55;
	checksum[3] = x;
	SPI(x);
	
	// even bits of header
	x = 0x55;
	checksum[0] ^= x;
	SPI(x);
	x = track & 0x55;
	checksum[1] ^= x;
	SPI(x);
	x = sector & 0x55;
	checksum[2] ^= x;
	SPI(x);
	x = 11 - sector & 0x55;
	checksum[3] ^= x;
	SPI(x);
	
	// sector label and reserved area (changes nothing to checksum)
	i = 0x20;
	while (i--)
		SPI(0xAA);
	
	// send header checksum
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	SPI(checksum[0] | 0xAA);
	SPI(checksum[1] | 0xAA);
	SPI(checksum[2] | 0xAA);
	SPI(checksum[3] | 0xAA);
	
	// calculate data checksum
	checksum[0] = 0;
	checksum[1] = 0;
	checksum[2] = 0;
	checksum[3] = 0;
	p = secbuf;
	i = DATA_SIZE / 2 / 4;
	while (i--)
	{
		x = *p++;
		checksum[0] ^= x ^ x >> 1;
		x = *p++;
		checksum[1] ^= x ^ x >> 1;
		x = *p++;
		checksum[2] ^= x ^ x >> 1;
		x = *p++;
		checksum[3] ^= x ^ x >> 1;
	}
	
	// send data checksum
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	SPI(0xAA);
	SPI(checksum[0] | 0xAA);
	SPI(checksum[1] | 0xAA);
	SPI(checksum[2] | 0xAA);
	SPI(checksum[3] | 0xAA);
	
	// odd bits of data field
	i = DATA_SIZE / 2;
	p = secbuf;
	while (i--)
		SPI(*p++ >> 1 | 0xAA);
	
	// even bits of data field
	i = DATA_SIZE / 2;
	p = secbuf;
	while (i--)
		SPI(*p++ | 0xAA); 
	
	return 0;
}
*/

void SectorGapToFpga()
{
//	unsigned short i = GAP_SIZE;
//	do
//	{	SPI(MFM_CLOCK_BITS);	}
//	while (--i);
	
	unsigned short i;
	for(i=0; i < GAP_SIZE; i++)
	{	SPI(MFM_CLOCK_BITS);	}
}
