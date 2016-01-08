#include <xcvr.h>

XcvrUi::XcvrUi() {
}

ClickEncoder* XcvrUi::encoder;
void timerIsr() {
    XcvrUi::encoder->service();
}

Bounce modeDebouncer = Bounce();

void XcvrUi::init(Xcvr& xcvr, Keyer& keyer) {
    this->xcvr = &xcvr;
    this->keyer = &keyer;
    display = new U8GLIB_SSD1306_128X64(13, 12, 0, 11, 10); // CS is not used
    encoder = new ClickEncoder(A1, A0, A2);

    Timer1.initialize(1000);
    Timer1.attachInterrupt(timerIsr);

    // initialize UI mode button
    pinMode(8, INPUT);
    digitalWrite(8, HIGH); // enable pull-up
    modeDebouncer.attach(8);
    modeDebouncer.interval(5);
}

void XcvrUi::update() {

    // check if button state changed
    if (modeDebouncer.update()) {
        if (modeDebouncer.read() == LOW) {
            mode = mode == NORMAL ? SETTING_SPEED : NORMAL;
            Serial.println("button on");
        }
    }


    currentEncoderValue += encoder->getValue();
    bool encoderChanged = currentEncoderValue != lastEncoderValue;

    if (encoderChanged) {
        lastEncoderValue = currentEncoderValue;
        int amountToAdd = (currentEncoderValue / 3);
        if (amountToAdd != 0) {
            currentEncoderValue -= amountToAdd * 3;

            switch (mode) {
                case NORMAL:
                    if (xcvr->isRitOn()) {
                        xcvr->ritIncrement(amountToAdd * 10);
                    } else {
                        xcvr->incrementFrequency(amountToAdd * stepSize);
                    }
                    break;
                case SETTING_SPEED:
                    keyer->speed_change(amountToAdd);
                    break;
                default:
                    break;
            }

        }
    }

    ClickEncoder::Button encoderButtonState = encoder->getButton();
    if (encoderButtonState == ClickEncoder::Clicked) {
        xcvr->nextBand();
    } else if (encoderButtonState == ClickEncoder::Held) {
        xcvr->setRit(!xcvr->isRitOn());
        render();
        // wait until button is released
        while (encoder->getButton() != ClickEncoder::Released);
    }


    if (xcvr->hasStatusChanged() || keyer->config_dirty) {
        render();
        xcvr->clearStatusChange();
        keyer->config_dirty = 0;
    }
}

void XcvrUi::render() {
    display->firstPage();
    do {
        draw();
    } while (display->nextPage());
}

void XcvrUi::draw() {
    renderFrequency();
    renderRit();

    if (xcvr->isRitOn()) {
        display->setFont(u8g_font_helvB08);
        display->drawStr(90, 10, ritRepr);
    }

    display->setFont(u8g_font_8x13B);
    display->drawStr(0, 10, frequencyRepr);

    wpmRepr[4] = (keyer->configuration.wpm > 9) ? (keyer->configuration.wpm / 10) + '0' : ' ';
    wpmRepr[5] = (char)(keyer->configuration.wpm % 10) + '0';
    display->drawStr(0, 30, wpmRepr);
}

void XcvrUi::renderFrequency() {
    long long& f = xcvr->frequency;

    frequencyRepr[2] = f >= 1000000 ? '.' : ' ';

    unsigned char unit = 0;
    bool hasHundreds = false;

    unit = f / 100000000LL;
    if (unit > 0) {
        frequencyRepr[0] = '0' + unit;
        hasHundreds = true;
    } else {
        frequencyRepr[0] = ' ';
    }

    unit = (f / 10000000LL) % 10;
    if (unit > 0 || hasHundreds) {
        frequencyRepr[1] = '0' + unit;
    } else {
        frequencyRepr[1] = ' ';
    }

    unit = (f / 1000000LL) % 10;
    frequencyRepr[2] = '0' + unit;

    unit = (f / 100000LL) % 10;
    frequencyRepr[4] = '0' + unit;

    unit = (f / 10000LL) % 10;
    frequencyRepr[5] = '0' + unit;

    unit = (f / 1000LL) % 10;
    frequencyRepr[6] = '0' + unit;

    unit = (f / 100LL) % 10;
    frequencyRepr[8] = '0' + unit;

    unit = (f / 10LL) % 10;
    frequencyRepr[9] = '0' + unit;
}

