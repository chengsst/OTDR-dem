/*
**************************************************************************

rec_single_ascii.cpp                                     (c) Spectrum GmbH

**************************************************************************

Does a continous FIFO transfer and writes data as hex to an ascii file

Change the global flag g_bThread to use the threaded version or the plain
and more simple loop.

Feel free to use this source for own projects and modify it in any kind

Documentation for the API as well as a detailed description of the hardware
can be found in the manual for each device which can be found on our website:
https://www.spectrum-instrumentation.com/en/downloads

Further information can be found online in the Knowledge Base:
https://www.spectrum-instrumentation.com/en/knowledge-base-overview

**************************************************************************
*/


// ----- include standard driver header from library -----
#include "../c_header/dlltyp.h"
#include "../c_header/regs.h"
#include "../c_header/spcerr.h"
#include "../c_header/spcm_drv.h"

// ----- include of common example librarys -----
#include "../common/spcm_lib_card.h"
#include "../common/spcm_lib_data.h"
#include "../common/spcm_lib_thread.h"

// ----- standard c include files -----
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <iostream>



// ----- IQdem need
#include "mclmcr.h"
#include "matrix.h"
#include "mclcppclass.h"
#include "IQdem.h"


#pragma comment(lib,"IQdem.lib")
#pragma comment(lib,"mclmcrrt.lib")
#pragma comment(lib,"libmx.lib")
#pragma comment(lib,"libmat.lib")
#pragma comment(lib,"mclmcr.lib")



#define maxcol 40000000
#define maxrow 100
// ----- this is the global thread flag that defines whether we use the thread or non-thread loop -----
bool g_bThread = false;


int row = 40;
int col = 12160;
int64 sample_v;
int fec=20000;
double data_t[MEGA_B(1)];
double data[MEGA_B(1)];
double *var_value = new double[maxcol];
double *location = new double[maxcol];

typedef enum E_FILETYPE { eFT_noWrite, eFT_PlainBinary, eFT_PlainWithTimestamps, eFT_SB5_Stream } E_FILETYPE;

#define FILENAME "location_test.txt"

/*
**************************************************************************
Working routine data
**************************************************************************
*/

struct ST_WORKDATA
{
	FILE*   hFile;
	int64   llFileSize;
	bool    bFirst;
	int16*  ppnChannelData[SPCM_MAX_AICHANNEL];
	int32   lNotifySamplesPerChannel;
	int32       lSegmentsize, lPosttrigger;
	int64       llSamplesWritten;
	int64       llMemsize;
};

/*
**************************************************************************
bDoCardSetup: setup matching the calculation routine
**************************************************************************
*/

