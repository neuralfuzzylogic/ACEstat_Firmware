
#include "eis.h"


//EIS GLOBALS
volatile uint8_t dftRdy = 0;
volatile uint32_t ucButtonPress =0;
SNS_CFG_Type * pSnsCfg0;
SNS_CFG_Type * pSnsCfg1;
volatile uint32_t u32AFEDieStaRdy =0;         // Variable used to load AFEDIESTA
float FCW_Val = 0;
const int maxNumFreqs = 80;
int numTestPoints = 0;
ImpResult_t ImpResult[80];
bool EISdebugMode = false;

//END EIS GLOBALS

// On initialization, this function is called to temporarily close SW1 in teh Low Power loop.
// This results in the LPTIA output being shorted to its input.
// For a short duration, the amplifier can handle this
// This greatly speeds up the settling time of the gas sensor.
void ChargeECSensor(void)
{
  pADI_AFE->LPTIASW0 |= 0x2;                   // Close SW1 in LP loop to short LPTIA0 output to inverting input
  delay_10us(400000);                          // delay 4S
  pADI_AFE->LPTIASW0 &= ~(0x2);                // Open SW1 in LP loop to unshort LPTIA0 output to inverting input
}

/**
   @brief uint8_t SnsACInit(uint8_t channel)
          Initialization for AC test, setup wave generation and switches
   @param channel :{CHAN0,CHAN1}
      - 0 or CHAN0, Sensor channel 0
      - 1 or CHAN1, Sensor channel 1
   @return 1.
*/

uint8_t SnsACInit(uint8_t channel)
{
   uint32_t ctia;
   /*DFT interrupt enable*/
   AfeAdcIntCfg(BITM_AFE_ADCINTIEN_DFTRDYIEN);
   NVIC_EnableIRQ(AFE_ADC_IRQn);
   /******setup exitation loop and TIA********/
   AfeHpTiaPwrUp(true);
   AfeHpTiaCon(HPTIABIAS_1V1); /*Normal power mode, 1.1V biased HP TIA*/
   AfeSwitchFullCfg(SWITCH_GROUP_T,SWID_T9);
   ctia = BITM_HPTIA_CTIA_16PF|BITM_HPTIA_CTIA_8PF|BITM_HPTIA_CTIA_4PF| \
            BITM_HPTIA_CTIA_2PF|BITM_HPTIA_CTIA_1PF;
   //AfeHpTiaSeCfg(HPTIASE_RTIA_5K,ctia,0);   /*rtia,ctia,no diosel*/
   AfeHpTiaSeCfg(HPTIASE_RTIA_1K,ctia,0);   /*reduce gain for PGA = 4*/
   AfeHpTiaDeCfg(CHAN0,HPTIADE_RLOAD_OPEN,HPTIADE_RTIA_OPEN);
   AfeHpTiaDeCfg(CHAN1,HPTIADE_RLOAD_OPEN,HPTIADE_RTIA_OPEN);
   /*switch to RCAL, loop exitation before power up*/
   AfeSwitchDPNT(SWID_DR0_RCAL0,SWID_PR0_RCAL0,SWID_NR1_RCAL1,SWID_TR1_RCAL1|SWID_T9);
   /*********Initialize ADC and DFT********/
   /*ADC initialization*/
   AfeAdcFiltCfg(SINC3OSR_5,SINC2OSR_178,LFPBYPEN_NOBYP,ADCSAMPLERATE_800K); //900Hz as default
   AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW50);
   AfeAdcPgaCfg(GNPGA_4,0);
   AfeAdcChan(MUXSELP_HPTIA_P,MUXSELN_HPTIA_N);
   AfeAdcChopEn(1);   //Enable ADC input buffer chop for LP mode (up to 80kHz)
    /********sinewave generation**********/
   AfeHPDacPwrUp(true);
   /*DAC attenuator = 1/5, Excitaion Amplifier Gain=1/4,DAC update rate = 320KHz,bandwidth=50KHz*/
   AfeHPDacCfg(HPDAC_ATTEN_DIV5,HPDAC_RATE_REG,HPDAC_INAMPGAIN_DIV4);
   AfeHPDacSineCfg(SINE_FREQ_REG,0,SINE_OFFSET_REG,SINE_AMPLITUDE_REG);
   AfeHPDacWgType(HPDAC_WGTYPE_SINE);
   return 1;
}