void XcvrUi::renderRit() {
    short r = xcvr->getRitAmount();
    short absR = abs(r);

    ritRepr[0] = r < 0 ? '-' : '+';

    unsigned char unit = 0;
    unit = (absR / 1000) % 10;
    ritRepr[1] = '0' + unit;
    unit = (absR / 100) % 10;
    ritRepr[3] = '0' + unit;
    unit = (absR / 10) % 10;
    ritRepr[4] = '0' + unit;
}


// --------------------------------------------

void Xcvr::init(void) {
    // initialize synthesizer
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0);

    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);

    si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);  

    // set band settings
    bands[0].startFrequency = 1800;
    bands[1].startFrequency = 3500;
    bands[2].startFrequency = 7000;
    bands[3].startFrequency = 10000;
    bands[4].startFrequency = 14000;
    bands[5].startFrequency = 18000;
    bands[6].startFrequency = 21000;
    bands[7].startFrequency = 24000;

    applyCurrentBandSettings();
}

bool Xcvr::hasStatusChanged() {
    return statusChanged;
}
void Xcvr::clearStatusChange() {
    statusChanged = false;
}


void Xcvr::setFilter(unsigned char index) {
    this->filterIndex = index;
    recalculateBfo();
    setBfoFrequency();
}

void Xcvr::setSideband(Sideband sideband) {
    this->sideband = sideband;
    recalculateBfo();
    setBfoFrequency();
}

// -----------------------------------------------------------------------------

void Xcvr::nextBand() {
    bandIndex++;
    bandIndex %= 10; // only 10 bands, starting from 0
    applyCurrentBandSettings();
}

void Xcvr::applyCurrentBandSettings() {
    frequency = bands[bandIndex].startFrequency * 1000LL;
    vfoFrequency = frequency - filters[filterIndex].centerFrequency;
    setSideband(bands[bandIndex].isUpperSideband ? USB : LSB);
    ritReset();
    setVfoFrequency();
    switchBandFilters();
}

void Xcvr::switchBandFilters() {
    unsigned char value = 0;
    if (!isInExternalBandMode()) {
        value = bandIndex + 1;
    }
    // TODO: use a port expander for these!
    // digitalWrite(5, (value & 1) == 1 ? HIGH : LOW);
    // digitalWrite(6, (value & 2) == 1 ? HIGH : LOW);
    // digitalWrite(7, (value & 4) == 1 ? HIGH : LOW);
    // digitalWrite(8, (value & 8) == 1 ? HIGH : LOW);
}


unsigned char Xcvr::getBand() {
    return bandIndex;
}

void Xcvr::setExternalBandMode(bool on) {
    if (on)
        this->flags |= EXTERNAL_FILTERS_ON;
    else
        this->flags &= ~EXTERNAL_FILTERS_ON;

    statusChanged = true;
}

bool Xcvr::isInExternalBandMode() {
    return (this->flags & EXTERNAL_FILTERS_ON) == EXTERNAL_FILTERS_ON;
}

// -----------------------------------------------------------------------------

void Xcvr::ritReset() {
    ritAmount = 0;
}

bool Xcvr::isRitOn() {
    return (this->flags & RIT_ON) == RIT_ON;
}

void Xcvr::setRit(bool on) {
    if (on)
        this->flags |= RIT_ON;
    else
        this->flags &= ~RIT_ON;

    setVfoFrequency();
}

void Xcvr::ritIncrement(short amount) {
    this->ritAmount += amount;
    setVfoFrequency();
}

short Xcvr::getRitAmount() {
    return this->ritAmount;
}

// -----------------------------------------------------------------------------

void Xcvr::recalculateBfo() {
    if (sideband == USB)
        bfoFrequency = filters[filterIndex].centerFrequency + cwPitch;
    else
        bfoFrequency = filters[filterIndex].centerFrequency - cwPitch;

    // correct BFO if needed so that we don't get both sidebands of the signal
    if (filters[filterIndex].bandwidth / 2 > cwPitch) {
        short int amountToCorrect = filters[filterIndex].bandwidth / 2 - cwPitch;
        if (sideband == USB)
            bfoFrequency += amountToCorrect;
        else
            bfoFrequency -= amountToCorrect;
    }
}

void Xcvr::incrementFrequency(int amount) {
    vfoFrequency += amount;
    frequency += amount;
    setVfoFrequency();
}

