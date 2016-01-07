#ifndef xcvr_h_
#define xcvr_h_

#include <Arduino.h>
#include <si5351.h>
#include <U8glib.h>
#include <TimerOne.h>

#define ENC_DECODER (1 << 2)
#include <ClickEncoder.h>

/**
	Pins used:
 		oled display (SPI):
 			- 13 = SCK
 			- 12 = MOSI 
 			- 11 = MISO
 			- 10 = SS

 		optical encoder
 			- A0 = 
 			- A1 = 
 			- A2 = 

 		IO port expander
 			- A4 = I2C SDA
 			- A5 = I2C SCL

 		Band switching
 			- 8
 			- 7
 			- 6
 			- 5

 		Computer communication (Serial)
 			- 0 = TX
 			- 1 = RX

 		Free pins:
 			- 2 (led)
 			- 3
 			- 4

 */


enum Sideband {
	LSB = 0,
	USB = 1
};

enum XcvrStateFlags {
	FILTER_ON 			= 0x01,
	RIT_ON    			= 0x02,
	VFO_B_ON  			= 0x03,
	PREAMP_ON 			= 0x04,
	ATT_ON				= 0x05,
	EXTERNAL_FILTERS_ON = 0x06
};

typedef struct Filter {
	long long centerFrequency; // in Hz
	short int bandwidth; // in Hz
};

typedef struct Band {
	unsigned short startFrequency; // in KHz
	unsigned short bandLength; // in KHz
	bool isUpperSideband;
};

class Xcvr; // forward

class XcvrUi {
public:
	XcvrUi();
	void init(Xcvr& xcvr);
	void render();
	void update();

	static ClickEncoder *encoder;

 private:
	void draw();
	void renderFrequency();
	void renderRit();

	short int lastEncoderValue, currentEncoderValue;
	short int stepSize = 10;
	char frequencyRepr[11] = {' ', '2', '8', '.', '1', '1', '0', '.', '2', '0', '\0'};
	char ritRepr[6] = {'+', '9', '.', '9', '9', '\0'};
	Xcvr* xcvr;
	U8GLIB_SSD1306_128X64* display; 
};

class Xcvr {
public:
	void init(void);
	void setFilter(unsigned char index);
	void setSideband(Sideband sideband);
	void incrementFrequency(int amount);

	bool hasStatusChanged();
	void clearStatusChange();

	void nextBand();
	unsigned char getBand();
	bool isInExternalBandMode();
	void setExternalBandMode(bool on);

	void ritReset();
	bool isRitOn();
	void setRit(bool on);
	void ritIncrement(short amount);
	short getRitAmount();


	short ritAmount = 0; // delta, in Hz
	long long frequency = 1000000LL; // in Hz

	Filter filters[4];
	unsigned char filterIndex;

	Band bands[10]; // 160, 80, 40, 30, 20, 15, 17, 10
	unsigned char bandIndex; // this one can be merged with filterIndex to save space

	Sideband sideband; // 0 == upper, 1 == lower
	short int cwPitch = 500;

	unsigned char flags;

	long long vfoFrequency; // in Hz, changed when switching encoder or rit
	long long bfoFrequency; // in Hz, changed when switching filters or sideband
	bool statusChanged = false;

	Si5351 si5351;

private:
	void recalculateBfo();
	void setVfoFrequency();
	void setBfoFrequency();
	void switchBandFilters();
	void applyCurrentBandSettings();

};


#endif
