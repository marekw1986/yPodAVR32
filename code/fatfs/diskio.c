/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2014        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "diskio.h"		/* FatFs lower layer API */
#include <stdio.h>
#include <stdlib.h>

#include "sd.h"


/* Definitions of physical drive number for each drive */
#define MMC             0	/* Example: Map MMC/SD card to physical drive 0 */
#define SPI_FLASH		1	/* Example: Map SPI flash memory to physical drive 1 */

DSTATUS SDStatus = STA_NOINIT;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	switch (pdrv) {
         
        case MMC:
            return SDStatus;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{	
	switch (pdrv) {
        case MMC:
            //if (SDStatus & STA_NODISK) return STA_NOINIT; //No card in the socket
            if (sd_init() == 0) SDStatus &= ~STA_NOINIT;
            return SDStatus;
	}
	return STA_NOINIT;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	DWORD sector,	/* Sector address in LBA */
	UINT count		/* Number of sectors to read */
)
{
	switch (pdrv) {            
        case MMC:
            while (count) {
                if(!sd_readSECTOR(sector,(char*)buff)) break;
                buff+=512;
                sector++;
                count--;
            }
            return count ? RES_ERROR : RES_OK;
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	DWORD sector,		/* Sector address in LBA */
	UINT count			/* Number of sectors to write */
)
{
	switch (pdrv) {            
        case MMC:
            while(count){
                if(!sd_writeSECTOR(sector,(char*)buff)) break;
                buff+=512;
                sector++;
                count--;
            }
            return count ? RES_ERROR : RES_OK;
	}

	return RES_PARERR;
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	switch (pdrv) {
        case MMC:
            if (cmd == GET_SECTOR_SIZE) {
                *(WORD*)buff = 512;
                return RES_OK;
            }
            if (SDStatus & STA_NOINIT) {return RES_NOTRDY;}
	}

	return RES_PARERR;
}


void disk_timerproc (void)
{
	DSTATUS s;

	s = SDStatus;
	if (sd_getCD()) {s &= ~STA_NODISK;}
	else {s |= (STA_NODISK | STA_NOINIT);}
	SDStatus = s;
}


#endif