/**
   @brief uint8_t SnsACSigChainCfg(float freq)
         ======== configuration of AC signal chain depends on required excitation frequency.
   @param freq :{}
            - excitation AC signal frequency
   @return 1.
   @note settings including DAC update rate, ADC update rate and DFT samples can be adjusted for
   different excitation frequencies to get better performance. As general guidelines,
       - DAC update rate: make sure at least 4 points per sinewave period. Higher rate comsumes more power.
       - ADC update rate:  at least follow Nyquist sampling rule.
       - DFT samples should cover more than 1 sine wave period. more DFT sample reduce variation but take longer time.
          the configuration can be optimised depending on user's applicationn
*/
uint8_t SnsACSigChainCfg(float freq)
{
   uint16_t DacCon;
   uint32_t WgFreqReg;

   DacCon = pADI_AFE->HSDACCON;
   DacCon &= (~BITM_AFE_HSDACCON_RATE);  //clear rate bits for later setting
  // WgFreqReg = (uint32_t)((((uint64_t)freq)<<30)/16000000.0+0.5);  //ATE version 0x14
   //WgFreqReg = (uint32_t)((((uint64_t)freq)<<26)/16000000.0+0.5); //ATE version less than 0x03
   if (freq < .11){

      ClkDivCfg(1,1);                                   // digital die to 26MHz
      AfeHFOsc32M(0x0);                                 //AFE oscillator change to 16MHz
      AfeSysClkDiv(AFE_SYSCLKDIV_1);                    //AFE system clock remain in 16MHz

      AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW250);
      AfeHpTiaCon(HPTIABIAS_1V1);

      DacCon &= 0xFE01;                                 // Clear DACCON[8:1] bits
      DacCon |= (0x1b<<BITP_AFE_HSDACCON_RATE);         // Set DACCLK to recommended setting for LP mode

      pADI_AFE->AFECON &= (~(BITM_AFE_AFECON_SINC2EN));          // Clear the SINC2 filter to flush its contents
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_SINC2EN;               // re-enable SINC2 filter

      AfeAdcFiltCfg(SINC3OSR_4,
                    SINC2OSR_1067,
                    LFPBYPEN_BYP,
                    ADCSAMPLERATE_800K);                // Configure ADC update = 800KSPS/5 = 200KSPS SINC3 output. 200K/800, SINC2 O/P = 250 SPS

      //DFT source: supply filter output.
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));                     // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN;        // re-enable DFT

      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,
                   DFTNUM_16384,
                   DFTIN_SINC2); // DFT input is from SINC2 filter. 16384 * (1/250) = 65.5 seconds to fill

      FCW_Val = (((freq/16000000)*1073741824)+0.5);
      WgFreqReg = (uint32_t)FCW_Val;
   }

   else if (freq < .51){

      ClkDivCfg(1,1);                       // digital die to 26MHz
      AfeHFOsc32M(0x0);                       //AFE oscillator change to 16MHz
      AfeSysClkDiv(AFE_SYSCLKDIV_1);        //AFE system clock remain in 16MHz

      AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW250);
      AfeHpTiaCon(HPTIABIAS_1V1);


      DacCon &= 0xFE01;                        // Clear DACCON[8:1] bits
      DacCon |= (0x1b<<BITP_AFE_HSDACCON_RATE);        // Set DACCLK to recommended setting for LP mode

      pADI_AFE->AFECON &= (~(BITM_AFE_AFECON_SINC2EN));          // Clear the SINC2 filter to flush its contents
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_SINC2EN;               // re-enable SINC2 filter

      AfeAdcFiltCfg(SINC3OSR_4,
                    SINC2OSR_640,
                    LFPBYPEN_BYP,ADCSAMPLERATE_800K); // Configure ADC update = 800KSPS/5 = 160KSPS SINC3 output. 160K/640, SINC2 O/P = 250 SPS

      //DFT source: supply filter output.
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));            // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN;// re-enable DFT

      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,
                   DFTNUM_8192,
                   DFTIN_SINC2);// DFT input is from SINC2 filter. 2048 * (1/250) = 8.2 seconds to fill

      WgFreqReg = 0x21; //(.5Hz * 2^30)/16MHz = 33 (0x21)
      FCW_Val = (((freq/16000000)*1073741824)+0.5);
      WgFreqReg = (uint32_t)FCW_Val;
   }

   else if(freq<5)
   {
      ClkDivCfg(1,1);                          // digital die to 26MHz
      AfeHFOsc32M(0);                          // AFE oscillator change to 16MHz
      AfeSysClkDiv(AFE_SYSCLKDIV_1);           // AFE system clock remain in 16MHz
      DacCon &= 0xFE01;                        // Clear DACCON[8:1] bits
      DacCon |=
        (0x1b<<BITP_AFE_HSDACCON_RATE);        // Set DACCLK to recommended setting for LP mode
      AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW250);
		AfeHpTiaCon(HPTIABIAS_1V1);

      pADI_AFE->AFECON &= (~(BITM_AFE_AFECON_SINC2EN)); // Clear the SINC2 filter
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_SINC2EN;

      AfeAdcFiltCfg(SINC3OSR_4,
                    SINC2OSR_533,LFPBYPEN_BYP,
                    ADCSAMPLERATE_800K);       // Configure ADC update = 800KSPS/4 = 200KSPS SINC3 output. 200K/533, SINC2 O/P = 375 SPS
      //DFT source: supply filter output.
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));            // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN;// re-enable DFT

      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,  // DFT input is from SINC2 filter. 8192 * (1/375) = 21.83 seconds to fill
                   DFTNUM_8192,
                   DFTIN_SINC2);
      FCW_Val = (((freq/16000000)*1073741824)+0.5);
      WgFreqReg = (uint32_t)FCW_Val;
    }
   else if(freq<450)   /*frequency lower than 450 Hz*/
   {
      ClkDivCfg(1,1);                          // digital die to 26MHz
      AfeHFOsc32M(0);                          // AFE oscillator change to 16MHz
      AfeSysClkDiv(AFE_SYSCLKDIV_1);           // AFE system clock remain in 16MHz

      AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW250);
		AfeHpTiaCon(HPTIABIAS_1V1);
      DacCon &= 0xFE01;                        // Clear DACCON[8:1] bits
      DacCon |=
        (0x1b<<BITP_AFE_HSDACCON_RATE);        // Set DACCLK to recommended setting for LP mode
      /*ADC 900sps update rate to DFT engine*/
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_SINC2EN));          // Clear the SINC2 filter to flush its contents
      delay_10us(50);
      pADI_AFE->AFECON |=
        BITM_AFE_AFECON_SINC2EN;               // re-enable SINC2 filter
      AfeAdcFiltCfg(SINC3OSR_4,
                    SINC2OSR_178,LFPBYPEN_BYP,
                    ADCSAMPLERATE_800K);       // Configure ADC update = 800KSPS/4 = 200KSPS SINC3 output. 200K/178, SINC2 O/P = 1123 SPS
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));            // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN;// re-enable DFT
      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,  // DFT input is from SINC2 filter. 4096 * (1/1123) = 3.64 seconds to fill
                   DFTNUM_4096,
                   DFTIN_SINC2);
      FCW_Val = (((freq/16000000)*1073741824)+0.5);
      WgFreqReg = (uint32_t)FCW_Val;
   }
   else if(freq<80000)  /*450Hz < frequency < 80KHz*/
   {
     ClkDivCfg(1,1);                           // digital die to 26MHz
     AfeHFOsc32M(0);                           // AFE oscillator change to 16MHz
     AfeSysClkDiv(AFE_SYSCLKDIV_1);            // AFE system clock remain in 16MHz
      /*set middle DAC update rate,16MHz/18=~888KHz update rate,skew the DAC and ADC clocks with respect to each other*/
      AfeSysCfg(ENUM_AFE_PMBW_LP,ENUM_AFE_PMBW_BW250);
		AfeHpTiaCon(HPTIABIAS_1V1);
      DacCon &= 0xFE01;                        // Clear DACCON[8:1] bits
      DacCon |=
        (0x1b<<BITP_AFE_HSDACCON_RATE);        // Set DACCLK to recommended setting for LP mode
      /*ADC 160Ksps update rate to DFT engine*/
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_SINC2EN));          // Clear the SINC2 filter to flush its contents
      delay_10us(50);
      pADI_AFE->AFECON |=
        BITM_AFE_AFECON_SINC2EN;               // re-enable SINC2 filter
      AfeAdcFiltCfg(SINC3OSR_4,SINC2OSR_178,
                    LFPBYPEN_BYP,
                    ADCSAMPLERATE_800K);      //bypass LPF, 200KHz ADC update rate
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));            // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN;// re-enable DFT
      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,
                   DFTNUM_16384,
                   DFTIN_SINC3);               //DFT source: Sinc3 result. 16384 * (1/200000) = 81.92mS
     FCW_Val = (((freq/16000000)*1073741824)+0.5);
      WgFreqReg = (uint32_t)FCW_Val;
   }
   else/*80KHz < frequency < 200KHz*/
   {
      /*****boost ADC sample rate to 1.6MHz****/
      AfeAdcChopEn(0);  //Disable ADC input buffer chop for HP mode (>80kHz)
      AfeSysCfg(ENUM_AFE_PMBW_HP,ENUM_AFE_PMBW_BW250);   //set High speed DAC and ADC in high power mode
		 AfeHpTiaCon(HPTIABIAS_1V1);
      ClkDivCfg(2,2);
      AfeSysClkDiv(AFE_SYSCLKDIV_2);   //AFE system clock remain in 8MHz
      AfeHFOsc32M(BITM_AFE_HPOSCCON_CLK32MHZEN);   //AFE oscillator change to 32MHz
      ClkDivCfg(1,1);
      /*set High DAC update rate,16MHz/9=~1.6MHz update rate,skew the DAC and ADC clocks with respect to each other*/
      DacCon &= 0xFE01;                        // Clear DACCON[8:1] bits
      DacCon |=
        (0x07<<BITP_AFE_HSDACCON_RATE);        // Set DACCLK to recommended setting for HP mode
      /*ADC 400Ksps update rate to DFT engine*/
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_SINC2EN));          // Clear the SINC2 filter to flush its contents
      delay_10us(50);
      pADI_AFE->AFECON |=
        BITM_AFE_AFECON_SINC2EN;               // re-enable SINC2 filter
      AfeAdcFiltCfg(SINC3OSR_2,SINC2OSR_178,LFPBYPEN_BYP,ADCSAMPLERATE_1600K); //800KHz ADC update rate
      pADI_AFE->AFECON &=
        (~(BITM_AFE_AFECON_DFTEN));            // Clear DFT enable bit
      delay_10us(50);
      pADI_AFE->AFECON |=
        BITM_AFE_AFECON_DFTEN;                 // re-enable DFT
      AfeAdcDFTCfg(BITM_AFE_DFTCON_HANNINGEN,
                   DFTNUM_16384,DFTIN_SINC3); //DFT source: Sinc3 result 16384 * (1/800000) = 20.48mS
     FCW_Val = (((freq/16000000)*1073741824)+0.5);
     WgFreqReg = (uint32_t)FCW_Val;
   }
   pADI_AFE->HSDACCON = DacCon;
   AfeHPDacSineCfg(WgFreqReg,0,SINE_OFFSET_REG,SINE_AMPLITUDE_REG);  //set new frequency
   return 1;
}

