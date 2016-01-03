#ifndef xcvr_h_
#define xcvr_h_

#include <Arduino.h>
#include <si5351.h>

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
	long long startFrequency;
	long long endFrequency;
	bool isUpperSideband;
};

class Xcvr {
public:
	void init(void);
	void setFilter(unsigned char index);
	void setSideband(Sideband sideband);
	void recalculateBfo();
	void incrementFrequency(int amount);
	void setVfoFrequency();
	void setBfoFrequency();

	void nextBand();
	unsigned char getBand();
	bool isInExternalBandMode();
	void setExternalBandMode(bool on);

	void ritReset();
	bool isRitOn();
	void setRit(bool on);
	void ritIncrement(short amount);

	short ritAmount = 0; // delta, in Hz
	long long frequency = 12106900LL; // in Hz

	Filter filters[4];
	unsigned char filterIndex;

	Band bands[8]; // 160, 80, 40, 30, 20, 15, 17, 10
	unsigned char bandIndex; // this one can be merged with filterIndex to save space

	Sideband sideband; // 0 == upper, 1 == lower
	short int cwPitch = 500;

	unsigned char flags;

	long long vfoFrequency; // in Hz, changed when switching encoder or rit
	long long bfoFrequency; // in Hz, changed when switching filters or sideband

	Si5351 si5351;
};


#endif