void Xcvr::setVfoFrequency() {
    // TODO: make a distinction between RX and TX
    si5351.set_freq(vfoFrequency + isRitOn() ? ritAmount : 0, 0ULL, SI5351_CLK0);
    statusChanged = true;
}

void Xcvr::setBfoFrequency() {
    si5351.set_freq(bfoFrequency, 0ULL, SI5351_CLK2);
    statusChanged = true;
}


// ----------------------------------------------------------------------------------



void Keyer::initialize_keyer_state() {
  
  key_state = 0;
  key_tx = 1;
  configuration.wpm = initial_speed_wpm;

  configuration.hz_sidetone = initial_sidetone_freq;
  configuration.dah_to_dit_ratio = initial_dah_to_dit_ratio;
  //configuration.current_tx = 1;
  configuration.length_wordspace = default_length_wordspace;
  configuration.weighting = default_weighting;
  
  switch_to_tx_silent(1);
}  


//---------------------------------------------------------------------   

void Keyer::initialize_default_modes() {
  // setup default modes
  configuration.paddle_mode = PADDLE_NORMAL;
  configuration.keyer_mode = IAMBIC_B;
  configuration.sidetone_mode = SIDETONE_ON;
}  

void Keyer::initialize_pins() {
  pinMode (paddle_left, INPUT);
  digitalWrite (paddle_left, HIGH);

  pinMode (paddle_right, INPUT);
  digitalWrite (paddle_right, HIGH);

  if (ptt_tx_1) {
    pinMode (ptt_tx_1, OUTPUT);
    digitalWrite (ptt_tx_1, LOW);
  }

  if (tx_key_line_1) {
    pinMode (tx_key_line_1, OUTPUT);
    digitalWrite (tx_key_line_1, LOW);
  }

  pinMode(sidetone_line, OUTPUT);
  digitalWrite(sidetone_line, LOW);

  if (tx_key_dit) {
    pinMode(tx_key_dit, OUTPUT);
    digitalWrite(tx_key_dit, LOW);
  }
  if (tx_key_dah) {
    pinMode(tx_key_dah, OUTPUT);
    digitalWrite(tx_key_dah, LOW);
  }
}



// Subroutines --------------------------------------------------------------------------------------------