/**
   @brief uint8_t SnsACTest(uint8_t channel)
          start AC test
   @param channel :{CHAN0,CHAN1}
      - 0 or CHAN0, Sensor channel 0
      - 1 or CHAN1, Sensor channel 1
   @param pDFTData :{}
      - pointer to DFT result:6x word
   @return 1.
*/
uint8_t SnsACTest(uint8_t channel)
{
   //uint32_t freqNum = sizeof(ImpResult)/sizeof(ImpResult_t);
   for(uint32_t i=0;i<numTestPoints;i++)
   {
      SnsACSigChainCfg(ImpResult[i].freq);
      pADI_AFE->AFECON &= ~(BITM_AFE_AFECON_WAVEGENEN|BITM_AFE_AFECON_EXBUFEN|   \
                           BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN); //disable loop before switching
      /*********Sensor+Rload AC measurement*************/
      /*break LP TIA connection*/
      AfeLpTiaSwitchCfg(channel,SWMODE_AC);  /*LP TIA disconnect sensor for AC test*/
#if EIS_DCBIAS_EN //add bias voltage to excitation sinewave
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DACBUFEN;   //enable DC buffer for excitation loop
      if(channel>0)
      {
         pADI_AFE->DACDCBUFCON = ENUM_AFE_DACDCBUFCON_CHAN1;   //set DC offset using LP DAC1
      }
      else
      {
         pADI_AFE->DACDCBUFCON = ENUM_AFE_DACDCBUFCON_CHAN0;   //set DC offset using LP DAC0
      }
#endif

      /*switch to sensor+rload*/
      if(channel>0)
      {
         /*disconnect RTIA to avoid RC filter discharge*/
         AfeLpTiaCon(CHAN1,pSnsCfg1->Rload,LPTIA_RGAIN_DISCONNECT,pSnsCfg1->Rfilter);

#if EN_2_LEAD
        AfeSwitchDPNT(SWID_D6_CE1,SWID_P12_CE1,SWID_N7_SE1RLOAD,SWID_T7_SE1RLOAD|SWID_T9);
#else
        AfeSwitchDPNT(SWID_D6_CE1,SWID_P6_RE1,SWID_N7_SE1RLOAD,SWID_T7_SE1RLOAD|SWID_T9);
#endif

      }
      else
      {
         /*disconnect RTIA to avoid RC filter discharge*/
         AfeLpTiaCon(CHAN0,pSnsCfg0->Rload,LPTIA_RGAIN_DISCONNECT,pSnsCfg0->Rfilter);

#if EN_2_LEAD
        AfeSwitchDPNT(SWID_D5_CE0,SWID_P11_CE0,SWID_N5_SE0RLOAD,SWID_T5_SE0RLOAD|SWID_T9);
#else
        AfeSwitchDPNT(SWID_D5_CE0,SWID_P5_RE0,SWID_N5_SE0RLOAD,SWID_T5_SE0RLOAD|SWID_T9);
#endif

      }
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN|BITM_AFE_AFECON_SINC2EN|BITM_AFE_AFECON_WAVEGENEN| \
                           BITM_AFE_AFECON_EXBUFEN|BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN;
      delay_10us(30);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN;
      delay_10us(20);   //200us for switch settling
      /*start ADC conversion and DFT*/
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN|BITM_AFE_AFECON_ADCCONVEN;
      while(!dftRdy)
      {
         PwrCfg(ENUM_PMG_PWRMOD_FLEXI,0,BITM_PMG_SRAMRET_BNK2EN);
      }
      dftRdy = 0;
      ImpResult[i].DFT_result[0] = convertDftToInt(pADI_AFE->DFTREAL);
      ImpResult[i].DFT_result[1] = convertDftToInt(pADI_AFE->DFTIMAG);
      /***************Rload AC measurement*************/
      pADI_AFE->AFECON &= ~(BITM_AFE_AFECON_WAVEGENEN|BITM_AFE_AFECON_EXBUFEN|   \
                           BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN); //disable loop before switching
      if(channel>0)
      {
         /*this stage measures Rload by disconnecting CE1 from D terminal - D terminal connected to Rload directly */
         AfeSwitchDPNT(SWID_D8_WE1,SWID_P9_WE1,SWID_N7_SE1RLOAD,SWID_T7_SE1RLOAD|SWID_T9);
      }
      else
      {
         /*this stage measures Rload by disconnecting CE0 from D terminal - D terminal connected to Rload directly */
         AfeSwitchDPNT(SWID_D7_WE0,SWID_P7_WE0,SWID_N5_SE0RLOAD,SWID_T5_SE0RLOAD|SWID_T9);
      }
      /*switch to rload*/
#if EIS_DCBIAS_EN //add bias voltage to excitation sinewave
      pADI_AFE->AFECON &= ~BITM_AFE_AFECON_DACBUFEN;   //Disable DC buffer for excitation loop
#endif
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN|BITM_AFE_AFECON_SINC2EN|BITM_AFE_AFECON_WAVEGENEN| \
                           BITM_AFE_AFECON_EXBUFEN|BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN;
      delay_10us(30);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN;
      delay_10us(20);   //200us for switch settling
      /*start ADC conversion and DFT*/
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN|BITM_AFE_AFECON_ADCCONVEN;
      while(!dftRdy)
      {
         PwrCfg(ENUM_PMG_PWRMOD_FLEXI,0,BITM_PMG_SRAMRET_BNK2EN);
      }
      dftRdy = 0;
      ImpResult[i].DFT_result[2] = convertDftToInt(pADI_AFE->DFTREAL);
      ImpResult[i].DFT_result[3] = convertDftToInt(pADI_AFE->DFTIMAG   );
      /************RCAL AC measurement***************/
      pADI_AFE->AFECON &= ~(BITM_AFE_AFECON_WAVEGENEN|BITM_AFE_AFECON_EXBUFEN|   \
                           BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN); //disable loop before switching
      /*switch to RCAL, loop exitation before power up*/
      AfeSwitchDPNT(SWID_DR0_RCAL0,SWID_PR0_RCAL0,SWID_NR1_RCAL1,SWID_TR1_RCAL1|SWID_T9);
      AfeLpTiaSwitchCfg(channel,SWMODE_NORM);  //LP TIA normal working mode
      if(channel>0)
      {
         AfeLpTiaCon(CHAN1,pSnsCfg1->Rload,pSnsCfg1->Rtia,pSnsCfg1->Rfilter);//connect RTIA
      }
      else
      {
         AfeLpTiaCon(CHAN0,pSnsCfg0->Rload,pSnsCfg0->Rtia,pSnsCfg0->Rfilter);//connect RTIA
      }
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN|BITM_AFE_AFECON_SINC2EN|BITM_AFE_AFECON_WAVEGENEN| \
                           BITM_AFE_AFECON_EXBUFEN|BITM_AFE_AFECON_INAMPEN|BITM_AFE_AFECON_TIAEN;
      delay_10us(30);
      pADI_AFE->AFECON |= BITM_AFE_AFECON_ADCEN;
      delay_10us(20);   //200us for switch settling
      /*start ADC conversion and DFT*/
      pADI_AFE->AFECON |= BITM_AFE_AFECON_DFTEN|BITM_AFE_AFECON_ADCCONVEN;
      while(!dftRdy)
      {
         PwrCfg(ENUM_PMG_PWRMOD_FLEXI,0,BITM_PMG_SRAMRET_BNK2EN);
      }
      dftRdy = 0;
      ImpResult[i].DFT_result[4] = convertDftToInt(pADI_AFE->DFTREAL);
      ImpResult[i].DFT_result[5] = convertDftToInt(pADI_AFE->DFTIMAG   );
      /**********recover LP TIA connection to maintain sensor*********/
      AfeSwitchDPNT(SWID_ALLOPEN,SWID_ALLOPEN,SWID_ALLOPEN,SWID_ALLOPEN);
      AfeWaveGenGo(false);
   }

   return 1;
}

