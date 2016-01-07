#include <xcvr.h>

XcvrUi::XcvrUi() {
}

ClickEncoder* XcvrUi::encoder;
void timerIsr() {
    XcvrUi::encoder->service();
}

void XcvrUi::init(Xcvr& xcvr) {
    this->xcvr = &xcvr;
    display = new U8GLIB_SSD1306_128X64(13, 12, 0, 11, 10); // CS is not used
    encoder = new ClickEncoder(A1, A0, A2);

    Timer1.initialize(1000);
    Timer1.attachInterrupt(timerIsr);
}

void XcvrUi::update() {
    currentEncoderValue += encoder->getValue();
    bool encoderChanged = currentEncoderValue != lastEncoderValue;

    if (encoderChanged) {
        lastEncoderValue = currentEncoderValue;
        int amountToAdd = (currentEncoderValue / 3);
        if (amountToAdd != 0) {
            currentEncoderValue -= amountToAdd * 3;

            if (xcvr->isRitOn()) {
                xcvr->ritIncrement(amountToAdd * 10);
            } else {
                xcvr->incrementFrequency(amountToAdd * stepSize);
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


    if (xcvr->hasStatusChanged()) {
        render();
        xcvr->clearStatusChange();
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

    display->setFont(u8g_font_8x13B);
    display->drawStr(0, 10, frequencyRepr);

    if (xcvr->isRitOn()) {
        display->setFont(u8g_font_helvB08);
        display->drawStr(90, 10, ritRepr);
    }
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