void Keyer::check_paddles() {
  
  #define NO_CLOSURE 0
  #define DIT_CLOSURE_DAH_OFF 1
  #define DAH_CLOSURE_DIT_OFF 2
  #define DIT_CLOSURE_DAH_ON 3
  #define DAH_CLOSURE_DIT_ON 4

  static byte last_closure = NO_CLOSURE;

  check_dit_paddle();
  check_dah_paddle();

  if (configuration.keyer_mode == ULTIMATIC) {
    if (ultimatic_mode == ULTIMATIC_NORMAL) {
      switch (last_closure) {
        case DIT_CLOSURE_DAH_OFF:
          if (dah_buffer) {
            if (dit_buffer) {
              last_closure = DAH_CLOSURE_DIT_ON;
              dit_buffer = 0;
            } else {
              last_closure = DAH_CLOSURE_DIT_OFF;
            }
          } else {
            if (!dit_buffer) {
              last_closure = NO_CLOSURE;
            }
          }
          break;
        case DIT_CLOSURE_DAH_ON:
          if (dit_buffer) {
            if (dah_buffer) {
              dah_buffer = 0;
            } else {
              last_closure = DIT_CLOSURE_DAH_OFF;
            }
          } else {
            if (dah_buffer) {
              last_closure = DAH_CLOSURE_DIT_OFF;
            } else {
              last_closure = NO_CLOSURE;
            }
          }
          break;

        case DAH_CLOSURE_DIT_OFF:
          if (dit_buffer) {
            if (dah_buffer) {
              last_closure = DIT_CLOSURE_DAH_ON;
              dah_buffer = 0;
            } else {
              last_closure = DIT_CLOSURE_DAH_OFF;
            }
          } else {
            if (!dah_buffer) {
              last_closure = NO_CLOSURE;
            }
          }
          break;

        case DAH_CLOSURE_DIT_ON:
          if (dah_buffer) {
            if (dit_buffer) {
              dit_buffer = 0;
            } else {
              last_closure = DAH_CLOSURE_DIT_OFF;
            }
          } else {
            if (dit_buffer) {
              last_closure = DIT_CLOSURE_DAH_OFF;
            } else {
              last_closure = NO_CLOSURE;
            }
          }
          break;

        case NO_CLOSURE:
          if ((dit_buffer) && (!dah_buffer)) {
            last_closure = DIT_CLOSURE_DAH_OFF;
          } else {
            if ((dah_buffer) && (!dit_buffer)) {
              last_closure = DAH_CLOSURE_DIT_OFF;
            } else {
              if ((dit_buffer) && (dah_buffer)) {
                // need to handle dit/dah priority here
                last_closure = DIT_CLOSURE_DAH_ON;
                dah_buffer = 0;
              }
            }
          }
          break;
      }
    } else {
     if ((dit_buffer) && (dah_buffer)) {   // dit or dah priority mode
       if (ultimatic_mode == ULTIMATIC_DIT_PRIORITY) {
         dah_buffer = 0;
       } else {
         dit_buffer = 0;
       }
     }
    }
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::ptt_key() {
  if (ptt_line_activated == 0) {   // if PTT is currently deactivated, bring it up and insert PTT lead time delay
    if (configuration.current_ptt_line) {
      digitalWrite (configuration.current_ptt_line, HIGH);    
      delay(ptt_lead_time[configuration.current_tx-1]);
    }
    ptt_line_activated = 1;
  }
  ptt_time = millis();
}

//-------------------------------------------------------------------------------------------------------
void Keyer::ptt_unkey() {
  if (ptt_line_activated) {
    if (configuration.current_ptt_line) {
      digitalWrite (configuration.current_ptt_line, LOW);
    }
    ptt_line_activated = 0;
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::check_ptt_tail() {

  if (key_state) {
    ptt_time = millis();
  } else {
    if (ptt_line_activated && manual_ptt_invoke == 0) {
      //if ((millis() - ptt_time) > ptt_tail_time) {
      if (last_sending_type == MANUAL_SENDING) {
        #ifndef OPTION_INCLUDE_PTT_TAIL_FOR_MANUAL_SENDING
        if ((millis() - ptt_time) >= ((configuration.length_wordspace*ptt_hang_time_wordspace_units)*float(1200/configuration.wpm)) ) {
          ptt_unkey();
        }          
        #else //ndef OPTION_INCLUDE_PTT_TAIL_FOR_MANUAL_SENDING
        #ifndef OPTION_EXCLUDE_PTT_HANG_TIME_FOR_MANUAL_SENDING
        if ((millis() - ptt_time) >= (((configuration.length_wordspace*ptt_hang_time_wordspace_units)*float(1200/configuration.wpm))+ptt_tail_time[configuration.current_tx-1])) {       
          ptt_unkey();
        }
        #else //OPTION_EXCLUDE_PTT_HANG_TIME_FOR_MANUAL_SENDING
        if ((millis() - ptt_time) >= ptt_tail_time[configuration.current_tx-1]) {       
          ptt_unkey();
        }
        #endif //OPTION_EXCLUDE_PTT_HANG_TIME_FOR_MANUAL_SENDING
        #endif //ndef OPTION_INCLUDE_PTT_TAIL_FOR_MANUAL_SENDING
      } else {
        if ((millis() - ptt_time) > ptt_tail_time[configuration.current_tx-1]) {
          #ifdef OPTION_KEEP_PTT_KEYED_WHEN_CHARS_BUFFERED
          if (!send_buffer_bytes){
            ptt_unkey();
          }
          #else
          ptt_unkey();
          #endif //OPTION_KEEP_PTT_KEYED_WHEN_CHARS_BUFFERED
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::check_dit_paddle() {
  byte pin_value = 0;
  byte dit_paddle = 0;
  static byte memory_rpt_interrupt_flag = 0;

  if (configuration.paddle_mode == PADDLE_NORMAL) {
    dit_paddle = paddle_left;
  } else {
    dit_paddle = paddle_right;
  }

  pin_value = paddle_pin_read(dit_paddle);
  if (pin_value == 0) {
    dit_buffer = 1;
    manual_ptt_invoke = 0;
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::check_dah_paddle() {
  byte pin_value = 0;
  byte dah_paddle;

  if (configuration.paddle_mode == PADDLE_NORMAL) {
    dah_paddle = paddle_right;
  } else {
    dah_paddle = paddle_left;
  }

  pin_value = paddle_pin_read(dah_paddle);

  if (pin_value == 0) {
    dah_buffer = 1;
    manual_ptt_invoke = 0;
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::send_dit(byte sending_type) {

  // notes: key_compensation is a straight x mS lengthening or shortening of the key down time
  //        weighting is

  unsigned int character_wpm = configuration.wpm;

  being_sent = SENDING_DIT;
  tx_and_sidetone_key(1,sending_type);
  if ((tx_key_dit) && (key_tx)) {digitalWrite(tx_key_dit,HIGH);}

  loop_element_lengths((1.0*(float(configuration.weighting)/50)),keying_compensation,character_wpm,sending_type);
  
  if ((tx_key_dit) && (key_tx)) {digitalWrite(tx_key_dit,LOW);}
  tx_and_sidetone_key(0,sending_type);

  loop_element_lengths((2.0-(float(configuration.weighting)/50)),(-1.0*keying_compensation),character_wpm,sending_type);

  // autospace
  if ((sending_type == MANUAL_SENDING) && (configuration.autospace_active)) {
    check_paddles();
  }
  if ((sending_type == MANUAL_SENDING) && (configuration.autospace_active) && (dit_buffer == 0) && (dah_buffer == 0)) {
    loop_element_lengths(2,0,configuration.wpm,sending_type);
  }

  being_sent = SENDING_NOTHING;
  last_sending_type = sending_type;
  
  check_paddles();
}

//-------------------------------------------------------------------------------------------------------

void Keyer::send_dah(byte sending_type) {
  unsigned int character_wpm = configuration.wpm;

  being_sent = SENDING_DAH;
  tx_and_sidetone_key(1,sending_type);
  if ((tx_key_dah) && (key_tx)) {digitalWrite(tx_key_dah,HIGH);}

  loop_element_lengths((float(configuration.dah_to_dit_ratio/100.0)*(float(configuration.weighting)/50)),keying_compensation,character_wpm,sending_type);

  if ((tx_key_dah) && (key_tx)) {digitalWrite(tx_key_dah,LOW);}

  tx_and_sidetone_key(0,sending_type);

  loop_element_lengths((4.0-(3.0*(float(configuration.weighting)/50))),(-1.0*keying_compensation),character_wpm,sending_type);

  // autospace
  if ((sending_type == MANUAL_SENDING) && (configuration.autospace_active)) {
    check_paddles();
  }
  if ((sending_type == MANUAL_SENDING) && (configuration.autospace_active) && (dit_buffer == 0) && (dah_buffer == 0)) {
    loop_element_lengths(2,0,configuration.wpm,sending_type);
  }

//  if ((keyer_mode == IAMBIC_A) && (iambic_flag)) {
//    iambic_flag = 0;
//    //dit_buffer = 0;
//    dah_buffer = 0;
//  }

  check_paddles();

  being_sent = SENDING_NOTHING;
  last_sending_type = sending_type;
}

//-------------------------------------------------------------------------------------------------------

void Keyer::tx_and_sidetone_key(int state, byte sending_type) {

  if ((state) && (key_state == 0)) {
    if (key_tx) {
      byte previous_ptt_line_activated = ptt_line_activated;
      ptt_key();
      if (current_tx_key_line) {digitalWrite (current_tx_key_line, HIGH);}
      if ((first_extension_time) && (previous_ptt_line_activated == 0)) {
        delay(first_extension_time);
      }
    }

    if (configuration.sidetone_mode == SIDETONE_ON || configuration.sidetone_mode == SIDETONE_PADDLE_ONLY) {
      tone(sidetone_line, configuration.hz_sidetone);
    }
    key_state = 1;
  } else {
    if ((state == 0) && (key_state)) {
      if (key_tx) {
        if (current_tx_key_line) {digitalWrite (current_tx_key_line, LOW);}
        ptt_key();
      }
      if (configuration.sidetone_mode == SIDETONE_ON || configuration.sidetone_mode == SIDETONE_PADDLE_ONLY) {
        noTone(sidetone_line);
      }
      key_state = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::loop_element_lengths(float lengths, float additional_time_ms, int speed_wpm_in, byte sending_type) {

  if ((lengths == 0) or (lengths < 0)) {
    return;
  }

  float element_length;

  element_length = 1200/speed_wpm_in;

  unsigned long endtime = micros() + long(element_length*lengths*1000) + long(additional_time_ms*1000);
  while ((micros() < endtime) && (micros() > 200000)) {  // the second condition is to account for millis() rollover
    if (configuration.keyer_mode != ULTIMATIC) {
      if ((configuration.keyer_mode == IAMBIC_A) && (paddle_pin_read(paddle_left) == LOW ) && (paddle_pin_read(paddle_right) == LOW )) {
          iambic_flag = 1;
      }    
  
      if (being_sent == SENDING_DIT) {
        check_dah_paddle();
      } else {
        if (being_sent == SENDING_DAH) {
          check_dit_paddle();
        }
      }
    } //while ((millis() < endtime) && (millis() > 200))
  }   

  if ((configuration.keyer_mode == IAMBIC_A) && (iambic_flag) && (paddle_pin_read(paddle_left) == HIGH ) && (paddle_pin_read(paddle_right) == HIGH )) {
      iambic_flag = 0;
      dit_buffer = 0;
      dah_buffer = 0;
  }    
} //void loop_element_lengths


//-------------------------------------------------------------------------------------------------------

void Keyer::speed_change(int change) {
  if (((configuration.wpm + change) > wpm_limit_low) && ((configuration.wpm + change) < wpm_limit_high)) {
    speed_set(configuration.wpm + change);
  }
}

void Keyer::speed_set(int wpm_set) {
  configuration.wpm = wpm_set;
  config_dirty = 1;
}
//-------------------------------------------------------------------------------------------------------

void Keyer::sidetone_adj(int hz) {
  if ((configuration.hz_sidetone + hz) > SIDETONE_HZ_LOW_LIMIT && (configuration.hz_sidetone + hz) < SIDETONE_HZ_HIGH_LIMIT) {
    configuration.hz_sidetone = configuration.hz_sidetone + hz;
    config_dirty = 1;
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::switch_to_tx_silent(byte tx) {
  configuration.current_ptt_line = ptt_tx_1; 
  current_tx_key_line = tx_key_line_1; 
  configuration.current_tx = 1; 
  config_dirty = 1;
}

//------------------------------------------------------------------

void Keyer::service_dit_dah_buffers() {

  if (configuration.keyer_mode == IAMBIC_A || configuration.keyer_mode == IAMBIC_B || configuration.keyer_mode == ULTIMATIC) {
    if ((configuration.keyer_mode == IAMBIC_A) && (iambic_flag) && (paddle_pin_read(paddle_left)) && (paddle_pin_read(paddle_right))) {
      iambic_flag = 0;
      dit_buffer = 0;
      dah_buffer = 0;
    } else {
      if (dit_buffer) {
        dit_buffer = 0;
        send_dit(MANUAL_SENDING);
      }
      if (dah_buffer) {
        dah_buffer = 0;
        send_dah(MANUAL_SENDING);
      }
    }
  } else {
    if (configuration.keyer_mode == BUG) {
      if (dit_buffer) {
        dit_buffer = 0;
        send_dit(MANUAL_SENDING);
      }
      if (dah_buffer) {
        dah_buffer = 0;
        tx_and_sidetone_key(1,MANUAL_SENDING);
      } else {
        tx_and_sidetone_key(0,MANUAL_SENDING);
      }
    } else {
      if (configuration.keyer_mode == STRAIGHT) {
        if (dit_buffer) {
          dit_buffer = 0;
          tx_and_sidetone_key(1,MANUAL_SENDING);
        } else {
          tx_and_sidetone_key(0,MANUAL_SENDING);
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------------

void Keyer::beep() {
 tone(sidetone_line, hz_high_beep, 200);
}

void Keyer::boop() {
  tone(sidetone_line, hz_low_beep);
  delay(100);
  noTone(sidetone_line);
}

void Keyer::beep_boop() {
  tone(sidetone_line, hz_high_beep);
  delay(100);
  tone(sidetone_line, hz_low_beep);
  delay(100);
  noTone(sidetone_line);
}

void Keyer::boop_beep() {
  tone(sidetone_line, hz_low_beep);
  delay(100);
  tone(sidetone_line, hz_high_beep);
  delay(100);
  noTone(sidetone_line);
}

int Keyer::paddle_pin_read(int pin_to_read) {
  return digitalRead(pin_to_read);
}

void Keyer::init() {
  initialize_pins();
  initialize_keyer_state();
  initialize_default_modes();
}

void Keyer::update() {
    check_paddles();
    service_dit_dah_buffers();
    check_ptt_tail();
}

