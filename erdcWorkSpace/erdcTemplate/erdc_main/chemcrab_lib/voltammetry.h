#ifndef voltammetry_H
#define voltammetry_H

#include "ADuCM355.h"
#include "ClkLib.h"
#include "UrtLib.h"
#include "GptLib.h"
#include "AfeAdcLib.h"
#include "AfeWdtLib.h"
#include "DioLib.h"
#include "RstLib.h"
#include "AfeTiaLib.h"
#include "PwrLib.h"
#include "AfeDacLib.h"
#include <stdio.h>
#include <math.h>

#include "craabUtil.h"

/*Imax = 0.9V/RGAIN*/
#define mvStepDelay 147 //delay for 5mV/S

//Retrieves voltammetry parameters from UART input
uint16_t getParameter(int dec);

//Cyclic Voltammetry Functions
void runCV(void);
void cv_ramp_parameters(uint16_t zeroV, uint16_t startV, uint16_t midV, uint16_t endV, uint32_t RGAIN);
void printCVResults(float cZero, float cStart, float cMid, float cEnd, int samplesCount, int RTIA);

//Square-wave Voltammetry Functions
void runSWV(void);
void sqv_dep_time(uint16_t start, uint16_t time);
void sqv_ramp_parameters(uint16_t zeroV, uint16_t startV, uint16_t endV, uint32_t RGAIN, uint16_t amplitude, int dep);
void printSWVResults(float cZero, float cStart, float cEnd, uint16_t amp, int sampleCount, int RTIA, int dep);


#endif