/**
   @brief uint8_t SnsMagPhaseCal()
          calculate magnitude and phase of sensor
   @param pDFTData : {}
      - input array which stored 6 DFT data
   @param RMag :{}
      - calculated Magnitude of sensor
   @param RPhase :{}
      - calulated Phase of sensor

   @return 1.
*/
uint8_t SnsMagPhaseCal(void)
{
   float Src[8];
   //float Mag[4];
   float Phase[4];
   float Var1,Var2;

   uint32_t testNum = sizeof(ImpResult)/sizeof(ImpResult_t);
   for(uint32_t i=0;i<testNum;i++)
   {
      for (uint8_t ix=0;ix<6;ix++)
      {
         Src[ix] = (float)(ImpResult[i].DFT_result[ix]); // Load DFT Real/Imag results for RCAL, RLOAD, RLOAD+RSENSE into local array for this frequency
      }

      // The impedance engine inside of AD594x give us Real part and Imaginary part of DFT. Due to technology used, the Imaginary
      // part in register is the opposite number. So we add a negative sign on the Imaginary part of results.

      for (uint8_t ix=1;ix<6;ix+=2)
      {
         Src[ix] = - Src[ix];
      }

      Src[6] = (float)(Src[2]-Src[0]);                   // RLoad(real)-RSensor+load(real)
      Src[7] = (float)(Src[3]-Src[1]);                   // RLoad(Imag)-RSensor+load(Imag)

      for (uint8_t ix=0;ix<4;ix++)
      {
         ImpResult[i].DFT_Mag[ix] = Src[ix*2]*Src[ix*2]+Src[ix*2+1]*Src[ix*2+1];
         ImpResult[i].DFT_Mag[ix] = sqrt(ImpResult[i].DFT_Mag[ix]);

         Phase[ix] = atan2(Src[ix*2+1], Src[ix*2]);  // returns value between -pi to +pi (radians) of ATAN2(IMAG/Real)

         // DFT_Mag[0] = Magnitude of Rsensor+Rload
         // DFT_Mag[1] = Magnitude of Rload
         // DFT_Mag[2] = Magnitude of RCAL
         // DFT_Mag[3] = Magnitude of RSENSOR   (RSENSOR-RLOAD)
      }

      // Sensor Magnitude in ohms = (RCAL(ohms)*|Mag(RCAL)|*|Mag(RSensor))
      //                            --------------------------------------
      //                            |Mag(RSensor+Rload)|*|Mag(RLoad))
      Var1 = ImpResult[i].DFT_Mag[2]*ImpResult[i].DFT_Mag[3]*AFE_RCAL; // Mag(RCAL)*Mag(RSENSOR)*RCAL
      Var2 = ImpResult[i].DFT_Mag[0]*ImpResult[i].DFT_Mag[1];          // Mag(RSENSE+LOAD)*Mag(RLOAD)
      Var1 = Var1/Var2;
      ImpResult[i].Mag = Var1;

      // RSensor+Rload Magnitude in ohms =    (RCAL(ohms)*|Mag(RCAL)|*|Mag(Rload))
      //                                       --------------------------------------
      //                                       |Mag(RSensor+Rload)|*|Mag(RSensor+Rload)|
      Var1 = ImpResult[i].DFT_Mag[2]*ImpResult[i].DFT_Mag[0]*AFE_RCAL; // Mag(Rload)*Mag(Rcal)*RCAL
      Var2 = ImpResult[i].DFT_Mag[0]*ImpResult[i].DFT_Mag[0];          // Mag(RSENSE+LOAD)*Mag(RSENSE+LOAD)
      Var1 = Var1/Var2;
      ImpResult[i].RloadMag = (Var1 - ImpResult[i].Mag);               // Magnitude of Rload in ohms

      // Phase calculation for sensor
  //  Var1 = -(Phase[2]+Phase[3]-Phase[1]-Phase[0]); // -((RCAL+RSENSE - RLOAD-RLOADSENSE)
      Var1 = (Phase[2]+Phase[3]-Phase[1]-Phase[0]); // ((RCAL+RSENSE - RLOAD-RLOADSENSE)
      Var1 = Var1*180/PI;                      // Convert radians to degrees.
      /*shift phase back to range (-180,180]*/
      if(Var1 > 180)
      {
         do
         {
            Var1 -= 360;
         }
         while(Var1 > 180);
      }
      else if(Var1 < -180)
      {
         do
         {
            Var1 += 360;
         }
         while(Var1 < -180);
      }
      ImpResult[i].Phase = Var1;

      // Re and Im component of Magnitude
      ImpResult[i].Re_Mag = ImpResult[i].Mag * cos(ImpResult[i].Phase * (PI/180));
      ImpResult[i].Im_Mag = - ImpResult[i].Mag * sin(ImpResult[i].Phase * (PI/180));
   }


   return 1;

}

