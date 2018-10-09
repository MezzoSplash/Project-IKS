/*
 * RTC LCD goed.c
 *
 * Created: 7-6-2018 00:17:32
 * Author : Daan van Heusden
 */ 
#define F_CPU 2000000UL
#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\rtc.h"
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\lcd.h"
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\serialF0.h"
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\nrf24spiXM2.h"
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\nrf24L01.h"
#include "D:\Documenten\Atmel Studio\7.0\RTC LCD goed\twi_master_driver.h"
#define MAXWAARDE_UUR 24
#define MAXWAARDE_MINUUT 60
#define MAXWAARDE_SECONDE 60
#define KNOP_MASK PIN0_bm | PIN1_bm
#define BAUD_100K 100000UL
#define bit_is_clear(sfr,bit) (!(_SFR_BYTE(sfr) & _BV(bit)))
#define DEBOUNCE_PERIODE_ms 20 //tegen contactdender
#define LOCK_PERIOD_MS 200 // om te verkomen dat twee keer iets wordt uitgevoerd terwijl een keer je de knoop indrukt
#define MAX_VALUE 4095 // Maximum geretourneerde waarde ADC
#define MINIMUM 3400 // Minimum geretourneerde waarde lichtmeting
#define MAXIMUM (MINIMUM + 100) // Maximum geretourneerde waarde lichtmeting
#define N 40 // Aantal samples te nemen gedurende 1 lichtmeting
#define E (N/5) // Aantal kleinste en aantal grootste samples te verwijderen voor foutcorrectie
#define UUR_LAAG '5' // uur vanaf wanneer gordijn open moet
#define LUCHTVOCHTIGHEID_LAAG 40 //nog te veranderen naar daadwerkelijke grenswaarden
#define LUCHTVOCHTIGHEID_HOOG 60 //nog te veranderen naar daadwerkelijke grenswaarden
#define GRENS_DONKER_BUITEN 700 //grenswaarde tussen donker en licht buiten
#define TOTAL_DELAY 8
#define SENDRAAM_ITERATION 0
#define SENDLAMP_ITERATION (0.5*TOTAL_DELAY)
#define ADDRESS (0x40)
#define HUM (0xE5)
TWI_Master_t twiMaster;
uint8_t pipe[][6] = {
	"HvA42",//wekker>raam
	"AvH24",//raam>wekker
	"AvH28" //wekker>lamp
};
uint16_t licht_buiten = GRENS_DONKER_BUITEN;
uint8_t received_packet[32];
uint8_t CommandoRaam[2];
uint8_t DataLamp[3];
void sendRaam(uint8_t *data, uint8_t length);
void sendLamp(uint8_t *data, uint8_t length);
void init_nrf()
{
	nrfspiInit();
	nrfBegin();
	nrfSetRetries(NRF_SETUP_ARD_1000US_gc, NRF_SETUP_ARC_8RETRANSMIT_gc);
	nrfSetPALevel(NRF_RF_SETUP_PWR_6DBM_gc);
	nrfSetDataRate(NRF_RF_SETUP_RF_DR_250K_gc);
	nrfSetCRCLength(NRF_CONFIG_CRC_16_gc);
	nrfSetChannel(42);
	nrfSetAutoAck(1);
	nrfEnableDynamicPayloads();
	nrfClearInterruptBits();
	nrfFlushRx();
	nrfFlushTx();
	// Interrupt voor het ontvangen van draadloze berichten
	PORTF.INT0MASK |= PIN6_bm;
	PORTF.PIN6CTRL = PORT_ISC_FALLING_gc;
	PORTF.INTCTRL = (PORTF.INTCTRL & ~PORT_INT0LVL_gm) | PORT_INT0LVL_LO_gc;
	PMIC.CTRL |= PMIC_LOLVLEN_bm;
	//nrfOpenWritingPipe(pipe[0]);
	nrfOpenReadingPipe(0, pipe[0]); // voor acknowledgement
	nrfOpenReadingPipe(1, pipe[1]); // voor ontvangen data
	nrfOpenReadingPipe(2, pipe[2]);
	nrfPowerUp();
}
int select = 0; //drukknoop 1 Selecteren de parameters
int add = 0; //drukknoop 2 geselecteerde parameter verhogen
ISR(PORTA_INT0_vect)
{
	_delay_ms(DEBOUNCE_PERIODE_ms);
	if(bit_is_clear(PORTA.IN, PIN0_bp)) { // als eerste knoop indruken, selecteren parameter of uur of minuut of seconde 
		select++; // parameter 		kiezen
		if(select > 3) select = 0;
		_delay_ms(LOCK_PERIOD_MS);
	}
	else if(bit_is_clear(PORTA.IN, PIN1_bp)) {
		add = 1; //tweede 		drukknoop
		_delay_ms(LOCK_PERIOD_MS);
	}
}
void knoppen_init(void){
	PORTA.DIRCLR |= KNOP_MASK;
	PORTA.INT0MASK |= KNOP_MASK; // interrupt 	0
	//

	PORTCFG.MPCMASK = KNOP_MASK;

	PORTA.PIN0CTRL = PORT_ISC_FALLING_gc|PORT_OPC_PULLUP_gc; // falling	edge met pull up resistors portA pin 0 tot met pin 4
	PORTA.PIN1CTRL = PORT_ISC_FALLING_gc|PORT_OPC_PULLUP_gc;
	PORTA.INTCTRL = PORT_INT0LVL_LO_gc; // interrupt	low level
	PMIC.CTRL = PMIC_LOLVLEN_bm; // set low	level interrupts
	sei(); // enable	interrupts
}
float hum;
float readhumidity(TWI_Master_t *twim)
{
	uint8_t data[1];
	data[0] = HUM;
	TWI_MasterWriteRead(twim, ADDRESS, data, 1, 3);
	while (twim->status != TWIM_STATUS_READY);
	uint16_t h =twim->readData[0];
	h <<= 8;
	h |= twim->readData[1];
	// uint8_t crc = twim->readData[2];
	float hum = h;
	hum *= 125;
	hum /= 65536;
	hum -= 6;
	return hum;
}
uint16_t measure_light();
int numCmp(const void * x, const void * y);
void init_adcs(void);
void adjust_time(char *g);
int main(void)
{
	char t[]="HH:MM:SS";
	TWI_MasterInit(&twiMaster, &TWIE, TWI_MASTER_INTLVL_LO_gc, TWI_BAUD(F_CPU,
	BAUD_100K));
	//i2c_init(&TWIE, TWI_BAUD(F_CPU, BAUD_100K));
	PORTE.DIRSET = PIN1_bm|PIN0_bm; // SDA 0 (D8) SCL 1 (D9)
	PORTE.PIN0CTRL = PORT_OPC_WIREDANDPULL_gc; // Pullup SDA
	PORTE.PIN1CTRL = PORT_OPC_WIREDANDPULL_gc; // Pullup SCL
	PMIC.CTRL |= PMIC_LOLVLEN_bm;
	PORTF.DIRSET = PIN0_bm | PIN1_bm;
	PORTC.DIRSET = PIN0_bm;

	lcd_init();
	knoppen_init();
	init_adcs();
	string_to_rtc_time("08:53:38");
	init_nrf();
	init_stream(F_CPU);
	sei();
	int iter = 0;
	char buffer[20]; //buffer voor opslaan strings lcd
	while(1) {
		// leeg scherm Tera term
		clear_screen();
		//Haal tijd op
		rtc_get_time(&twiMaster);
		rtc_time_to_string(t);
		// als select niet 0 is, begin tijd veranderen
		if(select != 0){
			while(select != 0) {
				adjust_time(t);
			}
			string_to_rtc_time(t);
			rtc_set_time(&twiMaster);
		}
		// lees de luchtvochtigheidssensor
		hum = readhumidity(&twiMaster);
		// hier begint de LCD voor timer klok
		lcd_clear();
		lcd_gotoxy(0,0);
		lcd_puts("TIME:");
		lcd_gotoxy(6,0);
		sprintf(buffer, "%s" ,rtc_time_to_string(t));
		lcd_puts(buffer);
		//hier begint voor hum (luchtvochtigheidssensor)
		sprintf(buffer, "HUM: %2.1f", hum);
		buffer[9] = 0x25;
		buffer[10] = '\0';
		lcd_gotoxy(0,1);
		lcd_puts(buffer);
		//_delay_ms(1000);
		/* Als het later dan 7 (UUR_LAAG) uur in de ochtend is en het licht is
		buiten, zet commando gordijn openen hoog
		Anders, zet commando gordijn open laag (sluiten) */
		if(((t[0] > '0' || t[1] >= UUR_LAAG) && (t[0] < '2')) && licht_buiten> GRENS_DONKER_BUITEN) CommandoRaam[0] = 1;
		else CommandoRaam[0] = 0;
		/* Als luchtvochtigheid buiten bereik valt, open raam. */
		if(hum < LUCHTVOCHTIGHEID_LAAG || hum > LUCHTVOCHTIGHEID_HOOG)
		CommandoRaam[1] = 1;
		else CommandoRaam[1] = 0;
		// Stuur commandos voor raam via RF Als iter gelijk aan 0
		if(iter == SENDRAAM_ITERATION) {sendRaam(CommandoRaam, 2);printf("%d\t%d\n", licht_buiten, CommandoRaam[0]);}
		else _delay_ms(50);
		PORTF.OUTTGL = PIN1_bm;
		/* meet lokaal het licht en stop hoge byte packet[0] en lage byte in
		packet[1] */
		uint16_t light = measure_light();
		DataLamp[0] = (uint8_t) (light >> 8);
		DataLamp[1] = (uint8_t) (light & 0x00FF);
		/* t[0] en t[1] samen vormen de uren
		(t[0] > '1' || t[1] >= '8') betekent eerste karakter is groter dan
		1 of 2de karakter is groter of gelijk aan 8, dus na 18 uur.
		(t[0] == '0' && t[1] <= '6') betekent eerste karakter is gelijk
		aan 0 en 2de karakter is kleiner of gelijk aan 6, dus voor 06
		uur.
		In andere woorden, deze statement is waar na 18 uur en v??r 6 uur.
		DataLamp[2] = 1 betekent blauw licht dimmen, DataLamp[2] = 0
		betekent blauw licht vol */
		if((t[0] > '1' || t[1] >= '7') || (t[0] == '0' && t[1] <= '5'))
		DataLamp[2] = 0x0F;
		else DataLamp[2] = 0xF0;
		// als iter gelijk aan 4 is, dan send datallamp
		if(iter == SENDLAMP_ITERATION) sendLamp(DataLamp, 3);
		else _delay_ms(50);
		// een extra delay van 180 ms voor hoofdprogramma
		for(int i = 0; i < 180; i++) _delay_ms(1);
		// tel iter een op , als iter gelijk is aan totaal delay dan iter is 		0... iter is hij stuurd data bij iedere 8e keer
		if(iter++ == TOTAL_DELAY) iter = 0;
	}
}
void sendRaam(uint8_t *data, uint8_t length)
{
	nrfStopListening();
	_delay_ms(5);
	nrfOpenWritingPipe(pipe[0]); // open weer pipe van raam
	_delay_ms(10);
	nrfWrite((uint8_t *) data, length);
	_delay_ms(10);
	nrfStartListening();
	_delay_ms(5);
}
void sendLamp(uint8_t *data, uint8_t length)
{
	nrfStopListening();
	_delay_ms(5);
	nrfOpenWritingPipe(pipe[2]); // open weer pipe van raam
	_delay_ms(10);
	nrfWrite((uint8_t *) data, length);
	_delay_ms(10);
	nrfStartListening();
	_delay_ms(5);
}
ISR(PORTF_INT0_vect)
{
	uint8_t tx_ds, max_rt, rx_dr; //Parameters. TX_DS: Send was succesful;	MAX_RT: The send failed, too many retries; RX_DR: There is a message	waiting. Deze parameters kunnen 0 of 1, onwaar of waar zijn.
	uint8_t packet_length; //Unsigned integer om de lengte van het pakket in	aantallen bytes in op te slaan.
	nrfWhatHappened(&tx_ds, &max_rt, &rx_dr);
	if ( rx_dr ) {
		packet_length = nrfGetDynamicPayloadSize(); //get packet length in		number of bytes
		nrfRead(received_packet, packet_length); //buffer 'packet', length		'packet_length'. Read the packet, with "packet_length" number of		bytes.
		PORTC.OUTTGL = PIN0_bm;
		/* update waarde van licht_buiten */
		licht_buiten = (uint16_t) (((uint16_t) received_packet[0] << 8) |
		received_packet[1]);
		//receivedDataFlag = 1;
	}
	nrfFlushRx();
	nrfFlushTx();
}
void adjust_time(char *g)
{
	int static i = 1;
	char buf[20];
	if(add != 0) {
		if(select == 1) {
			PORTF.OUTTGL = PIN0_bm;
			if(g[1] == 0x39) {
				g[0]++;
				g[1] = 0x30;
			}
			else if(g[0] == 0x32 && g[1] == 0x34) { //0x32 "2"				00110010 and 0x34 "4" 00110100 dus MAXWAARDEUUR 24
				g[0] = 0x30;
				g[1] = 0x30;
			}
			else g[1]++;
		}
		else if(select == 2){
			if(g[4] == 0x39) {
				g[3]++;
				g[4] = 0x30;
			}
			else if(g[3] == 0x36 && g[4] == 0x30) { //0x32 "6"				00110110 and 0x34 "0" 00110000 dus MAXWAARDEUUR 60
				g[3] = 0x30;
				g[4] = 0x30;
			}
			else g[4]++;
		}
		else if(select == 3){
			if(g[7] == 0x39) {
				g[6]++;
				g[7] = 0x30;
			}
			else if(g[6] == 0x36 && g[7] == 0x30) { //0x36 "6"				00110110 and 0x30 "0" 00110000 dus MAXWAARDEUUR 60
				g[6] = 0x30;
				g[7] = 0x30;
			}
			else g[7]++;
		}
		add = 0;
	}
	if(i == 1) {
		lcd_clear();
		lcd_gotoxy(0,0);
		lcd_puts("TIME");
		sprintf(buf, "%s" , g);
		if(select == 1) {
			buf[0] = ' ';
			buf[1] = ' ';
		}
		if(select == 2) {
			buf[3] = ' ';
			buf[4] = ' ';
		}
		if(select == 3) {
			buf[6] = ' ';
			buf[7] = ' ';
		}
		lcd_gotoxy(6,0);
		lcd_puts(buf);
		lcd_gotoxy(0,1);
		lcd_puts("CHANGE TIME");
	}
	if(i == 25) {
		lcd_clear();
		lcd_gotoxy(0,0);
		lcd_puts("TIME");
		sprintf(buf, "%s" , g);
		lcd_gotoxy(6,0);
		lcd_puts(buf);
		lcd_gotoxy(0,1);
		lcd_puts("CHANGE TIME");
	}
	i++;
	if(i == 50) i = 1;
	_delay_ms(20);
}
void init_adcs(void)
{
	/* Initialiseer ADCs.
	CH0 is input fotodiode, CH1 is potmeter. */
	PORTB.DIRCLR = PIN0_bm|PIN1_bm; // PB0 for reference 	voltage
	ADCB.CH0.MUXCTRL = ADC_CH_MUXPOS_PIN1_gc; // PB1 to + 	channel 0
	ADCB.CH0.CTRL = ADC_CH_INPUTMODE_SINGLEENDED_gc; // channel 0	SINGLE
	ADCB.REFCTRL = ADC_REFSEL_AREFB_gc;
	ADCB.CTRLB = ADC_RESOLUTION_12BIT_gc;
	ADCB.PRESCALER = ADC_PRESCALER_DIV16_gc;
	ADCB.CTRLA = ADC_ENABLE_bm;
}
/* Functie voor het uitvoeren lichtmetingen,
foutcorrectie toepassen en gemiddelde nemen.
Het gemiddelde wordt geretourneerd. */
uint16_t measure_light()
{
	uint16_t res[N];
	int32_t sum = 0;
	uint16_t avg;
for (int i = 0; i<N; i++)
{
	ADCB.CH0.CTRL |= ADC_CH_START_bm;
	while ( !(ADCB.CH0.INTFLAGS & ADC_CH_CHIF_bm) ) ;
	res[i] = ADCB.CH0.RES;
	ADCB.CH0.INTFLAGS |= ADC_CH_CHIF_bm;
}
qsort(res, N, sizeof(int16_t), numCmp);
for(int i = E; i < (N-E); i++) sum += res[i];
avg = sum / (N-(2*E));
return avg;
}
/* Kleine functie voor vergelijken grootten getallen. */
int numCmp(const void * x, const void * y)
{
	int a = *(int*)x;
	int b = *(int*)y;
	if(a > b) return 1;
	else if (a < b) return -1;
	else return 0;
}
ISR(TWIE_TWIM_vect)
{
	TWI_MasterInterruptHandler(&twiMaster);
}