bool bDoCardSetup(ST_WORKDATA* pstWorkData, ST_BUFFERDATA* pstBufferData)
    {
    int     i;
	int64   llChannelMask;


	// Multiple Recording setup
	if (pstBufferData->pstCard->lMaxChannels >= 64)
		llChannelMask = -1; // -1 is all bits set to 1 = 0xffffffffffffffff
	else
		llChannelMask = ((int64)1 << pstBufferData->pstCard->lMaxChannels) - 1;


	// FIFO mode setup, we run continuously and use 1024 samples of pretrigger for each segment
	pstWorkData->llMemsize = MEGA_B(1);
	pstWorkData->lSegmentsize = KILO_B(16);
	pstWorkData->lPosttrigger = KILO_B(15);         // segment size 16384,


	bSpcMSetupModeRecStdMulti(pstBufferData->pstCard, llChannelMask, pstWorkData->llMemsize, pstWorkData->lSegmentsize, pstWorkData->lPosttrigger);

	

    // we try to set the samplerate to 1 MHz (M2i) or 20 MHz (M3i, M4i) on internal PLL, no clock output
    // increase this to test the read-out-after-overrun
	if (pstBufferData->pstCard->bM2i)
		bSpcMSetupClockPLL(pstBufferData->pstCard, MEGA(1), false);
	else if (pstBufferData->pstCard->bM2p)
		bSpcMSetupClockPLL(pstBufferData->pstCard, MEGA(10), false);
	else if (pstBufferData->pstCard->bM3i || pstBufferData->pstCard->bM4i)
		bSpcMSetupClockPLL(pstBufferData->pstCard, MEGA(500), false);
	printf("Sampling rate set to %.1lf MHz\n", (double)pstBufferData->pstCard->llSetSamplerate / 1000000.0);


	sample_v = pstBufferData->pstCard->llSetSamplerate;//samplerate

	// we set external trigger for multiple recording
	bSpcMSetupTrigExternal(pstBufferData->pstCard, SPC_TM_POS, false, 0);


    // type dependent card setup
	switch (pstBufferData->pstCard->eCardFunction)
        {

        // analog acquisition card setup
        case AnalogIn:
			
            // program all input channels to +/-1 V and 50 ohm termination (if it's available)
			if (pstBufferData->pstCard->bM2i || pstBufferData->pstCard->bM2p)
                {
				for (i = 0; i < pstBufferData->pstCard->lMaxChannels; i++)
					bSpcMSetupInputChannel(pstBufferData->pstCard, i, 1000, true);
                }
            else
                {
                bool bTerm           = true;
                bool bACCoupling     = false;
                bool bBandwidthLimit = false;
				for (i = 0; i < pstBufferData->pstCard->lMaxChannels; i++)
					bSpcMSetupPathInputCh(pstBufferData->pstCard, i, 0, 1000, bTerm, bACCoupling, bBandwidthLimit);
                }
            break;

        // digital acquisition card setup
        case DigitalIn:
        case DigitalIO:


			// we enable 16 channels for the example
			bSpcMSetupModeRecFIFOMulti(pstBufferData->pstCard, 0xffff, pstWorkData->lSegmentsize, pstWorkData->lSegmentsize-128 );


            // set all input channel groups to 110 ohm termination (if it's available)
			for (i = 0; i < pstBufferData->pstCard->uCfg.stDIO.lGroups; i++)
				bSpcMSetupDigitalInput(pstBufferData->pstCard, i, true);
            break;
        }

	// set up the timestamp mode to standard if timestamp is installed
	if (pstBufferData->bStartTimestamp)
		bSpcMSetupTimestamp(pstBufferData->pstCard, SPC_TSMODE_STANDARD | SPC_TSCNT_INTERNAL, 0);

	return pstBufferData->pstCard->bSetError;
    }




/*
**************************************************************************
Setup working routine
**************************************************************************
*/

bool bWorkInit (void* pvWorkData, ST_BUFFERDATA* pstBufferData)
    {
    ST_WORKDATA* pstWorkData = (ST_WORKDATA*) pvWorkData;

    // setup for the transfer, to avoid overrun we use quite large blocks as this has a better throughput to hard disk
    pstBufferData->dwDataBufLen = MEGA_B(128);//1024*1024*m
    pstBufferData->dwDataNotify = MEGA_B(2);
	pstBufferData->dwTSBufLen = MEGA_B(1);
	pstBufferData->dwTSNotify = KILO_B(4);


    // setup for the work
    pstWorkData->hFile = fopen (FILENAME, "wt");
    pstWorkData->llFileSize = 0;
	pstWorkData->bFirst = true;
	pstWorkData->lNotifySamplesPerChannel = pstBufferData->dwDataNotify / pstBufferData->pstCard->lSetChannels / pstBufferData->pstCard->lBytesPerSample;
	pstWorkData->llSamplesWritten = 0;

	// allocate arrays for channel data of notify size
	//Ϊ֪ͨ��С��ͨ�����ݷ�������
	for (int i = 0; i < pstBufferData->pstCard->lSetChannels; i++)
	{
		pstWorkData->ppnChannelData[i] = (int16*)pvAllocMemPageAligned(pstWorkData->lNotifySamplesPerChannel * 2);
	}


	if (!IQdemInitialize())

		{
			printf("Could not initialize IQdem!");

		}

    return (pstWorkData->hFile != NULL);
    }