void runEIS(void){
  printf("[START:EIS]");
  set_adc_mode(1);
      #if EIS_DCBIAS_EN //add bias voltage. Setup for O2 sensor 600mV bias

         //Bias = Vzero - Vbias
         if((pSnsCfg0->Enable == SENSOR_CHANNEL_ENABLE))
         {
            pSnsCfg0->Vzero = 1500;
            pSnsCfg0->Vbias = 900;
         }
         if((pSnsCfg1->Enable == SENSOR_CHANNEL_ENABLE))
         {
           pSnsCfg1->Vzero = 1500;
           pSnsCfg1->Vbias = 900;
         }
    
      #endif

         u32AFEDieStaRdy = AfeDieSta();              // Check if Kernel completed correctly before accessing AFE die
         if ((u32AFEDieStaRdy & 1) == 1)             // Kernel initialization of AFE die was not successful
         {
           UartInit();                               // Initialize UART for 57600-8-N-1
           //printf("AFE DIE Failure" EOL);
           printf("[ERR:AFE DIE Failure]");
           while(u32AFEDieStaRdy == 1)               // AFE die has not initialized correctly.
           {}                                        // trap code here
         }
         AfeWdtGo(false);                            // Turn off AFE watchdog timer for debug purposes
         ClockInit();                                // Init system clock sources
         UartInit();                                 // Init UART for 57600-8-N-1

         pSnsCfg0 = getSnsCfg(CHAN0);
         pSnsCfg1 = getSnsCfg(CHAN1);
         if((pSnsCfg0->Enable == SENSOR_CHANNEL_ENABLE))
         {
            //printf("Sensor Initializing...");
            SnsInit(pSnsCfg0);
            for(uint32_t i=0;i<5000;i++)delay_10us(100);
            //printf("Finish" EOL);
         }
         if((pSnsCfg1->Enable == SENSOR_CHANNEL_ENABLE))
         {
           //printf("%s Sensor Initializing...", pSnsCfg1->SensorName);
            SnsInit(pSnsCfg1);
            for(uint32_t i=0;i<5000;i++)delay_10us(100);
            //printf("Finish" EOL);
         }

         ChargeECSensor();
         //printf("Wait a few moments for results to complete...."EOL);
         //printf("Will take over a minute if 0.1 and 0.5Hz options enabled "EOL);
         //while(1)
         //{
               ucButtonPress = 0;

               SnsACInit(CHAN0);
               SnsACTest(CHAN0);
               SnsMagPhaseCal();   //calculate impedance

               /*power off high power exitation loop if required*/
               AfeAdcIntCfg(NOINT); //disable all ADC interrupts
               NVIC_DisableIRQ(AFE_ADC_IRQn);
               AfeWaveGenGo(false);
               AfeHPDacPwrUp(false);
               AfeHpTiaPwrUp(false);
               delay_10us(300000);
         //}
               printEISResults();
               printf("[END:EIS]");
               NVIC_SystemReset(); //ARM DIGITAL SOFTWARE RESET
}

