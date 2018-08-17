#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <functional>

// This firmware is for an ESP32 board connected to a NeoPixel ring, housed
// inside a mathmos light whose original electronics stopped working.
//
// The mathmo is set up with several modes. The power switch is used to cycle
// through the various modes.
//
// Mode1: Close to the mathos' original animation, it fades slowly from one
//        color to the next. White is not used.

// Pin 12 is connected to the switch and will read high when the switch is
// pressed.
// Pin 21 is the output to the neopixel bus.

const uint16_t PixelCount = 24;
const uint8_t SwitchPin = 12;
const uint8_t PixelPin = 21;
// TODO: crank this up to 220 or so for release
const uint8_t saturation = 80;

NeoGamma<NeoGammaTableMethod> cgamma;
NeoPixelBus<NeoRgbwFeature, Neo800KbpsMethod> ring(PixelCount, PixelPin);

RgbwColor black(0,0,0,0);
RgbwColor red(saturation, 0, 0, 0);
RgbwColor green(0, saturation, 0, 0);
RgbwColor blue(0, 0, saturation, 0);
RgbwColor white(0, 0, 0, saturation);

struct animState
{
    RgbwColor StartColor;
    RgbwColor EndColor;
    uint16_t pixel;
};

class animMode
{
public:
    virtual void setup() = 0;
    virtual void run() = 0;
};

class modeOff : public animMode
{
public:
    void setup() override {ring.ClearTo(black); ring.Show();}
    void run() override {delayMicroseconds(20000);}
};

class modeLight : public animMode
{
public:
    void setup() override {ring.ClearTo(white); ring.Show();}
    void run() override {delayMicroseconds(20000);}
};

class modeFader : public animMode
{
    // 10s between colors?
    const int fadeDelay = 10000;
    // TODO: Crank this up to 0.4-0.5f for release
    const float luminance = 0.05f;
    NeoPixelAnimator animations{1};
    animState state[1];
    int inOrOut;

    void animUpd(const AnimationParam& param);
    void fadeInOut();
public:
    void setup() override;
    void run() override;
};

class modeRotator : public animMode
{
public:
    void setup() override;
    void run() override;
    void drawTail();
};

animMode* modes[] = {
    new modeOff{}, 
    new modeFader{}, 
    new modeRotator{}, 
    new modeLight{}};

const auto modeCount = countof(modes);

void setup()
{
    Serial.begin(115200);
    // wait for serial attach
    while(!Serial);

    Serial.println("\nInitializing...");
    Serial.flush();

    pinMode(SwitchPin, INPUT);

    // turn all pixels off
    ring.Begin();
    ring.Show();

    Serial.println("Running...");
}

int prevPix(int pix)
{
    return (pix + PixelCount - 1) % PixelCount;
}

int nextPix(int pix)
{
    return (pix + 1) % PixelCount;
}

const int16_t tailLen = 10;
const float maxLightness = 0.4f;
const int animCount = 10;
animState state[animCount];
NeoPixelAnimator animations(animCount);
// For the rotator, delay this long before moving to the next pixel.
const uint16_t rotateDelay = 1000 / PixelCount;

void rotateUpd(const AnimationParam& param)
{
    if (param.state == AnimationState_Completed)
    {
        // done, time to restart this position tracking animation/timer.
        animations.RestartAnimation(param.index);

        ring.RotateRight(1);
    }
}

void modeRotator::drawTail()
{
    float hue = random(360) / 360.0f;
    for (uint16_t ix = 0; ix < ring.PixelCount() && ix < tailLen; ix++)
    {
        float lightness = ix * maxLightness / tailLen;
        RgbwColor col = HslColor(hue, 1.0f, lightness);
        col = cgamma.Correct(col);

        ring.SetPixelColor(ix, col);
    }
}

// This works, but the rotate method isn't animated. Fix.
void modeRotator::setup()
{
    ring.ClearTo(black);
    drawTail();
    animations.StartAnimation(0, rotateDelay, rotateUpd);
}

void modeRotator::run()
{
    animations.UpdateAnimations();
    ring.Show();
}

//
// modeFader
//
void modeFader::animUpd(const AnimationParam& param)
{
    auto progress = param.progress;
    //auto progress = NeoEase::QuadraticInOut(param.progress);
    RgbwColor col = RgbwColor::LinearBlend(
        state[0].StartColor,
        state[0].EndColor,
        progress);

    //col = cgamma.Correct(col);
    for(uint16_t pixel = 0; pixel < PixelCount; pixel++)
    {
        ring.SetPixelColor(pixel, col);
    }
}

void modeFader::fadeInOut()
{
    // For fade out, target color is black.
    RgbwColor col = black;
    // For fade in, it's a random color.
    if(inOrOut == 0)
    {
        // Fade to a random color
        col = HslColor(random(360) / 360.0f, 1.0f, luminance);
    }

    state[0].StartColor = state[0].EndColor;
    state[0].EndColor = col;

    // Use a member function for animation callbacks.
    auto updfn = [this](const AnimationParam& p) { animUpd(p); };
    animations.StartAnimation(0, fadeDelay, updfn);

    // flip the state. (Commented out so that it fades from color to color)
    //inOrOut ^= 1;
}

void modeFader::setup()
{
    inOrOut = 0;
    state[0].StartColor = black;
    state[0].EndColor = black;
    ring.ClearTo(black);
    ring.Show();
}

void modeFader::run()
{
    if(animations.IsAnimating())
    {
        animations.UpdateAnimations();
        ring.Show();
    } 
    else
    {
        // Start an animation
        fadeInOut();
    }
}

void runMode(int mode)
{
    static int lastMode = -1;

    if(mode != lastMode)
        modes[mode]->setup();

    lastMode = mode;

    modes[mode]->run();
}

int switchMode(int mode)
{
    static int lastVal = 0;
    int val = digitalRead(SwitchPin);
    // If the switch has changed state
    if(val ^ lastVal)
    {
        // switch was pressed, now is released. Increment the mode.
        if(val == 0)
            mode = (mode + 1) % modeCount;
    }
    lastVal = val;
    return mode;
}

extern "C" void app_main() 
{
    // This is the current animation mode. Mode0 is off.
    int mode = 0;

    setup();

    while(true)
    {
        mode = switchMode(mode);
        runMode(mode);
    }
}