void mIQdem()
{

	

	//*********����IQdem����
	mwArray data_in(row, col, mxDOUBLE_CLASS);//���ж�������
	mwArray data_row(1, 1, mxDOUBLE_CLASS);
	mwArray data_col(1, 1, mxDOUBLE_CLASS);
	mwArray sample_rate(1, 1, mxDOUBLE_CLASS);
	mwArray impulse_frecncy(1, 1, mxDOUBLE_CLASS);


	//*********��������
	data_in.SetData(data, row*col);
	data_row.SetData(&row, 1);
	data_col.SetData(&col, 1);
	sample_rate.SetData(&sample_v, 1);
	impulse_frecncy.SetData(&fec, 1);


	//*********����IQdem���
	mwArray distance(1, col, mxDOUBLE_CLASS);
	mwArray varmap(1, col, mxDOUBLE_CLASS);
	mwArray capTime(1, row, mxDOUBLE_CLASS);


	//*********���
	IQdem(3, varmap, distance, capTime, data_row, data_col, data_in, sample_rate, impulse_frecncy);


	//*********������ݴ洢
	varmap.GetData(var_value, col);
	
	distance.GetData(location, col);

	
}

template <class T>
void datatrans(  
	T* pTData,             // generic data array
	uint32 lLenInSamples)  // length of the data array
{
	if (!pTData)
		printf("Could not read data!");

	uint32 j;


	for (j = 0; j < lLenInSamples; j++, pTData++)
	{
		data_t[j] = *pTData;
	}


}
/*
**************************************************************************
bWorkDo: stores data to hard disk
**************************************************************************
*/

bool bWorkDo (void* pvWorkData, ST_BUFFERDATA* pstBufferData)
    {
    ST_WORKDATA* pstWorkData = (ST_WORKDATA*) pvWorkData;
    int32 lChannels =   pstBufferData->pstCard->lSetChannels;
    int32 lSamples =    pstBufferData->dwDataNotify / pstBufferData->pstCard->lBytesPerSample;
    FILE* f =           pstWorkData->hFile;
    int32			lIndex,i,j;
	double			dAverage;
	uint32          dwTimestampBytes;
	double			threshold = 200.0;
	int				len = 12166;

	// ----- M2i+M3i timestamps are 64 bit, M4i 128 bit -----
	dwTimestampBytes = 0;
	if (pstBufferData->pstCard->bM2i || pstBufferData->pstCard->bM3i)
		dwTimestampBytes = 8;
	else if (pstBufferData->pstCard->bM4i || pstBufferData->pstCard->bM2p)
		dwTimestampBytes = 16;
	
    // we only care for blocks of notify size
    if (pstBufferData->llDataAvailBytes < pstBufferData->dwDataNotify)
    pstBufferData->llDataAvailBytes = pstBufferData->dwDataNotify;

	// now let's split up the data
	bSpcMDemuxAnalogData(pstBufferData->pstCard, pstBufferData->pvDataCurrentBuf, pstWorkData->lNotifySamplesPerChannel, pstWorkData->ppnChannelData);
	

	//datatrans(pstWorkData->ppnChannelData[0], pstWorkData->lNotifySamplesPerChannel);


	for (lIndex = 0; lIndex < row*col; lIndex++)
	{
		for (j = 0; j < row; j++)
		{
			for (i = 130; i <col + 130; i++)
			{
				data[lIndex] = 500 * dSpcMIntToVoltage(pstBufferData->pstCard, 0, data_t[j*pstWorkData->lSegmentsize + i]);
			}
		}
	}



	mIQdem();


	for (lIndex = 0; lIndex < col; lIndex++)
	{
		if (var_value[lIndex]>=threshold)
		{
			fprintf(f, "%9.2f m\n ", location[lIndex]);
		}
	}

	
	pstWorkData->llFileSize += 7 * lSamples + 2 * (lSamples / lChannels);
	
	// calculate average of first channel
	dAverage = dSpcMCalcAverage(pstWorkData->ppnChannelData[0], pstWorkData->lNotifySamplesPerChannel);
	dAverage = dSpcMIntToVoltage(pstBufferData->pstCard, 0, dAverage);


    // announce the number of data that has been written
	printf(
		"\r%.2f MSamples (sum) written(file size: %.2f MB)(Average: %9.2f mV)%.2f MSamples per",
        (double) pstBufferData->qwDataTransferred / pstBufferData->pstCard->lBytesPerSample / 1024.0 / 1024.0, 
        (double) pstWorkData->llFileSize / 1024 / 1024,
		1000.0 * dAverage,
		pstWorkData->lNotifySamplesPerChannel/1024.0/1024.0
		);

    return true;
    }




/*
**************************************************************************
vWorkClose: Close the work and clean up
**************************************************************************
*/