//Modifies the Impresult array to use N=numFreqs logarithmically spaced frequencies between the values defined by user over UART
//NOTE: Using frequencies below 5Hz cause the test to run extremely slowly, possibly as long as 40 minutes
uint16_t getEISFrequencies(){
  
  uint8_t eisSensChan = getSensorChannel();     //Currently doesn't do anything, need to look more into channel setup for EIS functions first
  
  float startFreq,endFreq;
  uint32_t numPoints;
  //Request user input for frequency range and points-per-decade
    //printf("Lower-bound test frequency(between 1Hz and 250kHzHz) : ");
  printf("[:LBF]");
  startFreq = getParameter(6);
  
  //printf("\nUpper-bound test frequency(between 1Hz and 250kHz) : ");
  printf("[:UBF]");
  endFreq = getParameter(6);
  if(endFreq > 240000){ ///Max frequency is 250kHz
    endFreq = 240000;
  }
  
  //printf(\nFrequency test points per pecade
  printf("[:PPD]");
  int ppd = getParameter(2);
  
  //Calculate the number of frequencies to test, exit test if it exceeds ImpResult max size
  float numDecades = log10(endFreq)-log10(startFreq);
  numPoints = floor(numDecades*ppd);
  if(numPoints > maxNumFreqs){
      printf("[ERR:Frequency vector size(%i) > Maximum available(%i)\nTry reducing points-per-decade]", numPoints, maxNumFreqs);
      return 0;
  }
  numTestPoints = numPoints+1;
  //Populate ImpResult[0:numPoints].freq with log-spaced frequencies
  for(int i=0 ; i<numPoints+1 ; ++i){
    float linVal = 100*i/numPoints;
    float logVal = exp(0.046155*linVal)-1;
    ImpResult[i].freq = startFreq + (int)(logVal*(endFreq-startFreq)/100.0);
    //printf("%f, %f, %f\n", linVal, logVal, ImpResult[i].freq);
  }
  return 1;
}

