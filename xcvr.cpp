#include <xcvr.h>

void Xcvr::init(void) {
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0);

    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_8MA);

    si5351.set_pll(SI5351_PLL_FIXED, SI5351_PLLA);  
    // load settings and set frequency

    bands[0].startFrequency = 1800000LL;
    bands[1].startFrequency = 3500000LL;
    bands[2].startFrequency = 7000000LL;
    bands[3].startFrequency = 10000000LL;
    bands[4].startFrequency = 14000000LL;
    bands[5].startFrequency = 18000000LL;
    bands[6].startFrequency = 21000000LL;
    bands[7].startFrequency = 24000000LL;
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
    bandIndex &= 0x07; // only 8 bands, starting from 0

    frequency = bands[bandIndex].startFrequency;
    vfoFrequency = frequency - filters[filterIndex].centerFrequency;
    setSideband(bands[bandIndex].isUpperSideband ? USB : LSB);
    setVfoFrequency();
}

unsigned char Xcvr::getBand() {
    return bandIndex;
}

void Xcvr::setExternalBandMode(bool on) {
    if (on)
        this->flags |= EXTERNAL_FILTERS_ON;
    else
        this->flags &= ~EXTERNAL_FILTERS_ON;
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
}

void Xcvr::setBfoFrequency() {
    si5351.set_freq(bfoFrequency, 0ULL, SI5351_CLK2);
}
