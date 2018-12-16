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
const uint8_t PixelPin = 13;

// The light will pull up to 2.5A if all the leds are fully lit, which is too
// much for most usb ports. If you're connected to a computer for programing,
// undefine RELEASE to lower the power requirements.
#define RELEASE

#ifdef RELEASE
const uint8_t saturation = 220;
const float luminance = 0.5f;
#else
const uint8_t saturation = 80;
const float luminance = 0.05f;
#endif

NeoGamma<NeoGammaTableMethod> cgamma;
NeoPixelBus<NeoRgbwFeature, NeoWs2813Method> ring(PixelCount, PixelPin);

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
    virtual void stop() = 0;
};

class modeOff : public animMode
{
public:
    void setup() override {ring.ClearTo(black); ring.Show();}
    void run() override {delayMicroseconds(20000);}
    void stop() override {}
};

class modeLight : public animMode
{
public:
    void setup() override {ring.ClearTo(white); ring.Show();}
    void run() override {delayMicroseconds(20000);}
    void stop() override {}
};

class modeFader : public animMode
{
    // 15s between colors?
    const int fadeDelay = 15000;
    NeoPixelAnimator animations{1};
    animState state[1];
    int inOrOut;

    void animUpd(const AnimationParam& param);
    void fadeInOut();
public:
    void setup() override;
    void run() override;
    void stop() override;
};

class modeRotator : public animMode
{
    // Location of the two dots chasing each other
    int dot1;
    int dot2;

    RgbwColor cols1[PixelCount / 2];
    RgbwColor cols2[PixelCount / 2];

    animState state[PixelCount];
    NeoPixelAnimator animations{PixelCount};
    // For the rotator, delay this long before moving to the next pixel.
    const uint16_t rotateDelay = 200;
    void animUpd(const AnimationParam& param);
    void spin();

public:
    void setup() override;
    void run() override;
    void stop() override;
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

//
// modeRotator:
//
void modeRotator::animUpd(const AnimationParam& param)
{
    auto progress = param.progress;

    for (auto& s : state) {
        // Serial.printf("pixel %d, sc %d %d %d, ec %d, %d, %d\n",
        //     s.pixel, s.StartColor.R, s.StartColor.G, s.StartColor.B, 
        //     s.EndColor.R, s.EndColor.G, s.EndColor.B);
        auto col = RgbwColor::LinearBlend(s.StartColor, s.EndColor, progress);
        ring.SetPixelColor(s.pixel, col);
    }
}

// This is the animation routine called when the next pixel needs to start
// lighting up.
void modeRotator::spin()
{
    dot1 = nextPix(dot1);
    dot2 = nextPix(dot2);

    // rotate the target color to the next pixel. All other dots should fade to
    // black.
    auto p = dot1;
    for (auto col : cols1) {
        auto& s = state[p];
        s.StartColor = s.EndColor;
        s.EndColor = col;
        p = prevPix(p);
    }
    p = dot2;
    for (auto col : cols2) {
        auto& s = state[p];
        s.StartColor = s.EndColor;
        s.EndColor = col;
        p = prevPix(p);
    }

    auto updfn = [this](const AnimationParam& p) { animUpd(p); };
    animations.StartAnimation(0, rotateDelay, updfn);
}

void modeRotator::setup()
{
    ring.ClearTo(black);
    ring.Show();

    dot1 = 0;
    dot2 = PixelCount / 2;

    // pick two random colors to chase each other.
    auto col1 = HslColor(random(360) / 360.0f, 1.0f, luminance);
    auto col2 = HslColor(random(360) / 360.0f, 1.0f, luminance);

    for (int c = 0; c < PixelCount / 2; c++)
    {
        float progress = float(c) / float(PixelCount / 2);
        cols1[c] = RgbwColor::LinearBlend(col1, black, progress);
        cols2[c] = RgbwColor::LinearBlend(col2, black, progress);
    }

    int pix = 0;
    for (auto& pixState : state) {
        pixState.pixel = pix++;
        pixState.StartColor = black;
        pixState.EndColor = black;
    }

    state[dot1].EndColor = col1;
    state[dot2].EndColor = col2;
}

void modeRotator::run()
{
    if(animations.IsAnimating())
    {
        animations.UpdateAnimations();
        ring.Show();
    } 
    else
    {
        // Start an animation
        spin();
    }}

void modeRotator::stop()
{
    animations.StopAll();
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

void modeFader::stop()
{
    animations.StopAll();
}

void runMode(int mode)
{
    static int lastMode = -1;

    if(mode != lastMode)
    {
        modes[mode]->stop();
        vTaskDelay(20);
        modes[mode]->setup();
    }

    lastMode = mode;

    modes[mode]->run();
}

int switchMode(int mode)
{
    static int lastVal = 0;
    static unsigned long lastChange = 0;

    int val = digitalRead(SwitchPin);
    // If the switch has changed state
    if(val ^ lastVal)
    {
        // debounce. If there's been a change, we won't report any other changes for
        // 5ms
        if ((millis() - lastChange) < 5)
            return lastVal;

        // switch was pressed, now is released. Increment the mode.
        if(val == 0)
            mode = (mode + 1) % modeCount;

        // store the time for debouncing
        lastChange = millis();
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
        // this keeps the watchdog from barking.
        vTaskDelay(1);

        // check whether the switch has been pressed.
        mode = switchMode(mode);

        runMode(mode);
    }
}