void printEISResults(void){
  /*print Impedance result*/
  //printf("Impedance Result:\r\n");
  printf("[RESULTS:");
  //printf("Frequencey,RxRload_REAL,RxRload_IMG,Rload_REAL,Rload_IMG,Rcal_REAL,Rcal_IMG,Mag_Rx,Mag_Rload,Mag_Rcal,Mag_Rload-Rx,MAG,PHASE"EOL);
  //printf("Frequency, MAG, PHASE, Re_Mag, Im_Mag "EOL);
  for(uint32_t i=0;i<numTestPoints;i++){
    //phase magnitude is correct, but inverted relative to the palmsens
    printf("%6f,%.4f,%.4f,%.4f,%.4f"EOL,
           ImpResult[i].freq,
           ImpResult[i].Mag,
           (ImpResult[i].Phase*-1),
           ImpResult[i].Re_Mag,
           ImpResult[i].Im_Mag);
  }
  //printf("Test END\r\n");
  printf("]");
}

//void AfeAdc_Int_Handler(void)
//{
//	uint32_t sta;
//	sta = pADI_AFE->ADCINTSTA;
//	if(sta&BITM_AFE_ADCINTSTA_DFTRDY)
//	{
//          pADI_AFE->ADCINTSTA = BITM_AFE_ADCINTSTA_DFTRDY;	//clear interrupt
//          dftRdy = 1;
//          pADI_AFE->AFECON &= (~(BITM_AFE_AFECON_DFTEN|BITM_AFE_AFECON_ADCCONVEN|BITM_AFE_AFECON_ADCEN));  //stop conversion
//	}
//}