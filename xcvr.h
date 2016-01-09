#ifndef xcvr_h_
#define xcvr_h_

#include <Arduino.h>
#include <si5351.h>
#include <U8glib.h>
#include <TimerOne.h>

#define ENC_DECODER (1 << 2)
#include <ClickEncoder.h>
#include <Bounce2.h>  // https://github.com/thomasfredericks/Bounce2

/**
	Pins used:
 		OLED display (SPI):
 			- 13 = SCK
 			- 12 = MOSI 
 			- 11 = MISO
 			- 10 = SS

 		Optical encoder
 			- A0 = 
 			- A1 = 
 			- A2 = 

 		I2C IO port expander
 			- A4 = I2C SDA
 			- A5 = I2C SCL

 		Band switching
 			- will be done through the I2C port expander (4 bits)

 		Computer communication (Serial)
 			- 0 = TX
 			- 1 = RX

 		Keyer
 			- 2 = TX
 			- 3 = DIT
 			- 4 = DAH
 			- 5 = PTT 
 			- 6 = SIDETONE

 		Free pins:
 			- 7
 			- 8 = change ui mode
 			- 9

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
	byte meters;
	bool isUpperSideband;
};

// TODO Adrian: extract ui and keyer to their own libs
class Xcvr; // forward
class Keyer;

enum UiMode {
	NORMAL = 0,
	SETTING_RIT,
	SETTING_SPEED,
	SETTING_KEYER_MODE,
	SETTING_BAND,
	LAST_MODE 
};

class XcvrUi {
public:
	XcvrUi();
	void init(Xcvr& xcvr, Keyer& keyer);
	void render();
	void update();

	static ClickEncoder *encoder;

 private:
	void draw();
	void renderFrequency();
	void renderRit();

	byte mode = NORMAL;
	short int lastEncoderValue, currentEncoderValue;
	short int stepSize = 10;

	// TODO: since we are not drawing in parallel, perhaps we can use a single buffer for all text renderings
	char frequencyRepr[11] = {' ', '2', '8', '.', '1', '1', '0', '.', '2', '0', '\0'};
	char ritRepr[6] = {'+', '9', '.', '9', '9', '\0'};
	char wpmRepr[7] = {'2', '0', ' ', 'W', 'P', 'M', '\0'};
	char bandRepr[5] = {'1', '6', '0', 'M', '\0'};

	Xcvr* xcvr;
	Keyer* keyer;
	U8GLIB_SSD1306_128X64* display; 
};


class Keyer {
public:
	void init();
	void update();

	void switch_to_tx_silent(byte tx);
	void initialize_keyer_state();
	void initialize_default_modes();
	void initialize_pins();
	void check_paddles();
	void check_dit_paddle();
	void check_dah_paddle();
	void ptt_key();
	void ptt_unkey();
	void check_ptt_tail();
	void send_dit(byte sending_type);
	void send_dah(byte sending_type);
	void tx_and_sidetone_key(int state, byte sending_type);
	void loop_element_lengths(float lengths, float additional_time_ms, int speed_wpm_in, byte sending_type);
	void speed_set(int wpm_set);
	void speed_change(int change);
	void sidetone_adj(int hz);
	void service_dit_dah_buffers();
	int paddle_pin_read(int pin_to_read);
	void boop_beep();
	void beep_boop();
	void boop();
	void beep();

	// Variables and stuff
	struct config_t {  // 23 bytes
		unsigned int wpm;
		byte keyer_mode;
		byte sidetone_mode;
		unsigned int hz_sidetone;
		unsigned int dah_to_dit_ratio;
		byte length_wordspace;
		byte autospace_active;
		byte weighting;
		byte dit_buffer_off;
		byte dah_buffer_off;
	} configuration;



	/** pins **/
	#define tx_key_line_1 2       // (high = key down/tx on)
	#define ptt_tx_1 5              // PTT ("push to talk") lines
	#define paddle_left 3
	#define paddle_right 4
	#define tx_key_dit 0            // if defined, goes high for dit (any transmitter)
	#define tx_key_dah 0            // if defined, goes high for dah (any transmitter)
	#define sidetone_line 6         // connect a speaker for sidetone

	/** config **/

	#define SENDING_NOTHING 0
	#define SENDING_DIT 1
	#define SENDING_DAH 2

	#define STRAIGHT 1
	#define IAMBIC_B 2
	#define IAMBIC_A 3
	#define BUG 4
	#define ULTIMATIC 5

	#define ULTIMATIC_NORMAL 0
	#define ULTIMATIC_DIT_PRIORITY 1
	#define ULTIMATIC_DAH_PRIORITY 2

	#define AUTOMATIC_SENDING 0
	#define MANUAL_SENDING 1

	#define PADDLE_NORMAL 0
	#define PADDLE_REVERSE 1

	#define SIDETONE_OFF 0
	#define SIDETONE_ON 1
	#define SIDETONE_PADDLE_ONLY 2

	#define initial_dah_to_dit_ratio 300     // 300 = 3 / normal 3:1 ratio
	#define default_length_letterspace 3
	#define default_length_wordspace 7
	#define default_weighting 50             // 50 = weighting factor of 1 (normal)
	#define default_keying_compensation 0    // number of milliseconds to extend all dits and dahs - for QSK on boatanchors
	#define default_first_extension_time 0   // number of milliseconds to extend first sent dit or dah
	#define default_ptt_hang_time_wordspace_units 1.0
	#define wpm_limit_low 5
	#define wpm_limit_high 60
	#define hz_high_beep 1500                // frequency in hertz of high beep
	#define hz_low_beep 400                  // frequency in hertz of low beep
	#define initial_speed_wpm 24             // "factory default" keyer speed setting

	byte command_mode_disable_tx = 0;
	byte ptt_tail_time = 10;
	byte ptt_lead_time = 10;
	byte manual_ptt_invoke = 0;
	byte key_tx = 0;         // 0 = tx_key_line control suppressed
	byte dit_buffer = 0;     // used for buffering paddle hits in iambic operation
	byte dah_buffer = 0;     // used for buffering paddle hits in iambic operation
	byte being_sent = 0;     // SENDING_NOTHING, SENDING_DIT, SENDING_DAH
	byte key_state = 0;      // 0 = key up, 1 = key down
	byte config_dirty = 0;
	unsigned long ptt_time = 0; 
	byte ptt_line_activated = 0;
	byte length_letterspace = default_length_letterspace;
	byte keying_compensation = default_keying_compensation;
	byte first_extension_time = default_first_extension_time;
	byte ultimatic_mode = ULTIMATIC_NORMAL;
	float ptt_hang_time_wordspace_units = default_ptt_hang_time_wordspace_units;
	byte last_sending_type = MANUAL_SENDING;
	byte zero = 0;
	byte iambic_flag = 0;
	unsigned long last_config_write = 0;

	#define SIDETONE_HZ_LOW_LIMIT 299
	#define SIDETONE_HZ_HIGH_LIMIT 2001
	#define initial_sidetone_freq 600        // "factory default" sidetone frequency setting
};

class Xcvr {
public:
	void init();
	void setFilter(unsigned char index);
	void setSideband(Sideband sideband);
	void incrementFrequency(int amount);

	bool inline hasStatusChanged() { return statusChanged; }
	void inline clearStatusChange() { statusChanged = false; }

	void nextBand();
	byte inline getBand() { return bandIndex; }

	void ritReset();
	bool isRitOn();
	void setRit(bool on);
	void ritIncrement(short amount);
	short getRitAmount();


	short ritAmount = 0; // delta, in Hz
	long long frequency = 1000000LL; // in Hz

	Filter filters[1];
	unsigned char filterIndex;

	Band bands[9]; // 160, 80, 40, 30, 20, 15, 17, 10, ext
	unsigned char bandIndex; // this one can be merged with filterIndex and status changedto save space

	Sideband sideband; // 0 == upper, 1 == lower
	word cwPitch = 500;

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