void vWorkClose (void* pvWorkData, ST_BUFFERDATA* pstBufferData)
    {
    ST_WORKDATA* pstWorkData = (ST_WORKDATA*) pvWorkData;

    if (pstWorkData->hFile)
        fclose (pstWorkData->hFile);
	IQdemTerminate();
    }






/*
**************************************************************************
main 
**************************************************************************
*/

int main ()
    {
    char                szBuffer[1024];     // a character buffer for any messages
    ST_SPCM_CARDINFO    stCard;             // info structure of my card
    ST_BUFFERDATA       stBufferData;       // buffer and transfer definitions
    ST_WORKDATA         stWorkData;         // work data for the working functions


    // ------------------------------------------------------------------------
    // init card number 0 (the first card in the system), get some information and print it
    // uncomment the second line and replace the IP address to use remote
    // cards like in a digitizerNETBOX
    if (bSpcMInitCardByIdx (&stCard, 0))
    //if (bSpcMInitCardByIdx (&stCard, "192.168.1.10", 0))
        {
        printf (pszSpcMPrintDocumentationLink (&stCard, szBuffer, sizeof (szBuffer)));
        printf (pszSpcMPrintCardInfo (&stCard, szBuffer, sizeof (szBuffer)));
        }
    else
        return nSpcMErrorMessageStdOut (&stCard, "Error: Could not open card\n", true);
	
	memset(&stBufferData, 0, sizeof(stBufferData));
	stBufferData.pstCard = &stCard;

    // check whether we support this card type in the example
    if ((stCard.eCardFunction != AnalogIn) && (stCard.eCardFunction != DigitalIn) && (stCard.eCardFunction != DigitalIO))
        return nSpcMErrorMessageStdOut (&stCard, "Error: Card function not supported by this example\n", false);
	if ((stCard.lFeatureMap & SPCM_FEAT_MULTI) == 0)
		return nSpcMErrorMessageStdOut(&stCard, "Error: Multiple Recording Option not installed. Examples was done especially for this option!\n", false);

	// set a flag if timestamp is installed
	stBufferData.bStartTimestamp = ((stCard.lFeatureMap & SPCM_FEAT_TIMESTAMP) != 0);

    // ------------------------------------------------------------------------
    // do the card setup, error is routed in the structure so we don't care for the return values
    if (!stCard.bSetError)
		bDoCardSetup(&stWorkData, &stBufferData);

	// some additional information on the acquisition
	if (!stCard.bSetError)
	{
		printf("\nData information:\n=================\n");
		printf("Each segment is %.3f ms long\n", 1000.0 * stWorkData.lSegmentsize / stCard.llSetSamplerate);
		printf("Maximum pulse repetition frequency to reach with this setting is %.2f Hz\n", (double)stCard.llSetSamplerate / stWorkData.lSegmentsize);
	}


    // ------------------------------------------------------------------------
    // setup the data transfer thread and start it, we use atimeout of 5 s in the example

    stBufferData.bStartCard =   true;
    stBufferData.bStartData =   true;
	stBufferData.bStartTimestamp = ((stCard.lFeatureMap & SPCM_FEAT_TIMESTAMP) != 0);

    stBufferData.lTimeout =     5000;
    // start the threaded version if g_bThread is defined
    if (!stCard.bSetError && g_bThread)
        vDoThreadMainLoop (&stBufferData, &stWorkData, bWorkInit, bWorkDo, vWorkClose, bKeyAbortCheck);

    // start the unthreaded version with a smaller timeout of 100 ms to gain control about the FIFO loop
    stBufferData.lTimeout =     100;
    if (!stCard.bSetError && !g_bThread)
	{
		stBufferData.bStartExtraDMA = stBufferData.bStartTimestamp;
		vDoMainLoop(&stBufferData, &stWorkData, bWorkInit, bWorkDo, vWorkClose, bKeyAbortCheck);
	}
       


    // ------------------------------------------------------------------------
    // print error information if an error occured
    if (stCard.bSetError)
        return nSpcMErrorMessageStdOut (&stCard, "An error occured while programming the card:\n", true);

    // clean up and close the driver
    vSpcMCloseCard (&stCard);
	delete[]var_value;

    return EXIT_SUCCESS;
    }

